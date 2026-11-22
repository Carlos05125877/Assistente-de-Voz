/*
 * ============================================================================
 * J.A.R.V.I.S. — ESP32-S3 (N16R8) + Netlify, 100% gratis
 * ============================================================================
 * Diga "Jarvis" e fale (sem apertar nada):
 *   WakeNet (offline) -> fala limpa pelo AFE -> Groq Whisper (enquanto fala)
 *   -> Netlify /api/turn (Gemini + ferramentas) -> MP3 neural tocado enquanto chega.
 * "Jarvis" tambem interrompe o Jarvis enquanto ele fala (cancelamento de eco).
 * Alarmes, lembretes, timers e rotinas com horario rodam aqui, mesmo sem internet.
 * Botao: toque = falar/interromper | segurar 2 s = microfone mudo (privacidade).
 *
 * Arduino IDE (placa "ESP32S3 Dev Module"):
 *   USB CDC On Boot: Enabled | Flash Size: 16MB | PSRAM: OPI PSRAM
 *   Partition Scheme: "ESP SR 16M (3MB APP/6MB SPIFFS/3.9MB MODEL)"
 *   Bibliotecas: ArduinoJson 6.21.x e arduino-libhelix (ZIP do GitHub). Veja o README.
 * ============================================================================
 */
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <sys/time.h>
// Nao usamos a classe ESP_SR, mas incluir o cabecalho faz a Arduino IDE gravar o arquivo de
// modelos (srmodels.bin) na particao "model" junto com o sketch. Sem isso nao ha palavra de ativacao.
#include "ESP_SR.h"
#include <time.h>
#include "config.h"
#include "speaker.h"
#include "listener.h"
#include "net_link.h"
#include "agenda.h"

Speaker speaker;
Listener mic;
Netlify net;
Groq groq;
Agenda agenda;
Preferences prefs;

uint32_t nextSyncAt = 4000, lastSyncOk = 0;
int syncFails = 0;
bool hot = false, busy = false, restartRequested = false, followUp = true, sensitiveWake = false;
volatile bool buttonInterrupt = false;

// ---------------------------------------------------------------- LED de status
enum Led { LED_IDLE, LED_LISTEN, LED_THINK, LED_SPEAK, LED_ERROR, LED_MUTED, LED_OFFLINE };
void led(Led s) {
  static const uint8_t C[][3] = {{0, 0, 2}, {0, 40, 60}, {40, 0, 60}, {0, 50, 10}, {60, 0, 0}, {20, 4, 0}, {30, 20, 0}};
  rgbLedWrite(PIN_RGB_LED, C[s][0], C[s][1], C[s][2]);
}
void ledIdle() { led(mic.micMuted ? LED_MUTED : (WiFi.status() == WL_CONNECTED ? LED_IDLE : LED_OFFLINE)); }

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
  st["vol"] = speaker.volume;
  st["heap"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
  st["minh"] = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024;
  st["psram"] = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
  st["rssi"] = WiFi.RSSI();
  st["up"] = millis() / 1000;
  st["fw"] = JARVIS_FW_VERSION;
  st["ip"] = WiFi.localIP().toString();
  st["busy"] = busy;
  st["wake"] = mic.isReady() && mic.wakeEnabled;
  st["muted"] = (bool)mic.micMuted;
  st["wakes"] = (uint32_t)mic.wakeCount;
}

// o player para a fala se o botao for tocado ou se alguem disser "Jarvis"
bool speechInterrupt() { return buttonInterrupt || mic.wakeDetected || mic.capturing(); }

// ---------------------------------------------------------------- acoes vindas do Netlify
void sayParts(const String& id, int n);

void setMuted(bool m) {
  mic.micMuted = m;
  prefs.putBool("muted", m);
  if (m) speaker.muted(); else speaker.ok();
  agenda.dirty = true;
  ledIdle();
}

