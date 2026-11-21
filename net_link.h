#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "config.h"
#include "audio_io.h"

// ============================================================================
// REDE — HTTP/1.1 com keep-alive para o Netlify (uma sessao TLS reaproveitada entre as
// partes de uma fala longa) e voz -> texto direto no Groq Whisper (upload enquanto fala).
// Nunca ha duas sessoes TLS abertas ao mesmo tempo (~40 KB cada no ESP32 sem PSRAM).
// ============================================================================
struct Head {
  int status = 0;
  long length = -1;      // -1 = desconhecido (chunked ou ate fechar)
  bool chunked = false;
  bool close = false;
  int parts = 1;
  bool listen = false;
  String actions;        // X-Actions (base64url de JSON)
  String textId;         // X-Text-Id
};

class Netlify {
public:
  String host;
  uint32_t lastUse = 0;

  void begin() {
    String u = NETLIFY_URL;
    u = u.substring(u.indexOf("://") + 3);
    int slash = u.indexOf('/');
    host = slash >= 0 ? u.substring(0, slash) : u;
    c.setInsecure();     // o token do dispositivo protege o acesso; troque por setCACert se quiser validar
  }
  void close() { c.stop(); }

  /** Envia uma requisicao. Reaproveita a conexao se ela estiver viva. */
  bool request(const char* method, const String& path, const char* ctype = nullptr, const String* body = nullptr) {
    for (int attempt = 0; attempt < 2; ++attempt) {
      bool reused = c.connected() && millis() - lastUse < 25000;
      if (!reused) {
        c.stop();
        c.setTimeout(20);
        if (!c.connect(host.c_str(), 443)) { Serial.println("❌ Netlify inacessivel"); return false; }
      }
      String h = String(method) + " " + path + " HTTP/1.1\r\nHost: " + host +
                 "\r\nX-Device-Token: " + DEVICE_TOKEN + "\r\nUser-Agent: JarvisESP32/" JARVIS_FW_VERSION "\r\nConnection: keep-alive\r\n";
      if (ctype) h += String("Content-Type: ") + ctype + "\r\n";
      if (body) h += String("Content-Length: ") + (unsigned)body->length() + "\r\n";
      h += "\r\n";
      bool ok = c.print(h) == h.length() && (!body || body->length() == 0 || c.print(*body) == body->length());
      if (ok) { lastUse = millis(); return true; }
      c.stop();                                   // conexao velha: tenta de novo com uma nova
    }
    return false;
  }

  /** Le status e cabecalhos. */
  bool readHead(Head& h, uint32_t timeoutMs = 30000) {
    h = Head();
    String line;
    if (!readLine(line, timeoutMs)) { c.stop(); return false; }
    int sp = line.indexOf(' ');
    h.status = sp > 0 ? line.substring(sp + 1, sp + 4).toInt() : 0;
    while (readLine(line, 8000)) {
      if (line.length() == 0) break;
      int colon = line.indexOf(':');
      if (colon <= 0) continue;
      String k = line.substring(0, colon), v = line.substring(colon + 1);
      k.toLowerCase(); v.trim();
      if (k == "content-length") h.length = v.toInt();
      else if (k == "transfer-encoding") { v.toLowerCase(); h.chunked = v.indexOf("chunked") >= 0; }
      else if (k == "connection") { v.toLowerCase(); h.close = v == "close"; }
      else if (k == "x-parts") h.parts = v.toInt() > 0 ? v.toInt() : 1;
      else if (k == "x-listen") h.listen = v == "1";
      else if (k == "x-actions") h.actions = v;
      else if (k == "x-text-id") h.textId = v;
    }
    cur = h;
    remaining = h.chunked ? 0 : h.length;
    bodyDone = (!h.chunked && h.length == 0) || h.status == 204 || h.status == 304;
    lastUse = millis();
    return true;
  }

  /** Le ate n bytes do corpo (decodifica chunked). 0 = fim, -1 = erro. */
  int read(uint8_t* buf, size_t n, uint32_t timeoutMs = 15000) {
    if (bodyDone) return 0;
    if (cur.chunked && remaining == 0) {
      String line;
      if (!readLine(line, timeoutMs)) return fail();
      if (line.length() == 0 && !readLine(line, timeoutMs)) return fail();   // CRLF do chunk anterior
      remaining = strtol(line.c_str(), nullptr, 16);
      if (remaining == 0) { readLine(line, 2000); bodyDone = true; return 0; }
    }
    size_t want = n;
    if (remaining > 0 && (long)want > remaining) want = remaining;
    uint32_t t0 = millis();
    int got = 0;
    while (got == 0) {
      int av = c.available();
      if (av > 0) { got = c.read(buf, want < (size_t)av ? want : (size_t)av); break; }
      if (!c.connected()) { if (remaining < 0) { bodyDone = true; return 0; } return fail(); }
      if (millis() - t0 > timeoutMs) return fail();
      delay(2);
    }
    if (got < 0) return fail();
    if (remaining > 0) { remaining -= got; if (remaining == 0 && !cur.chunked) bodyDone = true; }
    lastUse = millis();
    return got;
  }

