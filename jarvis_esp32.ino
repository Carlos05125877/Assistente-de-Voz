/*
 * ============================================================================
 * J.A.R.V.I.S. — ESP-WROOM-32 HIBRIDO (nucleo local) + Netlify (sob demanda)
 * ============================================================================
 * Fica SEMPRE no ESP32 (de graca, funciona sem internet):
 *   botao, microfone, alto-falantes, alarmes, lembretes, timers, horarios das rotinas.
 * Vai para a nuvem SO quando precisa:
 *   voz -> texto ......... direto no Groq Whisper (upload enquanto voce fala)
 *   IA + ferramentas ..... Netlify /api/turn (Gemini, YouVersion, Spotify, casa, clima...)
 *   voz do Jarvis ........ MP3 24 kHz do Edge TTS, tocado enquanto chega (libhelix)
 *   painel ............... Netlify; o ESP pega comandos no sync (3 min, ou 10 s com o painel aberto)
 *
 * Um turno de voz = Groq (texto) + 1 chamada ao Netlify. Textos longos chegam em partes
 * pela mesma conexao TLS; o buffer de 40 KB toca uma parte enquanto baixa a proxima.
 * Atualizacao do firmware pelo WiFi: ArduinoOTA (host "jarvis", senha OTA_PASSWORD).
 * ============================================================================
 */
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#include "config.h"
#include "audio_io.h"
#include "net_link.h"
#include "agenda.h"

AudioIO audio;
Netlify net;
Agenda agenda;
Preferences prefs;

uint32_t nextSyncAt = 5000, lastSyncOk = 0;
int syncFails = 0;
bool hot = false, busy = false, listenRequested = false, restartRequested = false;

// ---------------------------------------------------------------- utilitarios
static int b64val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-' || c == '+') return 62;
  if (c == '_' || c == '/') return 63;
  return -1;
}
String b64urlDecode(const String& in) {
  String out;
  uint32_t buf = 0;
  int bits = 0;
  for (size_t i = 0; i < in.length(); ++i) {
    int v = b64val(in[i]);
    if (v < 0) continue;
    buf = (buf << 6) | v; bits += 6;
    if (bits >= 8) { bits -= 8; out += (char)((buf >> bits) & 0xFF); }
  }
  return out;
}
String urlEncode(const String& s) {
  String o;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); ++i) {
    uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
  }
  return o;
}
void fillState(JsonObject st) {
  agenda.toJson(st);
  st["vol"] = audio.volume;
  st["heap"] = ESP.getFreeHeap() / 1024;
  st["minh"] = ESP.getMinFreeHeap() / 1024;
  st["rssi"] = WiFi.RSSI();
  st["up"] = millis() / 1000;
  st["fw"] = JARVIS_FW_VERSION;
  st["ip"] = WiFi.localIP().toString();
  st["busy"] = busy;
}

// ---------------------------------------------------------------- acoes vindas do Netlify
void sayParts(const String& id, int n);

void runAction(JsonObject a) {
  if (agenda.apply(a)) { audio.ok(); return; }
  const char* t = a["t"] | "";
  if (!strcmp(t, "vol")) { audio.setVolume(a["v"] | DEFAULT_VOLUME_PCT); prefs.putInt("vol", audio.volume); audio.ok(); }
  else if (!strcmp(t, "tone")) audio.playMode(a["m"] | "chime");
  else if (!strcmp(t, "say")) sayParts(a["id"] | "", a["n"] | 1);
  else if (!strcmp(t, "listen")) listenRequested = true;
  else if (!strcmp(t, "restart")) restartRequested = true;
}

/** Aplica as acoes de um turno (vem no cabecalho X-Actions) antes de tocar a resposta. */
void runActionsHeader(const String& b64) {
  if (b64.length() < 3) return;
  DynamicJsonDocument doc(3072);
  if (deserializeJson(doc, b64urlDecode(b64))) return;
  for (JsonObject a : doc.as<JsonArray>()) {
    const char* t = a["t"] | "";
    if (!strcmp(t, "say")) continue;               // a fala do turno ja vem no corpo
    if (agenda.apply(a)) continue;                 // o "ok" sonoro fica por conta da resposta falada
    runAction(a);
  }
}

