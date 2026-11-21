/*
 * ============================================================================
 * J.A.R.V.I.S. — ESP-WROOM-32 THIN CLIENT
 * ============================================================================
 * O ESP32 e so "boca e ouvido": captura a voz, envia ao servidor, toca a resposta.
 * Todo o resto (Groq Whisper, Gemini + ferramentas, Edge-TTS, Spotify, YouVersion,
 * SmartLife, alarmes, lembretes, rotinas) roda em external_server/jarvis_bridge_server.py
 * — no seu PC ou na nuvem (Fly.io).
 *
 * Conversa por voz = 1 requisicao: POST /api/voice (audio em chunks enquanto voce fala)
 *                    -> resposta em PCM 16 kHz tocada enquanto chega.
 * Comandos do painel = long-poll GET /api/device/poll (funciona atras do roteador).
 * Sem o servidor: botao toca som de erro e os alarmes espelhados na NVS ainda tocam.
 * Atualizacao do firmware pelo WiFi: ArduinoOTA (host "jarvis", senha OTA_PASSWORD).
 * ============================================================================
 */
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#include "config.h"
#include "audio_io.h"
#include "server_link.h"

AudioIO audio;
ServerLink link;
WebServer web(80);          // so para a rede local (push direto e pagina de status)
Preferences prefs;

// ---------------------------------------------------------------- fila de comandos
struct Cmd { String type, key, mode; int v = -1; };
static const int QMAX = 8;
Cmd queueBuf[QMAX];
int qHead = 0, qCount = 0;
bool listenRequested = false;

void enqueue(const String& type, const String& key, const String& mode, int v) {
  if (type == "listen") { listenRequested = true; return; }
  if (qCount >= QMAX || type.length() == 0) return;
  Cmd& c = queueBuf[(qHead + qCount) % QMAX];
  c.type = type; c.key = key; c.mode = mode; c.v = v;
  qCount++;
}

// ---------------------------------------------------------------- alarmes offline (espelho da NVS)
struct MiniAlarm { uint8_t h, m, r; };
MiniAlarm alarms[12];
int alarmCount = 0;
String alarmsRev = "";
int lastAlarmFiredMinute = -1;
uint32_t lastServerOk = 0;

void loadAlarms() {
  alarmsRev = prefs.getString("al_rev", "");
  String raw = prefs.getString("al", "");
  alarmCount = 0;
  for (int i = 0; i + 2 < (int)raw.length() && alarmCount < 12; i += 3) {
    alarms[alarmCount++] = {(uint8_t)raw[i], (uint8_t)raw[i + 1], (uint8_t)raw[i + 2]};
  }
}

void saveAlarms(JsonArray arr, const String& rev) {
  String raw;
  alarmCount = 0;
  for (JsonArray a : arr) {
    if (alarmCount >= 12) break;
    alarms[alarmCount] = {(uint8_t)(int)a[0], (uint8_t)(int)a[1], (uint8_t)(int)a[2]};
    raw += (char)alarms[alarmCount].h; raw += (char)alarms[alarmCount].m; raw += (char)alarms[alarmCount].r;
    alarmCount++;
  }
  alarmsRev = rev;
  prefs.putString("al", raw);
  prefs.putString("al_rev", rev);
  Serial.printf("⏰ Espelho de alarmes atualizado (%d)\n", alarmCount);
}

// So toca sozinho se o servidor estiver fora ha mais de 90 s (senao quem toca e o servidor, com voz)
void checkOfflineAlarms() {
  static uint32_t lastCheck = 0;
  if (millis() - lastCheck < 2000) return;
  lastCheck = millis();
  if (millis() - lastServerOk < 90000 || alarmCount == 0) return;
  struct tm t;
  if (!getLocalTime(&t, 10)) return;
  int minuteOfDay = t.tm_hour * 60 + t.tm_min;
  if (minuteOfDay == lastAlarmFiredMinute) return;
  for (int i = 0; i < alarmCount; ++i) {
    if (alarms[i].h == t.tm_hour && alarms[i].m == t.tm_min) {
      lastAlarmFiredMinute = minuteOfDay;
      Serial.println("⏰ Alarme offline (servidor indisponivel)");
      for (int k = 0; k < 10 && !AudioIO::buttonPressed(); ++k) { audio.alarm(); delay(400); }
      return;
    }
  }
}