void runAction(JsonObject a) {
  if (agenda.apply(a)) { speaker.ok(); return; }
  const char* t = a["t"] | "";
  if (!strcmp(t, "vol")) { speaker.setVolume(a["v"] | DEFAULT_VOLUME_PCT); prefs.putInt("vol", speaker.volume); speaker.ok(); }
  else if (!strcmp(t, "tone")) speaker.playMode(a["m"] | "chime");
  else if (!strcmp(t, "say")) sayParts(a["id"] | "", a["n"] | 1);
  else if (!strcmp(t, "listen")) mic.startCapture();
  else if (!strcmp(t, "mic")) setMuted(!(a["on"] | true));
  else if (!strcmp(t, "restart")) restartRequested = true;
}

/** Acoes de um turno (cabecalho X-Actions), aplicadas antes de tocar a resposta. */
void runActionsHeader(const String& b64) {
  if (b64.length() < 3) return;
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, b64urlDecode(b64))) return;
  for (JsonObject a : doc.as<JsonArray>()) {
    const char* t = a["t"] | "";
    if (!strcmp(t, "say")) continue;
    if (agenda.apply(a)) continue;                  // a confirmacao vem falada
    runAction(a);
  }
}

// ---------------------------------------------------------------- fala
bool playStoredParts(const String& id, int from, int n) {
  for (int p = from; p < n; ++p) {
    if (!net.request("GET", "/api/say?id=" + id + "&p=" + p)) return false;
    Head h;
    if (!net.readHead(h, 30000) || h.status != 200) { net.finish(); return false; }
    if (!net.playBody(speaker)) return false;
  }
  return true;
}
void sayParts(const String& id, int n) {
  if (!id.length()) return;
  led(LED_SPEAK);
  speaker.startSpeech();
  if (playStoredParts(id, 0, n)) speaker.drain(); else speaker.flush();
  ledIdle();
}
bool speakText(const String& text) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!net.request("GET", "/api/say?text=" + urlEncode(text))) return false;
  Head h;
  if (!net.readHead(h, 20000) || h.status != 200) { net.finish(); return false; }
  led(LED_SPEAK);
  speaker.startSpeech();
  if (!(net.playBody(speaker) && speaker.drain())) speaker.flush();
  ledIdle();
  return true;
}

/** Um turno no Netlify. true = o Jarvis fez uma pergunta (ouvir de novo). */
bool turn(const String& text) {
  led(LED_THINK);
  DynamicJsonDocument doc(6144);
  doc["text"] = text;
  fillState(doc.createNestedObject("st"));
  String body;
  serializeJson(doc, body);
  doc.clear();
  const uint32_t t0 = millis();
  if (!net.request("POST", "/api/turn", "application/json", &body)) { speaker.error(); return false; }
  Head h;
  if (!net.readHead(h, 45000)) { Serial.println("❌ Netlify não respondeu"); speaker.error(); return false; }
  Serial.printf("⚡ /api/turn HTTP %d em %u ms (%d partes)\n", h.status, (unsigned)(millis() - t0), h.parts);
  lastSyncOk = millis();
  runActionsHeader(h.actions);
  if (h.status == 401) { Serial.println("❌ DEVICE_TOKEN recusado pelo Netlify"); speaker.error(); net.finish(); return false; }
  if (h.status == 204) { net.finish(); return false; }
  if (h.status != 200) { net.finish(); speaker.error(); return false; }
  led(LED_SPEAK);
  speaker.startSpeech();
  bool ok = net.playBody(speaker) && playStoredParts(h.textId, 1, h.parts);
  if (ok) ok = speaker.drain(); else speaker.flush();
  return ok && h.listen;
}

/** Conversa: o microfone ja esta gravando (palavra "Jarvis", botao ou continuacao). */
void converse(const String& typed = "") {
  busy = true;
  String text = typed;
  for (int i = 0; i < 4; ++i) {
    if (!text.length()) {
      led(LED_LISTEN);
      groq.warm();                                   // abre o TLS enquanto voce fala (a fala fica no anel)
      const uint32_t t0 = millis();
      text = groq.transcribe(mic, agenda.groqKey, agenda.sttModel);
      if (text == "#erro") { led(LED_ERROR); speaker.error(); break; }
      if (!text.length()) { speaker.noSpeech(); break; }
      Serial.printf("📝 Você (%u ms): %s\n", (unsigned)(millis() - t0), text.c_str());
    }
    buttonInterrupt = false;
    mic.wakeDetected = false;
    bool ask = turn(text);
    text = "";
    if (mic.capturing()) {                          // "Jarvis" (ou o botao) no meio da fala
      mic.wakeDetected = false;
      buttonInterrupt = false;
      speaker.flush();
      speaker.wake();
      continue;
    }
    if (!ask || !followUp) break;
    mic.startCapture();                              // o Jarvis perguntou: responde sem dizer "Jarvis"
    speaker.blip();
  }
  buttonInterrupt = false;
  busy = false;
  ledIdle();
}