// ---------------------------------------------------------------- fala
/** Toca as partes `from`..n-1 de um texto guardado no Netlify (mesma conexao TLS). */
bool playStoredParts(const String& id, int from, int n) {
  for (int p = from; p < n; ++p) {
    if (!net.request("GET", "/api/say?id=" + id + "&p=" + p)) return false;
    Head h;
    if (!net.readHead(h, 30000) || h.status != 200) { net.finish(); return false; }
    if (!net.playBody(audio)) return false;           // botao interrompeu
  }
  return true;
}
void sayParts(const String& id, int n) {
  if (!id.length()) return;
  audio.startSpeech();
  if (playStoredParts(id, 0, n)) audio.drain(); else audio.abortPlayback();
  if (!hot) net.close();
}
/** Fala um texto curto gerado no ESP (lembrete, timer). Sem internet: false. */
bool speakText(const String& text) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!net.request("GET", "/api/say?text=" + urlEncode(text))) return false;
  Head h;
  if (!net.readHead(h, 20000) || h.status != 200) { net.finish(); return false; }
  audio.startSpeech();
  bool ok = net.playBody(audio) && audio.drain();
  if (!ok) audio.abortPlayback();
  if (!hot) net.close();
  return true;
}

/** Um turno no Netlify. Retorna true se o Jarvis fez uma pergunta (ouvir de novo). */
bool turn(const String& text) {
  DynamicJsonDocument doc(4096);
  doc["text"] = text;
  fillState(doc.createNestedObject("st"));
  String body;
  serializeJson(doc, body);
  doc.clear();
  const uint32_t t0 = millis();
  if (!net.request("POST", "/api/turn", "application/json", &body)) { audio.error(); return false; }
  Head h;
  if (!net.readHead(h, 45000)) { Serial.println("❌ Netlify não respondeu"); audio.error(); return false; }
  Serial.printf("⚡ /api/turn HTTP %d em %u ms (%d partes)\n", h.status, (unsigned)(millis() - t0), h.parts);
  lastSyncOk = millis();
  runActionsHeader(h.actions);
  if (h.status == 401) { Serial.println("❌ DEVICE_TOKEN recusado pelo Netlify"); audio.error(); net.finish(); return false; }
  if (h.status == 204) { net.finish(); return false; }
  if (h.status != 200) { net.finish(); audio.error(); return false; }
  audio.startSpeech();
  bool ok = net.playBody(audio) && playStoredParts(h.textId, 1, h.parts);
  if (ok) ok = audio.drain(); else audio.abortPlayback();
  if (!hot) net.close();
  return ok && h.listen;
}

/** Conversa: microfone -> Groq -> Netlify -> alto-falantes. typed != "" pula o microfone. */
void converse(String typed = "") {
  busy = true;
  for (int i = 0; i < 3; ++i) {
    String text = typed;
    if (!text.length()) {
      net.close();                                  // libera a sessao TLS para o Groq
      const uint32_t t0 = millis();
      text = transcribeWithGroq(audio, agenda.groqKey, agenda.sttModel);
      if (text == "#erro") { audio.error(); break; }
      if (!text.length()) { audio.noSpeech(); break; }
      Serial.printf("📝 Você (%u ms): %s\n", (unsigned)(millis() - t0), text.c_str());
    }
    if (!turn(text)) break;
    typed = "";                                     // o Jarvis perguntou algo: ouve de novo
  }
  busy = false;
  while (AudioIO::buttonPressed()) delay(10);
}

// ---------------------------------------------------------------- eventos da agenda local
void handleEvent(const Event& ev) {
  busy = true;
  if (ev.kind == EV_ALARM) {
    Serial.printf("⏰ Alarme: %s\n", ev.text);
    const uint32_t t0 = millis();
    audio.startSpeech();
    while (millis() - t0 < 60000 && !audio.interrupted()) { audio.alarm(); delay(350); }
    while (AudioIO::buttonPressed()) delay(10);
    if (ev.briefing) turn("__briefing__");          // devocional/briefing logo depois de acordar
  } else if (ev.kind == EV_REMINDER) {
    Serial.printf("🔔 Lembrete: %s\n", ev.text);
    audio.bell();
    if (!speakText(String("Lembrete: ") + ev.text + ".")) { delay(400); audio.bell(); }
  } else if (ev.kind == EV_TIMER) {
    Serial.printf("⏲️ Timer: %s\n", ev.text);
    audio.alarm();
    if (!speakText(String("O timer ") + ev.text + " terminou.")) audio.alarm();
  } else if (ev.kind == EV_ROUTINE) {
    Serial.printf("▶️ Rotina agendada: %s\n", ev.text);
    turn(String("__routine:") + ev.text);
  }
  busy = false;
}

