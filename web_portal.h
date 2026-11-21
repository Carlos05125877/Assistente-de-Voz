#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include "config_private.h"
#include "audio_manager.h"
#include "spotify_client.h"
#include "smartlife_tuya.h"
#include "bible_client.h"
#include "jarvis_features.h"
#include "gemini_agent.h"
#include "bridge_link.h"

// ============================================================================
// PAINEL WEB HUD DO J.A.R.V.I.S. (SERVIDOR HTTP EMBARCADO NO ESP-WROOM-32)
// ============================================================================
// Acesse via navegador: http://jarvis.local ou http://<IP_DO_ESP32>
// - Envie comandos ao Gemini (com resposta por voz nos 2 alto-falantes!)
// - Acione a escuta pelo microfone remotamente
// - Controle o Spotify (Busca, Play, Pause, Next, Volume)
// - Conecte com o App Biblia (Versiculo do Dia, Busca de Passagens, Salmos)
// - Gerencie Alarmes Persistentes na NVS (HH:MM) e Timers
// - Controle dispositivos SmartLife e teste os 2 Alto-Falantes Estereo (L + R)
// ============================================================================

static const char JARVIS_HUD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>J.A.R.V.I.S. ESP32 HUD</title>
  <style>
    :root { --cyan: #00e5ff; --gold: #ffd166; --bg: #070b12; --card: #0e1726; --text: #e2f1ff; }
    * { box-sizing: border-box; font-family: 'Segoe UI', system-ui, sans-serif; }
    body { margin: 0; background: var(--bg); color: var(--text); padding: 18px; }
    .header { display: flex; align-items: center; justify-content: space-between; border-bottom: 1px solid #1a2c47; padding-bottom: 12px; margin-bottom: 18px; }
    .title { font-size: 1.35rem; font-weight: 700; color: var(--cyan); letter-spacing: 2px; text-shadow: 0 0 12px rgba(0,229,255,0.4); }
    .badge { background: rgba(0,229,255,0.12); border: 1px solid var(--cyan); color: var(--cyan); padding: 4px 12px; border-radius: 99px; font-size: 0.8rem; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 16px; }
    .card { background: var(--card); border: 1px solid #1b2e4b; border-radius: 12px; padding: 16px; box-shadow: 0 6px 20px rgba(0,0,0,0.35); }
    .card h3 { margin-top: 0; color: var(--cyan); font-size: 0.98rem; letter-spacing: 1px; text-transform: uppercase; }
    .row { display: flex; gap: 8px; flex-wrap: wrap; margin-top: 10px; }
    input[type=text], input[type=number], input[type=time] { flex: 1; background: #070d17; border: 1px solid #243e63; color: #fff; padding: 9px 10px; border-radius: 8px; outline: none; }
    button { background: rgba(0,229,255,0.15); border: 1px solid var(--cyan); color: var(--text); padding: 8px 13px; border-radius: 8px; cursor: pointer; font-weight: 600; transition: 0.15s; }
    button:hover { background: var(--cyan); color: #04101c; }
    .log-box { background: #05080e; border: 1px solid #182942; border-radius: 8px; padding: 11px; min-height: 75px; font-size: 0.9rem; line-height: 1.45; margin-top: 10px; white-space: pre-wrap; }
    .stat { font-size: 0.85rem; color: #9ab8d9; margin: 4px 0; }
  </style>
</head>
<body>
  <div class="header">
    <div class="title">J.A.R.V.I.S. // SISTEMA COMPLETO ESP32</div>
    <div style="display:flex;gap:8px;align-items:center">
      <a class="badge" id="bridgeLink" href="#" target="_blank" style="text-decoration:none;display:none">🖥️ Painel completo (PC)</a>
      <div class="badge" id="statusBadge">ONLINE</div>
    </div>
  </div>

  <div class="grid">
    <!-- 1. COMANDO DE VOZ & GEMINI IA -->
    <div class="card">
      <h3>🎙️ Comando de Voz & Gemini IA</h3>
      <div class="row">
        <input type="text" id="cmdInput" placeholder="Ex: Toca Coldplay / Leia Salmos 23 / Alarme 07:30..." />
        <button onclick="sendCommand()">Enviar</button>
      </div>
      <div class="row">
        <button onclick="triggerMic()">🎙️ Ouvir Microfone do ESP32</button>
        <button onclick="queueCmd('briefing')">☀️ Briefing do Dia</button>
      </div>
      <div class="log-box" id="replyBox">Aguardando ordens, senhor...</div>
    </div>

    <!-- 2. SPOTIFY & MUSICAS -->
    <div class="card">
      <h3>🎵 Spotify & Player Musical</h3>
      <div class="row">
        <button onclick="spotifyAct('previous')">⏮️ Ant.</button>
        <button onclick="spotifyAct('resume')">▶️ Play</button>
        <button onclick="spotifyAct('pause')">⏸️ Pause</button>
        <button onclick="spotifyAct('next')">⏭️ Prox.</button>
        <button onclick="spotifyAct('now_playing')">ℹ️ Atual</button>
      </div>
      <div class="row">
        <input type="text" id="spQuery" placeholder="Musica, Artista ou Playlist no Spotify..." />
        <button onclick="spotifyPlayQuery()">Tocar</button>
      </div>
      <div class="stat" id="spNow">Consulte ou inicie uma musica acima.</div>
    </div>

    <!-- 3. APP DA BIBLIA OFICIAL (YOUVERSION / HOLY BIBLE — bible.com/pt/reading-plans) -->
    <div class="card">
      <h3>📖 App Biblia (Holy Bible / YouVersion)</h3>
      <div class="stat" id="biblePlanStatus">Plano Ativo: Carregando...</div>
      <div class="row">
        <button onclick="bibleAction('read_plan_day','',1)">🔊 Devocional de Hoje</button>
        <button onclick="bibleAction('next_plan_day','',1)">⏭️ Proximo Dia</button>
        <button onclick="bibleAction('verse_of_day','',1)">✨ Versiculo do Dia</button>
      </div>
      <div class="row">
        <button onclick="bibleAction('list_plans','',1)">📋 Meus Planos</button>
        <button onclick="bibleAction('sync','',1)">🔄 Sincronizar Conta</button>
        <input type="text" id="planPick" placeholder="Ativar plano (nome ou numero)" />
        <button onclick="bibleStartPlan(document.getElementById('planPick').value)">Ativar</button>
      </div>
      <div class="row">
        <input type="text" id="bibleRef" placeholder="Ex: Salmos 23:1-4 ou URL bible.com/pt/reading-plans/..." />
        <button onclick="fetchBible(false)">Ler</button>
        <button onclick="fetchBible(true)">🔊 Ouvir</button>
      </div>
      <div class="log-box" id="bibleBox">Conectado a bible.com/pt/reading-plans.</div>
    </div>

    <!-- 4. ALARMES PERSISTENTES (NVS) & TIMERS -->
    <div class="card">
      <h3>⏰ Guardar Alarmes & Despertador (NVS)</h3>
      <div class="row">
        <input type="time" id="alarmTime" value="07:00" />
        <input type="text" id="alarmLabel" placeholder="Rotulo (ex: Acordar / Remedio)" />
        <button onclick="saveAlarm()">Salvar</button>
      </div>
      <div class="row">
        <button onclick="sendQuick('Coloque um timer de 5 minutos')">⏲️ Timer 5m</button>
        <button onclick="clearAllAlarms()">🗑️ Limpar Alarmes</button>
      </div>
      <div class="log-box" id="alarmsBox">Carregando alarmes salvos...</div>
    </div>

    <!-- 5. 2 ALTO-FALANTES ESTEREO (L + R) -->
    <div class="card">
      <h3>🔊 2 Alto-Falantes Estereo (L + R)</h3>
      <div class="stat">Volume Atual: <span id="volLabel">80</span>%</div>
      <div class="row">
        <input type="range" id="volRange" min="0" max="100" value="80" style="flex:1" onchange="setVol(this.value)" />
      </div>
      <div class="row">
        <button onclick="testSpeaker('left')">🔈 Esquerdo (L)</button>
        <button onclick="testSpeaker('right')">🔊 Direito (R)</button>
        <button onclick="testSpeaker('chime')">✨ Chime</button>
        <button onclick="testSpeaker('alarm')">🔔 Testar Alarme</button>
      </div>
    </div>

    <!-- 6. SMARTLIFE / TUYA & TELEMETRIA -->
    <div class="card">
      <h3>🏠 SmartLife / Tuya & Telemetria</h3>
      <div class="row">
        <button onclick="sendQuick('Ligue a luz da sala')">💡 Sala ON</button>
        <button onclick="sendQuick('Desligue a luz da sala')">🌑 Sala OFF</button>
        <button onclick="sendQuick('Coloque a fita led em azul')">🔵 Fita Azul</button>
        <button onclick="sendQuick('Ligue o ar condicionado')">❄️ Ar ON</button>
      </div>
      <div class="log-box" id="telemetryBox">Carregando telemetria...</div>
    </div>
  </div>

  <script>
    async function refreshStatus() {
      try {
        const r = await fetch('/api/status');
        const d = await r.json();
        document.getElementById('volLabel').innerText = d.volume;
        document.getElementById('volRange').value = d.volume;
        document.getElementById('spNow').innerText = 'Spotify: ' + d.spotifyTrack;
        document.getElementById('alarmsBox').innerText =
          `Alarmes Salvos (NVS): ${d.alarms}\nTimers Ativos: ${d.timers}`;
        if (d.biblePlan) {
          document.getElementById('biblePlanStatus').innerText = 'Plano YouVersion: ' + d.biblePlan;
        }
        document.getElementById('bibleBox').innerText = `${d.bibleRef}\n${d.bibleText}`;
        document.getElementById('telemetryBox').innerText =
          `Horario: ${d.time} | Uptime: ${Math.floor(d.uptime/3600)}h ${Math.floor(d.uptime%3600/60)}m\nIP: ${d.ip} | Firmware ${d.fw}\nRAM Livre: ${d.freeHeapKb} KB | WiFi: ${d.rssi} dBm\nNotas: ${d.notes}`;
        document.getElementById('statusBadge').innerText = d.busy ? 'FALANDO...' : 'ONLINE';
        const bl = document.getElementById('bridgeLink');
        if (d.bridge && d.bridge.length > 8) { bl.href = d.bridge; bl.style.display = 'inline-block'; bl.style.opacity = d.bridgeOnline ? 1 : 0.45; }
        if (d.lastReply) {
          document.getElementById('replyBox').innerText = `Voce: ${d.lastUser}\nJarvis: ${d.lastReply}`;
        }
      } catch (e) {}
    }
    // Comandos vao para a fila do ESP32 (o servidor responde na hora; a resposta chega pelo /api/status)
    let lastReplySeen = '';
    async function queueCmd(type, params = {}) {
      const qs = new URLSearchParams({type, ...params}).toString();
      const r = await fetch('/api/queue?' + qs);
      if (!r.ok) { document.getElementById('replyBox').innerText = 'Fila cheia, aguarde o Jarvis terminar.'; return false; }
      return true;
    }
    async function waitReply(boxId, field) {
      for (let i = 0; i < 40; i++) {
        await new Promise(res => setTimeout(res, 1500));
        const d = await (await fetch('/api/status')).json();
        const v = field === 'bible' ? `${d.bibleRef}\n${d.bibleText}` : d.lastReply;
        if (v && v !== lastReplySeen) { lastReplySeen = v; document.getElementById(boxId).innerText = field === 'bible' ? v : `Voce: ${d.lastUser}\nJarvis: ${d.lastReply}`; return; }
      }
    }
    async function sendCommand() {
      const q = document.getElementById('cmdInput').value.trim();
      if (!q) return;
      document.getElementById('replyBox').innerText = 'Processando com Gemini + TTS...';
      if (await queueCmd('ask', {text: q})) waitReply('replyBox', 'ask');
    }
    function sendQuick(text) {
      document.getElementById('cmdInput').value = text;
      sendCommand();
    }
    async function triggerMic() {
      document.getElementById('replyBox').innerText = '🎙️ Microfone do ESP32 ativado! Fale perto do microfone...';
      await fetch('/api/trigger_mic', {method:'POST'});
      setTimeout(refreshStatus, 5000);
    }
    async function setVol(v) {
      document.getElementById('volLabel').innerText = v;
      await fetch('/api/audio?vol=' + v);
    }
    async function testSpeaker(mode) {
      await fetch('/api/audio?test=' + mode);
    }
    async function spotifyAct(act) {
      const r = await fetch('/api/spotify?action=' + act);
      const d = await r.json();
      document.getElementById('spNow').innerText = d.result;
    }
    async function spotifyPlayQuery() {
      const q = document.getElementById('spQuery').value.trim();
      if (!q) return;
      document.getElementById('spNow').innerText = 'Buscando e iniciando no Spotify...';
      const r = await fetch('/api/spotify?action=play_query&q=' + encodeURIComponent(q));
      const d = await r.json();
      document.getElementById('spNow').innerText = d.result;
    }
    async function fetchBible(speak) {
      const ref = document.getElementById('bibleRef').value.trim() || 'Salmos 23:1-4';
      const isPlan = ref.includes('reading-plans') || ref.includes('bible.com') || /^\d+-/.test(ref);
      if (!speak) {   // so leitura na tela: resposta direta
        document.getElementById('bibleBox').innerText = 'Consultando App Biblia (YouVersion)...';
        const r = await fetch(`/api/bible?action=${isPlan ? 'start_plan' : 'get_passage'}&ref=${encodeURIComponent(ref)}&theme=${encodeURIComponent(ref)}&speak=0`);
        const d = await r.json();
        document.getElementById('bibleBox').innerText = d.result;
        return;
      }
      bibleAction(isPlan ? 'start_plan' : 'get_passage', ref, 1);
    }
    async function bibleAction(act, theme, speak) {
      document.getElementById('bibleBox').innerText = 'Conectando ao App Biblia YouVersion...';
      if (await queueCmd('bible', {action: act, text: theme || ''})) waitReply('bibleBox', 'bible');
    }
    function bibleStartPlan(planKey) {
      bibleAction('start_plan', planKey, 1);
    }
    async function saveAlarm() {
      const t = document.getElementById('alarmTime').value || '07:00';
      const parts = t.split(':');
      const h = parseInt(parts[0], 10);
      const m = parseInt(parts[1], 10);
      const lbl = document.getElementById('alarmLabel').value.trim() || 'Despertador';
      const r = await fetch(`/api/alarms?action=add&h=${h}&m=${m}&label=${encodeURIComponent(lbl)}`);
      const d = await r.json();
      document.getElementById('alarmsBox').innerText = d.result;
      setTimeout(refreshStatus, 800);
    }
    async function clearAllAlarms() {
      await fetch('/api/alarms?action=clear');
      refreshStatus();
    }
    setInterval(refreshStatus, 5000);
    refreshStatus();
  </script>
</body>
</html>
)rawliteral";

// Comando vindo do HUD (/api/queue) ou do Painel de Controle no PC (heartbeat / push direto).
// Os comandos sao enfileirados e executados no loop(), para o servidor web responder na hora
// mesmo quando o Jarvis vai falar por varios minutos.
struct PanelCmd {
  String type, text, key, label, action, mode;
  int h = -1, m = -1, v = -1, sec = 0;
  bool r = true;
};

class JarvisWebPortal {
public:
  WebServer server{80};
  bool remoteMicTriggered = false;
  bool busy = false;
  String lastUserText = "";
  String lastJarvisReply = "";

  static const int QMAX = 6;
  PanelCmd queue[QMAX];
  int qHead = 0, qCount = 0;

  AudioManager* pAudio = nullptr;
  SpotifyClient* pSpotify = nullptr;
  SmartLifeClient* pSmart = nullptr;
  BibleClient* pBible = nullptr;
  JarvisFeatures* pJarvis = nullptr;
  GeminiAgent* pGemini = nullptr;
  WiFiClientSecure* pTls = nullptr;

  uint32_t nextHeartbeatAt = 4000;
  int heartbeatFails = 0;

  bool enqueue(const PanelCmd& c) {
    if (c.type.length() == 0) return false;
    if (c.type == "mic") { remoteMicTriggered = true; return true; }   // nao precisa de fila
    if (qCount >= QMAX) return false;
    queue[(qHead + qCount) % QMAX] = c;
    qCount++;
    return true;
  }

  void enqueueFromJson(JsonObject o) {
    PanelCmd c;
    c.type   = o["type"] | "";
    c.text   = o["text"] | "";
    c.key    = o["key"] | "";
    c.label  = o["label"] | "";
    c.action = o["action"] | "";
    c.mode   = o["mode"] | "";
    c.h = o["h"] | -1;
    c.m = o["m"] | -1;
    c.v = o["v"] | -1;
    c.sec = o["sec"] | 0;
    c.r = o["r"].is<bool>() ? o["r"].as<bool>() : ((int)(o["r"] | 1) != 0);
    enqueue(c);
  }

  PanelCmd cmdFromArgs() {
    PanelCmd c;
    c.type   = server.arg("type");
    c.text   = server.arg("text");
    c.key    = server.arg("key");
    c.label  = server.arg("label");
    c.action = server.arg("action");
    c.mode   = server.arg("mode");
    if (server.hasArg("h"))   c.h = server.arg("h").toInt();
    if (server.hasArg("m"))   c.m = server.arg("m").toInt();
    if (server.hasArg("v"))   c.v = server.arg("v").toInt();
    if (server.hasArg("sec")) c.sec = server.arg("sec").toInt();
    if (server.hasArg("r"))   c.r = (server.arg("r") != "0" && server.arg("r") != "false");
    return c;
  }

  // Executa UM comando pendente por volta do loop()
  void processQueue() {
    if (qCount == 0 || pAudio == nullptr) return;
    PanelCmd c = queue[qHead];
    qHead = (qHead + 1) % QMAX;
    qCount--;
    busy = true;
    runCommand(c);
    busy = false;
    nextHeartbeatAt = millis() + 300;   // painel ve o resultado rapido
  }

  void runCommand(const PanelCmd& c) {
    AudioManager& audio = *pAudio;
    WiFiClientSecure& tls = *pTls;
    Serial.printf("🛰️ [Painel] Comando: %s\n", c.type.c_str());
    if (c.type == "speak") {
      audio.speakText(tls, c.text);
    } else if (c.type == "say_key") {
      String shortTxt = c.text.length() > 0 ? c.text : String("Lendo o texto do painel.");
      BridgeLink::armTtsKey(c.key.length() > 0 ? c.key : String("last"), shortTxt);
      audio.speakText(tls, shortTxt);
    } else if (c.type == "ask" && c.text.length() > 0) {
      String reply;
      lastUserText = c.text;
      if (!pGemini->processUserQuery(tls, c.text, reply, audio, *pSpotify, *pSmart, *pBible, *pJarvis)) {
        reply = "Desculpe senhor, ocorreu uma falha ao consultar o Gemini.";
      }
      lastJarvisReply = reply;
      audio.speakText(tls, reply);
    } else if (c.type == "volume" && c.v >= 0) {
      audio.setVolume(c.v);
      audio.playSuccessBeep();
    } else if (c.type == "alarm_add" && c.h >= 0 && c.m >= 0) {
      lastJarvisReply = pJarvis->addAlarm(c.h, c.m, c.label.length() ? c.label : String("Despertador"), c.r);
      audio.playSuccessBeep();
    } else if (c.type == "alarm_remove") {
      lastJarvisReply = pJarvis->removeAlarm(c.h, c.m, c.label);
      audio.playSuccessBeep();
    } else if (c.type == "alarm_clear") {
      lastJarvisReply = pJarvis->clearAlarms();
    } else if (c.type == "note_add" && c.text.length() > 0) {
      lastJarvisReply = pJarvis->addNote(c.text);
      audio.playSuccessBeep();
    } else if (c.type == "notes_clear") {
      lastJarvisReply = pJarvis->clearNotes();
    } else if (c.type == "timer" && c.sec > 0) {
      lastJarvisReply = pJarvis->addTimer(c.sec, c.label.length() ? c.label : String("timer"));
      audio.playSuccessBeep();
    } else if (c.type == "test") {
      if (c.mode == "left")       audio.playToneStereo(660.0f, 350, -1.0f, 0.45f);
      else if (c.mode == "right") audio.playToneStereo(880.0f, 350, +1.0f, 0.45f);
      else if (c.mode == "alarm") audio.playAlarmSound();
      else                        audio.playWakeChime();
    } else if (c.type == "briefing") {
      lastUserText = "Briefing";
      lastJarvisReply = pJarvis->morningBriefing(tls);
      audio.speakText(tls, lastJarvisReply);
    } else if (c.type == "bible") {
      String act = c.action.length() ? c.action : String("read_plan_day");
      String res = pBible->executeCommand(tls, act, c.text, c.text, 0);
      lastUserText = "Biblia: " + act;
      lastJarvisReply = res;
      audio.playSuccessBeep();
      if (res.length() > 0) audio.speakText(tls, res);
    } else if (c.type == "spotify") {
      lastJarvisReply = pSpotify->executeCommand(tls, c.action.length() ? c.action : String("now_playing"), c.text, "track", c.v);
    } else if (c.type == "restart") {
      audio.playSuccessBeep();
      delay(300);
      ESP.restart();
    }
  }

  // Heartbeat para o Painel de Controle no PC (a cada 15 s). A resposta traz comandos pendentes.
  void tickHeartbeat() {
    if (!BridgeLink::enabled() || pJarvis == nullptr || (int32_t)(millis() - nextHeartbeatAt) < 0) return;

    DynamicJsonDocument doc(3072);
    doc["ip"]       = WiFi.localIP().toString();
    doc["heap"]     = ESP.getFreeHeap() / 1024;
    doc["min_heap"] = ESP.getMinFreeHeap() / 1024;
    doc["rssi"]     = WiFi.RSSI();
    doc["uptime"]   = millis() / 1000;
    doc["volume"]   = pAudio->volumePercent;
    doc["fw"]       = JARVIS_FW_VERSION;
    doc["busy"]     = busy;
    doc["plan"]     = pBible->getActivePlanStatus();
    doc["notes"]    = pJarvis->notesList.substring(0, 400);
    doc["timers"]   = pJarvis->listActiveTimers();
    doc["spotify"]  = pSpotify->lastTrackInfo;
    doc["last_user"]  = lastUserText.substring(0, 200);
    doc["last_reply"] = lastJarvisReply.substring(0, 500);
    JsonArray al = doc.createNestedArray("alarms");
    for (int i = 0; i < JarvisFeatures::MAX_ALARMS; ++i) {
      if (!pJarvis->alarms[i].active) continue;
      JsonObject a = al.createNestedObject();
      a["h"] = pJarvis->alarms[i].hour;
      a["m"] = pJarvis->alarms[i].minute;
      a["r"] = pJarvis->alarms[i].repeatDaily;
      a["l"] = pJarvis->alarms[i].label;
    }
    String body;
    serializeJson(doc, body);
    doc.clear();

    HTTPClient http;
    WiFiClient tcp;
    bool ok = false;
    if (http.begin(tcp, String(EXTERNAL_SERVER_URL) + "/api/device/heartbeat")) {
      http.setConnectTimeout(1200);
      http.setTimeout(2500);
      http.addHeader("Content-Type", "application/json");
      int code = http.POST(body);
      if (code == 200) {
        ok = true;
        StaticJsonDocument<64> filter;
        filter["commands"] = true;   // copia o array inteiro
        DynamicJsonDocument resp(3072);
        if (!deserializeJson(resp, http.getStream(), DeserializationOption::Filter(filter))) {
          for (JsonObject o : resp["commands"].as<JsonArray>()) enqueueFromJson(o);
        }
      }
      http.end();
    }
    heartbeatFails = ok ? 0 : heartbeatFails + 1;
    // PC desligado: tenta com menos frequencia para nao travar o botao/loop
    nextHeartbeatAt = millis() + (ok ? 15000UL : (heartbeatFails > 3 ? 120000UL : 30000UL));
  }

  void begin(AudioManager& audio,
             SpotifyClient& spotify,
             SmartLifeClient& smartlife,
             BibleClient& bible,
             JarvisFeatures& jarvis,
             GeminiAgent& gemini,
             WiFiClientSecure& sharedTls) {
    pAudio = &audio; pSpotify = &spotify; pSmart = &smartlife; pBible = &bible;
    pJarvis = &jarvis; pGemini = &gemini; pTls = &sharedTls;
    if (MDNS.begin(HOSTNAME_MDNS)) {
      Serial.printf("🌐 [WebPortal] mDNS ativo: http://%s.local\n", HOSTNAME_MDNS);
    }

    server.on("/", HTTP_GET, [this]() {
      server.send_P(200, "text/html", JARVIS_HUD_HTML);
    });

    server.on("/api/status", HTTP_GET, [this, &audio, &spotify, &bible, &jarvis]() {
      DynamicJsonDocument doc(2560);
      doc["volume"]       = audio.volumePercent;
      doc["freeHeapKb"]   = ESP.getFreeHeap() / 1024;
      doc["rssi"]         = WiFi.RSSI();
      doc["time"]         = jarvis.getFormattedDateTime();
      doc["alarms"]       = jarvis.listAlarms();
      doc["timers"]       = jarvis.listActiveTimers();
      doc["notes"]        = jarvis.getNotes();
      doc["spotifyTrack"] = spotify.lastTrackInfo;
      doc["biblePlan"]    = bible.getActivePlanStatus();
      doc["bibleRef"]     = bible.lastReference;
      doc["bibleText"]    = bible.lastVerseText;
      doc["lastUser"]     = lastUserText;
      doc["lastReply"]    = lastJarvisReply;
      doc["uptime"]       = millis() / 1000;
      doc["ip"]           = WiFi.localIP().toString();
      doc["busy"]         = busy || qCount > 0;
      doc["fw"]           = JARVIS_FW_VERSION;
      doc["bridge"]       = EXTERNAL_SERVER_URL;
      doc["bridgeOnline"] = (heartbeatFails == 0 && BridgeLink::enabled());
      String out;
      serializeJson(doc, out);
      server.send(200, "application/json", out);
    });

    // Fila de comandos (usada pelo HUD e pelo Painel de Controle no PC)
    server.on("/api/queue", HTTP_GET, [this]() {
      PanelCmd c = cmdFromArgs();
      bool ok = enqueue(c);
      String out = String("{\"ok\":") + (ok ? "true" : "false") + ",\"queued\":" + String(qCount) + "}";
      server.send(ok ? 200 : 503, "application/json", out);
    });

    server.on("/api/trigger_mic", HTTP_POST, [this]() {
      remoteMicTriggered = true;
      server.send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/audio", HTTP_GET, [this, &audio]() {
      if (server.hasArg("vol")) {
        audio.setVolume(server.arg("vol").toInt());
      }
      if (server.hasArg("test")) {
        String t = server.arg("test");
        if (t == "left") {
          audio.playToneStereo(660.0f, 350, -1.0f, 0.45f); // 100% Alto-falante Esquerdo
        } else if (t == "right") {
          audio.playToneStereo(880.0f, 350, +1.0f, 0.45f); // 100% Alto-falante Direito
        } else if (t == "alarm") {
          audio.playAlarmSound();
        } else {
          audio.playWakeChime();
        }
      }
      server.send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/spotify", HTTP_GET, [this, &spotify, &audio, &sharedTls]() {
      String act = server.arg("action");
      String q   = server.arg("q");
      String res = spotify.executeCommand(sharedTls, act, q, "track", -1);
      audio.playSuccessBeep();
      DynamicJsonDocument doc(512);
      doc["result"] = res;
      String out;
      serializeJson(doc, out);
      server.send(200, "application/json", out);
    });

    server.on("/api/bible", HTTP_GET, [this, &bible, &audio, &sharedTls]() {
      String act   = server.hasArg("action") ? server.arg("action") : "";
      String ref   = server.arg("ref");
      String theme = server.arg("theme");
      int day      = server.hasArg("day") ? server.arg("day").toInt() : 0;
      bool speak   = (server.arg("speak") == "1");
      if (act.length() == 0) {
        act = (theme.length() > 0) ? "start_plan" : (ref.length() > 0 ? "get_passage" : "read_plan_day");
      }
      String res   = bible.executeCommand(sharedTls, act, ref, theme, day);
      audio.playSuccessBeep();
      if (speak && res.length() > 0) {
        audio.speakText(sharedTls, res);
      }
      DynamicJsonDocument doc(1536);
      doc["result"] = res;
      doc["plan"]   = bible.getActivePlanStatus();
      String out;
      serializeJson(doc, out);
      server.send(200, "application/json", out);
    });

    server.on("/api/alarms", HTTP_GET, [this, &jarvis, &audio]() {
      String act = server.arg("action");
      String res = "";
      if (act == "add") {
        int h = server.arg("h").toInt();
        int m = server.arg("m").toInt();
        String lbl = server.arg("label");
        res = jarvis.addAlarm(h, m, lbl, true);
        audio.playSuccessBeep();
      } else if (act == "remove") {
        int h = server.hasArg("h") ? server.arg("h").toInt() : -1;
        int m = server.hasArg("m") ? server.arg("m").toInt() : -1;
        String lbl = server.arg("label");
        res = jarvis.removeAlarm(h, m, lbl);
      } else if (act == "clear") {
        res = jarvis.clearAlarms();
      } else {
        res = jarvis.listAlarms();
      }
      DynamicJsonDocument doc(512);
      doc["result"] = res;
      doc["alarms"] = jarvis.listAlarms();
      String out;
      serializeJson(doc, out);
      server.send(200, "application/json", out);
    });

    server.on("/api/ask", HTTP_GET, [this, &audio, &spotify, &smartlife, &bible, &jarvis, &gemini, &sharedTls]() {
      String q = server.arg("q");
      String reply = "";
      if (q.length() > 0) {
        lastUserText = q;
        if (gemini.processUserQuery(sharedTls, q, reply, audio, spotify, smartlife, bible, jarvis)) {
          lastJarvisReply = reply;
          audio.speakText(sharedTls, reply);
        } else {
          reply = "Desculpe senhor, ocorreu uma falha ao consultar o Gemini.";
        }
      }
      DynamicJsonDocument doc(1024);
      doc["reply"] = reply;
      String out;
      serializeJson(doc, out);
      server.send(200, "application/json", out);
    });

    server.begin();
    Serial.printf("🌐 [WebPortal] Painel HUD disponivel em http://%s\n",
                  WiFi.localIP().toString().c_str());
  }

  void handleClient() {
    server.handleClient();
  }
};