// ---------------------------------------------------------------- conversa por voz
// typed != "" -> envia texto (Monitor Serial) em vez do microfone.
void converse(String typed = "") {
  for (int turn = 0; turn < 3; ++turn) {
    WiFiClient* c = link.connect();
    if (!c) { Serial.println("❌ Servidor indisponivel"); audio.error(); return; }
    HttpHead h;
    if (typed.length() > 0) {
      link.writeHead(*c, "POST", "/api/voice/text",
                     (String("Content-Type: text/plain; charset=utf-8\r\nContent-Length: ") + typed.length() + "\r\n").c_str());
      c->print(typed);
    } else {
      link.writeHead(*c, "POST", "/api/voice", "Content-Type: audio/L16; rate=16000; channels=1\r\nTransfer-Encoding: chunked\r\n");
      audio.chime();
      Serial.println("🎙️ Ouvindo...");
      bool speech = audio.captureChunked(*c);
      c->print("0\r\n\r\n");
      audio.blip();
      if (!speech) Serial.println("… nenhuma voz detectada");
    }
    const uint32_t t0 = millis();
    if (!ServerLink::readHead(*c, h, 45000)) { audio.error(); link.close(); return; }
    lastServerOk = millis();
    Serial.printf("⚡ Resposta HTTP %d em %u ms\n", h.status, (unsigned)(millis() - t0));
    if (h.status == 200) audio.playStream(*c, h.length);
    else if (h.status == 204) audio.noSpeech();
    else audio.error();
    link.close();
    if (!h.listenAgain) return;
    typed = "";                     // o Jarvis fez uma pergunta: ouve de novo
  }
}

// Toca um audio guardado no servidor (/api/tts?key=...): avisos, lembretes, alarmes, painel.
void playKey(const String& key) {
  WiFiClient* c = link.connect();
  if (!c) { audio.error(); return; }
  link.writeHead(*c, "GET", "/api/tts?key=" + key);
  HttpHead h;
  if (ServerLink::readHead(*c, h, 45000) && h.status == 200) audio.playStream(*c, h.length);
  link.close();
}

void runCommand(const Cmd& c) {
  Serial.printf("🛰️ Comando: %s\n", c.type.c_str());
  if (c.type == "play" && c.key.length()) playKey(c.key);
  else if (c.type == "tone") audio.playMode(c.mode);
  else if (c.type == "volume" && c.v >= 0) { audio.setVolume(c.v); prefs.putInt("vol", audio.volume); audio.ok(); }
  else if (c.type == "restart") { audio.ok(); delay(200); ESP.restart(); }
}

// ---------------------------------------------------------------- long-poll (comandos do painel)
enum PollState { POLL_IDLE, POLL_WAITING };
PollState pollState = POLL_IDLE;
uint32_t pollStarted = 0, nextPollAt = 0;
int pollFails = 0;
bool busy = false;

void pollTick() {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClient* c = link.current();
  if (pollState == POLL_IDLE) {
    if ((int32_t)(millis() - nextPollAt) < 0) return;
    c = link.connect();
    if (!c) {
      pollFails++;
      nextPollAt = millis() + (pollFails > 3 ? 60000UL : 5000UL);
      return;
    }
    char q[200];
    snprintf(q, sizeof(q), "/api/device/poll?wait=25&heap=%u&min_heap=%u&rssi=%d&uptime=%lu&volume=%d&busy=%d&fw=%s&ip=%s&alarms_rev=%s",
             (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMinFreeHeap() / 1024), WiFi.RSSI(),
             (unsigned long)(millis() / 1000), audio.volume, busy ? 1 : 0, JARVIS_FW_VERSION,
             WiFi.localIP().toString().c_str(), alarmsRev.c_str());
    link.writeHead(*c, "GET", q);
    pollState = POLL_WAITING;
    pollStarted = millis();
    return;
  }
  // POLL_WAITING: nao bloqueia o loop — so le quando a resposta chegar
  if (c->available()) {
    HttpHead h;
    String body;
    if (ServerLink::readHead(*c, h, 5000) && h.status == 200) body = ServerLink::readBody(*c, h.length);
    link.close();
    pollState = POLL_IDLE;
    if (h.status == 200) {
      pollFails = 0;
      lastServerOk = millis();
      nextPollAt = millis() + 50;
      DynamicJsonDocument doc(3072);
      if (!deserializeJson(doc, body)) {
        for (JsonObject o : doc["commands"].as<JsonArray>()) {
          enqueue(o["type"] | "", o["key"] | "", o["mode"] | "", o["v"] | -1);
        }
        if (doc.containsKey("alarms")) saveAlarms(doc["alarms"].as<JsonArray>(), doc["alarms_rev"] | "");
      }
    } else {
      pollFails++;
      nextPollAt = millis() + (h.status == 401 ? 300000UL : 5000UL);   // 401 = DEVICE_TOKEN errado
      if (h.status == 401) Serial.println("❌ Token do dispositivo recusado (JARVIS_DEVICE_TOKEN)");
    }
  } else if (!c->connected() || millis() - pollStarted > 40000) {
    link.close();
    pollState = POLL_IDLE;
    nextPollAt = millis() + 2000;
  }
}