  String readBody(size_t maxLen = 8192) {
    String s;
    uint8_t b[512];
    int n;
    while ((n = read(b, sizeof(b))) > 0) if (s.length() < maxLen) s.concat((const char*)b, n);
    finish();
    return s;
  }

  /** Termina a resposta atual (para reaproveitar a conexao) ou fecha se nao der. */
  void finish() {
    uint8_t b[256];
    uint32_t t0 = millis();
    while (!bodyDone && millis() - t0 < 3000) if (read(b, sizeof(b), 1500) <= 0) break;
    if (!bodyDone || cur.close) c.stop();
  }

  /** Toca o corpo (MP3) da resposta atual. false = interrompido/erro. */
  bool playBody(AudioIO& audio) {
    uint8_t b[1024];
    int n;
    while ((n = read(b, sizeof(b))) > 0) {
      if (!audio.push(b, n)) { c.stop(); return false; }
    }
    if (n < 0) return false;
    finish();
    return true;
  }

private:
  WiFiClientSecure c;
  Head cur;
  long remaining = 0;
  bool bodyDone = true;

  int fail() { c.stop(); bodyDone = true; return -1; }
  bool readLine(String& out, uint32_t timeoutMs) {
    out = "";
    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
      while (c.available()) {
        char ch = (char)c.read();
        if (ch == '\n') { out.trim(); return true; }
        if (out.length() < 1500) out += ch;
      }
      if (!c.connected()) return false;
      delay(2);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------- Groq Whisper
// O audio sobe para o Groq ENQUANTO voce fala (chunked); o texto chega ~0,4 s depois do fim.
static void putChunk(WiFiClient& c, const uint8_t* d, size_t n) {
  char hdr[10];
  snprintf(hdr, sizeof(hdr), "%X\r\n", (unsigned)n);
  c.print(hdr); c.write(d, n); c.print("\r\n");
}

/** Retorna o texto reconhecido ("" = nada falado, "#erro" = falha). */
String transcribeWithGroq(AudioIO& audio, const String& key, const String& model) {
  if (key.length() < 10) { Serial.println("❌ Chave do Groq ausente (painel > Chaves ou GROQ_API_KEY)"); return "#erro"; }
  WiFiClientSecure g;
  g.setInsecure();
  g.setTimeout(20);
  if (!g.connect("api.groq.com", 443)) { Serial.println("❌ Groq inacessivel"); return "#erro"; }
  const char* B = "----JarvisESP32";
  String head = String("--") + B + "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n" + model +
                "\r\n--" + B + "\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\npt\r\n--" + B +
                "\r\nContent-Disposition: form-data; name=\"response_format\"\r\n\r\ntext\r\n--" + B +
                "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"voz.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
  String tail = String("\r\n--") + B + "--\r\n";
  g.print(String("POST /openai/v1/audio/transcriptions HTTP/1.1\r\nHost: api.groq.com\r\nAuthorization: Bearer ") + key +
          "\r\nContent-Type: multipart/form-data; boundary=" + B + "\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n");
  putChunk(g, (const uint8_t*)head.c_str(), head.length());
  // cabecalho WAV 16 kHz mono 16-bit (tamanho maximo; o Whisper aceita o arquivo mais curto)
  const uint32_t dataLen = (uint32_t)MIC_SAMPLE_RATE * MAX_RECORD_SECONDS * 2;
  uint8_t w[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,2,0,16,0,'d','a','t','a',0,0,0,0};
  uint32_t riff = dataLen + 36, sr = MIC_SAMPLE_RATE, br = MIC_SAMPLE_RATE * 2;
  memcpy(w + 4, &riff, 4); memcpy(w + 24, &sr, 4); memcpy(w + 28, &br, 4); memcpy(w + 40, &dataLen, 4);
  putChunk(g, w, sizeof(w));

  audio.chime();
  Serial.println("🎙️ Ouvindo...");
  bool speech = audio.captureChunked(g);
  putChunk(g, (const uint8_t*)tail.c_str(), tail.length());
  g.print("0\r\n\r\n");
  if (!speech) { g.stop(); return ""; }
  audio.blip();

  String resp;
  uint32_t t0 = millis();
  while ((g.connected() || g.available()) && millis() - t0 < 15000) {
    while (g.available()) { char ch = (char)g.read(); if (resp.length() < 3000) resp += ch; }
    delay(2);
  }
  g.stop();
  int status = resp.substring(9, 12).toInt();
  int body = resp.indexOf("\r\n\r\n");
  String text = body >= 0 ? resp.substring(body + 4) : String();
  if (resp.indexOf("chunked") > 0 && body >= 0) {          // remove os tamanhos de chunk
    String out;
    int i = 0;
    while (i < (int)text.length()) {
      int e = text.indexOf("\r\n", i);
      if (e < 0) break;
      long n = strtol(text.substring(i, e).c_str(), nullptr, 16);
      if (n <= 0) break;
      out += text.substring(e + 2, e + 2 + n);
      i = e + 2 + n + 2;
    }
    text = out;
  }
  text.trim();
  if (status != 200) { Serial.printf("❌ Groq HTTP %d: %s\n", status, text.substring(0, 160).c_str()); return "#erro"; }
  return text;
}