// ---------------------------------------------------------------- sync com o Netlify
void doSync() {
  DynamicJsonDocument doc(4096);
  fillState(doc.createNestedObject("st"));
  String body;
  serializeJson(doc, body);
  doc.clear();
  Head h;
  String resp;
  bool ok = net.request("POST", "/api/sync", "application/json", &body) && net.readHead(h, 20000);
  if (ok) resp = net.readBody(6144);
  if (!ok || h.status != 200) {
    net.close();
    syncFails++;
    uint32_t wait = h.status == 401 ? 600 : min(60 * syncFails, 300);
    if (h.status == 401) Serial.println("❌ DEVICE_TOKEN recusado pelo Netlify (confira secrets_local.h)");
    nextSyncAt = millis() + wait * 1000UL;
    hot = false;
    return;
  }
  syncFails = 0;
  lastSyncOk = millis();
  agenda.dirty = false;
  DynamicJsonDocument r(6144);
  if (deserializeJson(r, resp)) { nextSyncAt = millis() + 60000; return; }
  hot = r["hot"] | false;
  nextSyncAt = millis() + (uint32_t)(r["next"] | SYNC_IDLE_S) * 1000UL;
  if (!hot) net.close();                            // fora do modo painel, libera ~40 KB de RAM
  if (r.containsKey("cfg")) agenda.applyConfig(r["cfg"].as<JsonObject>());
  if (!agenda.timeValid() && r.containsKey("t")) {  // sem NTP ainda: usa a hora do Netlify
    struct timeval tv = {(time_t)(r["t"].as<long>()), 0};
    settimeofday(&tv, nullptr);
  }
  JsonArray q = r["q"].as<JsonArray>();
  if (q.size()) {
    busy = true;
    for (JsonObject a : q) { Serial.printf("🛰️ Painel: %s\n", (const char*)(a["t"] | "?")); runAction(a); }
    busy = false;
    nextSyncAt = millis() + 1500;                   // conta ao painel o novo estado
  }
}

// ---------------------------------------------------------------- setup / loop
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME_MDNS);
  WiFi.setSleep(false);                             // menos latencia no audio
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("📶 WiFi");
  for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; ++i) { delay(250); Serial.print('.'); }
  Serial.printf("\n📶 %s  IP %s\n", WiFi.status() == WL_CONNECTED ? "conectado" : "FALHOU (seguindo offline)", WiFi.localIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_WAKE_BUTTON, INPUT_PULLUP);
  pinMode(PIN_EXTERNAL_BTN, INPUT_PULLUP);
  pinMode(PIN_STATUS_LED, OUTPUT);
  prefs.begin("jarvis", false);
  audio.begin();                                    // primeiro: reserva o buffer MP3 com a RAM inteira
  audio.setVolume(prefs.getInt("vol", DEFAULT_VOLUME_PCT));
  agenda.begin(&prefs);
  connectWiFi();
  configTzTime(NTP_TZ, "pool.ntp.org", "a.st1.ntp.br");
  net.begin();
  MDNS.begin(HOSTNAME_MDNS);
  ArduinoOTA.setHostname(HOSTNAME_MDNS);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();
  audio.chime();
  Serial.printf("✅ J.A.R.V.I.S. %s | %s | %d alarmes, %d lembretes | RAM livre %u KB\n", JARVIS_FW_VERSION, NETLIFY_URL,
                agenda.nAl, agenda.nRem, (unsigned)(ESP.getFreeHeap() / 1024));
  Serial.println("👉 Botão BOOT/GPIO4 = falar | ENTER no Serial = microfone | texto + ENTER = pergunta digitada");
}

void loop() {
  ArduinoOTA.handle();
  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t lastTry = 0;
    if (millis() - lastTry > 15000) { lastTry = millis(); WiFi.reconnect(); }
  }

  bool pressed = AudioIO::buttonPressed();
  if (pressed) { delay(40); pressed = AudioIO::buttonPressed(); }
  String typed;
  bool serialMic = false;
  if (Serial.available()) {
    typed = Serial.readStringUntil('\n');
    typed.trim();
    serialMic = typed.length() == 0;
  }
  if (pressed || listenRequested || serialMic || typed.length()) {
    listenRequested = false;
    if (WiFi.status() == WL_CONNECTED) converse(typed); else audio.error();
  }

  static uint32_t lastTick = 0;
  if (millis() - lastTick >= 1000) {
    lastTick = millis();
    Event ev = agenda.tick();
    if (ev.kind != EV_NONE) handleEvent(ev);
  }

  if (agenda.dirty && (int32_t)(nextSyncAt - millis()) > 3000) nextSyncAt = millis() + 3000;
  if (WiFi.status() == WL_CONNECTED && (int32_t)(millis() - nextSyncAt) >= 0) doSync();

  if (restartRequested) { audio.ok(); delay(300); ESP.restart(); }
  delay(5);
}