void stopPoll() {
  if (pollState == POLL_WAITING) { link.close(); pollState = POLL_IDLE; }
  nextPollAt = millis() + 100;
}

// ---------------------------------------------------------------- setup / loop
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME_MDNS);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("📶 WiFi");
  for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; ++i) { delay(250); Serial.print('.'); }
  Serial.printf("\n📶 %s  IP %s\n", WiFi.status() == WL_CONNECTED ? "conectado" : "FALHOU", WiFi.localIP().toString().c_str());
}

void setupWeb() {
  web.on("/", HTTP_GET, []() {
    String h = String("<html><meta charset=utf-8><body style='font-family:sans-serif;background:#060a12;color:#e3eefc'>"
                      "<h2>J.A.R.V.I.S. ESP32 ") + JARVIS_FW_VERSION + "</h2><p>RAM livre: " + (ESP.getFreeHeap() / 1024) +
               " KB · WiFi " + WiFi.RSSI() + " dBm · volume " + audio.volume + "%</p><p>Painel completo: <a style='color:#34e4ff' href='" +
               JARVIS_SERVER_URL + "'>" + JARVIS_SERVER_URL + "</a></p></body></html>";
    web.send(200, "text/html", h);
  });
  web.on("/api/queue", HTTP_GET, []() {       // push direto do servidor na rede local (opcional)
    String ip = web.client().remoteIP().toString();
    bool lan = ip.startsWith("192.168.") || ip.startsWith("10.") || ip.startsWith("172.");
    if (strlen(JARVIS_DEVICE_TOKEN) > 0 && !lan && web.arg("token") != JARVIS_DEVICE_TOKEN) {
      web.send(401, "application/json", "{\"ok\":false}");
      return;
    }
    enqueue(web.arg("type"), web.arg("key"), web.arg("mode"), web.hasArg("v") ? web.arg("v").toInt() : -1);
    web.send(200, "application/json", "{\"ok\":true}");
  });
  web.begin();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_WAKE_BUTTON, INPUT_PULLUP);
  pinMode(PIN_EXTERNAL_BTN, INPUT_PULLUP);
  pinMode(PIN_STATUS_LED, OUTPUT);
  prefs.begin("jarvis", false);
  audio.begin();
  audio.setVolume(prefs.getInt("vol", DEFAULT_VOLUME_PCT));
  audio.chime();
  loadAlarms();
  connectWiFi();
  configTzTime(NTP_TZ, "pool.ntp.org", "a.st1.ntp.br");
  link.begin();
  MDNS.begin(HOSTNAME_MDNS);
  ArduinoOTA.setHostname(HOSTNAME_MDNS);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();
  setupWeb();
  Serial.printf("✅ J.A.R.V.I.S. thin client %s | servidor %s | RAM livre %u KB\n",
                JARVIS_FW_VERSION, JARVIS_SERVER_URL, (unsigned)(ESP.getFreeHeap() / 1024));
  Serial.println("👉 Botao BOOT/GPIO4 para falar | ENTER no Serial = microfone | texto + ENTER = pergunta digitada");
}

void loop() {
  ArduinoOTA.handle();
  web.handleClient();

  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t lastTry = 0;
    if (millis() - lastTry > 10000) { lastTry = millis(); WiFi.reconnect(); }
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

  if (pressed || listenRequested || serialMic || typed.length() > 0) {
    listenRequested = false;
    busy = true;
    stopPoll();                     // uma conexao (TLS) por vez
    converse(typed);
    busy = false;
    while (AudioIO::buttonPressed()) delay(10);
  } else if (qCount > 0) {
    Cmd c = queueBuf[qHead];
    qHead = (qHead + 1) % QMAX;
    qCount--;
    busy = true;
    stopPoll();
    runCommand(c);
    busy = false;
  } else {
    pollTick();
  }

  checkOfflineAlarms();
  delay(5);
}
