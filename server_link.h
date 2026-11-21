#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include "config.h"

// ============================================================================
// SERVER LINK — conexao HTTP/HTTPS com o servidor J.A.R.V.I.S.
// Usa UMA conexao por vez (o long-poll e fechado antes de cada conversa), entao o
// ESP32 nunca segura mais de uma sessao TLS (~40 KB) ao mesmo tempo.
// ============================================================================
struct HttpHead {
  int status = 0;
  long length = -1;
  bool listenAgain = false;
};

class ServerLink {
public:
  bool tls = false;
  String host;
  uint16_t port = 80;
  String base;           // prefixo de caminho, se a URL tiver um

  void begin() {
    String u = JARVIS_SERVER_URL;
    tls = u.startsWith("https://");
    u = u.substring(u.indexOf("://") + 3);
    int slash = u.indexOf('/');
    base = slash >= 0 ? u.substring(slash) : String("");
    if (base.endsWith("/")) base.remove(base.length() - 1);
    String hp = slash >= 0 ? u.substring(0, slash) : u;
    int colon = hp.indexOf(':');
    host = colon >= 0 ? hp.substring(0, colon) : hp;
    port = colon >= 0 ? (uint16_t)hp.substring(colon + 1).toInt() : (tls ? 443 : 80);
#ifdef JARVIS_ROOT_CA
    secure.setCACert(JARVIS_ROOT_CA);
#else
    secure.setInsecure();
#endif
  }

  // Abre a conexao (fecha a anterior). Retorna nullptr se o servidor estiver fora.
  WiFiClient* connect() {
    close();
    WiFiClient* c = tls ? (WiFiClient*)&secure : &plain;
    if (!c->connect(host.c_str(), port)) return nullptr;
    return c;
  }

  void close() {
    plain.stop();
    secure.stop();
  }

  WiFiClient* current() { return tls ? (WiFiClient*)&secure : &plain; }

  void writeHead(WiFiClient& c, const char* method, const String& path, const char* extra = "") {
    c.print(String(method) + " " + base + path + " HTTP/1.1\r\nHost: " + host + "\r\n"
            "X-Device-Token: " + JARVIS_DEVICE_TOKEN + "\r\nUser-Agent: jarvis-esp32/" JARVIS_FW_VERSION "\r\n"
            "Connection: close\r\n" + extra + "\r\n");
  }

  // Le a linha de status e os cabecalhos que interessam (bloqueia ate timeoutMs).
  static bool readHead(WiFiClient& c, HttpHead& h, uint32_t timeoutMs) {
    const uint32_t t0 = millis();
    String line;
    bool first = true;
    while (millis() - t0 < timeoutMs) {
      if (!c.available()) {
        if (!c.connected()) return false;
        delay(5);
        continue;
      }
      char ch = (char)c.read();
      if (ch == '\r') continue;
      if (ch != '\n') { if (line.length() < 200) line += ch; continue; }
      if (line.length() == 0) return h.status > 0;             // fim dos cabecalhos
      if (first) {
        int sp = line.indexOf(' ');
        h.status = sp > 0 ? line.substring(sp + 1, sp + 4).toInt() : 0;
        first = false;
      } else {
        String low = line;
        low.toLowerCase();
        if (low.startsWith("content-length:")) h.length = low.substring(15).toInt();
        else if (low.startsWith("x-listen-again:")) h.listenAgain = low.indexOf('1') > 0;
      }
      line = "";
    }
    return false;
  }

  static String readBody(WiFiClient& c, long len, uint32_t timeoutMs = 5000) {
    String out;
    const uint32_t t0 = millis();
    while ((len < 0 || (long)out.length() < len) && millis() - t0 < timeoutMs) {
      if (c.available()) { out += (char)c.read(); continue; }
      if (!c.connected()) break;
      delay(2);
    }
    return out;
  }

private:
  WiFiClient plain;
  WiFiClientSecure secure;
};
