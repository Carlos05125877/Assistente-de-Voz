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
    <div class="badge" id="statusBadge">ONLINE</div>
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
        <button onclick="sendQuick('Bom dia Jarvis, me de o briefing com clima, alarmes e versiculo do dia')">☀️ Briefing Completo</button>
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
        <button onclick="bibleStartPlan('ansiedade')">🕊️ Plano Ansiedade & Paz</button>
        <button onclick="bibleStartPlan('sabedoria')">💡 Plano Proverbios</button>
        <button onclick="bibleStartPlan('esperanca')">🔥 Plano Esperanca</button>
        <button onclick="bibleStartPlan('gratidao')">🙌 Plano Gratidao</button>
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
          `Horario: ${d.time}\nRAM Livre: ${d.freeHeapKb} KB | WiFi: ${d.rssi} dBm\nNotas: ${d.notes}`;
        if (d.lastReply) {
          document.getElementById('replyBox').innerText = `Voce: ${d.lastUser}\nJarvis: ${d.lastReply}`;
        }
      } catch (e) {}
    }
    async function sendCommand() {
      const q = document.getElementById('cmdInput').value.trim();
      if (!q) return;
      document.getElementById('replyBox').innerText = 'Processando com Gemini + TTS...';
      const r = await fetch('/api/ask?q=' + encodeURIComponent(q));
      const d = await r.json();
      document.getElementById('replyBox').innerText = `Voce: ${q}\nJarvis: ${d.reply}`;
      refreshStatus();
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
      document.getElementById('bibleBox').innerText = 'Consultando App Biblia (YouVersion)...';
      const isPlan = ref.includes('reading-plans') || ref.includes('bible.com') || /^\d+-/.test(ref);
      const url = isPlan
        ? `/api/bible?action=start_plan&theme=${encodeURIComponent(ref)}&speak=${speak ? 1 : 0}`
        : `/api/bible?action=get_passage&ref=${encodeURIComponent(ref)}&speak=${speak ? 1 : 0}`;
      const r = await fetch(url);
      const d = await r.json();
      document.getElementById('bibleBox').innerText = d.result;
      refreshStatus();
    }
    async function bibleAction(act, theme, speak) {
      document.getElementById('bibleBox').innerText = 'Conectando ao App Biblia YouVersion (bible.com/pt/reading-plans)...';
      const r = await fetch(`/api/bible?action=${encodeURIComponent(act)}&theme=${encodeURIComponent(theme)}&speak=${speak ? 1 : 0}`);
      const d = await r.json();
      document.getElementById('bibleBox').innerText = d.result;
      refreshStatus();
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

class JarvisWebPortal {
public:
  WebServer server{80};
  bool remoteMicTriggered = false;
  String lastUserText = "";
  String lastJarvisReply = "";

  void begin(AudioManager& audio,
             SpotifyClient& spotify,
             SmartLifeClient& smartlife,
             BibleClient& bible,
             JarvisFeatures& jarvis,
             GeminiAgent& gemini,
             WiFiClientSecure& sharedTls) {
    if (MDNS.begin(HOSTNAME_MDNS)) {
      Serial.printf("🌐 [WebPortal] mDNS ativo: http://%s.local\n", HOSTNAME_MDNS);
    }

    server.on("/", HTTP_GET, [this]() {
      server.send_P(200, "text/html", JARVIS_HUD_HTML);
    });

    server.on("/api/status", HTTP_GET, [this, &audio, &spotify, &bible, &jarvis]() {
      DynamicJsonDocument doc(1536);
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
      String out;
      serializeJson(doc, out);
      server.send(200, "application/json", out);
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