// ---------------------------------------------------------------- eventos da agenda local
void handleEvent(const Event& ev) {
  busy = true;
  if (ev.kind == EV_ALARM) {
    Serial.printf("⏰ Alarme: %s\n", ev.text);
    led(LED_SPEAK);
    const uint32_t t0 = millis();
    buttonInterrupt = false;
    mic.wakeDetected = false;
    // toca ate 60 s; para com o botao OU dizendo "Jarvis"
    while (millis() - t0 < 60000 && !buttonInterrupt && !mic.wakeDetected) { speaker.alarm(); delay(350); }
    bool byVoice = mic.wakeDetected;
    if (byVoice) mic.cancelCapture();
    mic.wakeDetected = false;
    buttonInterrupt = false;
    if (ev.briefing) turn("__briefing__");           // devocional/briefing logo depois de acordar
  } else if (ev.kind == EV_REMINDER) {
    Serial.printf("🔔 Lembrete: %s\n", ev.text);
    speaker.bell();
    if (!speakText(String("Lembrete: ") + ev.text + ".")) { delay(400); speaker.bell(); }
  } else if (ev.kind == EV_TIMER) {
    Serial.printf("⏲️ Timer: %s\n", ev.text);
    speaker.alarm();
    if (!speakText(String("O timer ") + ev.text + " terminou.")) speaker.alarm();
  } else if (ev.kind == EV_ROUTINE) {
    Serial.printf("▶️ Rotina agendada: %s\n", ev.text);
    turn(String("__routine:") + ev.text);
  }
  busy = false;
  ledIdle();
}

// ---------------------------------------------------------------- sync com o Netlify
void applyVoiceConfig(JsonObject cfg) {
  if (cfg.containsKey("we")) { mic.wakeEnabled = cfg["we"].as<bool>(); prefs.putBool("we", mic.wakeEnabled); }
  if (cfg.containsKey("bi")) { mic.bargeIn = cfg["bi"].as<bool>(); prefs.putBool("bi", mic.bargeIn); }
  if (cfg.containsKey("fu")) { followUp = cfg["fu"].as<bool>(); prefs.putBool("fu", followUp); }
  if (cfg.containsKey("ws") && cfg["ws"].as<bool>() != sensitiveWake) {
    prefs.putBool("ws", cfg["ws"].as<bool>());       // sensibilidade do WakeNet: vale no proximo boot
    restartRequested = true;
  }
}

void doSync() {
  DynamicJsonDocument doc(6144);
  fillState(doc.createNestedObject("st"));
  String body;
  serializeJson(doc, body);
  doc.clear();
  Head h;
  String resp;
  bool ok = net.request("POST", "/api/sync", "application/json", &body) && net.readHead(h, 20000);
  if (ok) resp = net.readBody(8192);
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
  DynamicJsonDocument r(8192);
  if (deserializeJson(r, resp)) { nextSyncAt = millis() + 60000; return; }
  hot = r["hot"] | false;
  nextSyncAt = millis() + (uint32_t)(r["next"] | SYNC_IDLE_S) * 1000UL;
  if (r.containsKey("cfg")) { agenda.applyConfig(r["cfg"].as<JsonObject>()); applyVoiceConfig(r["cfg"].as<JsonObject>()); }
  if (!agenda.timeValid() && r.containsKey("t")) {
    struct timeval tv = {(time_t)(r["t"].as<long>()), 0};
    settimeofday(&tv, nullptr);
  }
  JsonArray q = r["q"].as<JsonArray>();
  if (q.size()) {
    busy = true;
    for (JsonObject a : q) { Serial.printf("🛰️ Painel: %s\n", (const char*)(a["t"] | "?")); runAction(a); }
    busy = false;
    nextSyncAt = millis() + 1500;
  }
}

// ---------------------------------------------------------------- botao (toque / segurar)
void pollButton() {
  static bool down = false, longDone = false;
  static uint32_t since = 0;
  bool now = Speaker::buttonPressed();
  if (now && !down) { down = true; since = millis(); longDone = false; }
  if (now && down && !longDone && millis() - since > 2000) { longDone = true; setMuted(!mic.micMuted); }
  if (!now && down) {
    down = false;
    if (!longDone && millis() - since > 40) {
      if (speaker.isSpeaking()) buttonInterrupt = true;   // interrompe e ja escuta
      mic.startCapture();
      speaker.wake();
    }
  }
}

// ---------------------------------------------------------------- setup / loop
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME_MDNS);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("📶 WiFi");
  for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; ++i) { delay(250); Serial.print('.'); }
  Serial.printf("\n📶 %s  IP %s\n", WiFi.status() == WL_CONNECTED ? "conectado" : "FALHOU (seguindo offline)", WiFi.localIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_WAKE_BUTTON, INPUT_PULLUP);
  pinMode(PIN_EXTERNAL_BTN, INPUT_PULLUP);
  led(LED_THINK);
  prefs.begin("jarvis", false);
  speaker.begin();
  speaker.setVolume(prefs.getInt("vol", DEFAULT_VOLUME_PCT));
  speaker.interrupt = speechInterrupt;
  agenda.begin(&prefs);
  mic.wakeEnabled = prefs.getBool("we", true);
  mic.bargeIn = prefs.getBool("bi", true);
  mic.micMuted = prefs.getBool("muted", false);
  followUp = prefs.getBool("fu", true);
  sensitiveWake = prefs.getBool("ws", false);
  if (!mic.begin(&speaker, sensitiveWake)) Serial.println("⚠️ Sem palavra de ativação: use o botão");
  connectWiFi();
  configTzTime(NTP_TZ, "pool.ntp.org", "a.st1.ntp.br");
  net.begin();
  groq.begin();
  MDNS.begin(HOSTNAME_MDNS);
  ArduinoOTA.setHostname(HOSTNAME_MDNS);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();
  speaker.chime();
  ledIdle();
  Serial.printf("✅ J.A.R.V.I.S. %s | %s | RAM %u KB + PSRAM %u KB livres\n", JARVIS_FW_VERSION, NETLIFY_URL,
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
  Serial.println("👉 Diga \"Jarvis\" | botão: toque = falar, segurar 2 s = mudo | Serial: texto + ENTER");
}

void loop() {
  ArduinoOTA.handle();
  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t lastTry = 0;
    if (millis() - lastTry > 15000) { lastTry = millis(); WiFi.reconnect(); ledIdle(); }
  }
  pollButton();

  if (Serial.available()) {
    String typed = Serial.readStringUntil('\n');
    typed.trim();
    if (typed.length()) converse(typed); else { mic.startCapture(); speaker.wake(); }
  }

  if (mic.wakeDetected && !busy) {                  // "Jarvis"
    Serial.println("👂 Jarvis!");
    speaker.wake();
  }
  if (mic.capturing() && !busy) {
    if (WiFi.status() == WL_CONNECTED) converse();
    else { mic.cancelCapture(); speaker.error(); }
    mic.wakeDetected = false;
  }

  static uint32_t lastTick = 0;
  if (millis() - lastTick >= 1000) {
    lastTick = millis();
    Event ev = agenda.tick();
    if (ev.kind != EV_NONE) handleEvent(ev);
  }

  if (agenda.dirty && (int32_t)(nextSyncAt - millis()) > 3000) nextSyncAt = millis() + 3000;
  if (WiFi.status() == WL_CONNECTED && !busy && (int32_t)(millis() - nextSyncAt) >= 0) doSync();

  if (restartRequested && !busy) { speaker.ok(); delay(300); ESP.restart(); }
  delay(5);
}
