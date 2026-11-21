/*
 * ============================================================================
 * J.A.R.V.I.S. — ASSISTENTE DE IA COMPLETO PARA ESP32 (ESP-WROOM-32 / ESP32S)
 * ============================================================================
 * Desenvolvido sob medida para:
 * - Placa: ESP32 WiFi + Bluetooth (ESP32S / ESP-WROOM-32 sem PSRAM)
 * - Audio: 2 Alto-Falantes I2S em Estereo Real (Canal L + Canal R) e 1 Microfone I2S
 * - Acionamento: Botao BOOT (GPIO 0), Botao Externo (GPIO 4), Painel Web HUD ou Serial
 *   (Sem sensor de distancia)
 * - IA: Google Gemini 2.5 Flash com Function Calling Nativo (4 Ferramentas Completas)
 * - Voz: Google Cloud STT (com VAD Inteligente) + Google Cloud Neural2 TTS Streaming
 * - Integracoes Completas:
 *     1. Spotify Web API (OAuth2 Refresh Token: Busca, Play, Pause, Next, Volume)
 *     2. App Biblia Sagrada (Leitura de capitulos, versiculos, Salmos e Versiculo do Dia)
 *     3. Guardiao de Alarmes Persistentes na NVS (HH:MM diarios/unicos) + Timers + Notas
 *     4. SmartLife / Tuya IoT Cloud (HMAC-SHA256 Nativo + Ponte Webhook)
 *     5. Servidor Externo Jarvis Bridge (Python FastAPI opcional para aliviar o ESP32)
 *     6. Painel Web HUD Embarcado (http://jarvis.local)
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_task_wdt.h"

#include "config_private.h"
#include "audio_manager.h"
#include "spotify_client.h"
#include "smartlife_tuya.h"
#include "bible_client.h"
#include "jarvis_features.h"
#include "gemini_agent.h"
#include "web_portal.h"

// ==== INSTANCIAS GLOBAIS DOS MODULOS ====
// Usamos um unico WiFiClientSecure compartilhado para economizar ~80 KB de RAM
// no ESP-WROOM-32 (evita crash de heap causado por multiplas conexoes TLS simultaneas)
WiFiClientSecure sharedTlsClient;
AudioManager     audio;
SpotifyClient    spotify;
SmartLifeClient  smartlife;
BibleClient      bible;
JarvisFeatures   jarvis;
GeminiAgent      gemini;
JarvisWebPortal  webPortal;

uint32_t cooldownUntil = 0;

// ==== CONEXAO WIFI OTIMIZADA PARA ESP-WROOM-32 ====
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // Desativa modem-sleep para menor latencia de audio streaming
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("📡 [WiFi] Conectando a '%s'", WIFI_SSID);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start < 20000)) {
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
    delay(250);
    Serial.print(".");
  }
  digitalWrite(PIN_STATUS_LED, LOW);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n❌ [WiFi] Nao foi possivel conectar! Verifique WIFI_SSID e WIFI_PASSWORD em config_private.h");
    return;
  }

  Serial.printf("\n✅ [WiFi] Conectado! IP: %s | Sinal: %d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

// ==== CICLO COMPLETO DE CONVERSA POR VOZ (STT -> GEMINI TOOLS -> TTS ESTEREO) ====
void runVoiceAssistantRound() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ [Jarvis] WiFi desconectado. Tentando reconectar...");
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;
  }

  uint32_t tStart = millis();
  Serial.println("\n=======================================================");
  Serial.printf("🚀 [Jarvis] Iniciando captura de voz (RAM Livre: %u bytes)\n", ESP.getFreeHeap());

  // 1. Escuta o microfone com VAD inteligente e transcreve via Google STT
  String transcript;
  if (!audio.listenAndTranscribe(sharedTlsClient, transcript)) {
    Serial.println("⚠️ [Jarvis] Nenhuma fala reconhecida.");
    return;
  }

  Serial.printf("📝 [Voce disse]: \"%s\"\n", transcript.c_str());
  webPortal.lastUserText = transcript;

  // 2. Processa com Gemini 2.5 Flash + Function Calling (Spotify / Biblia / Alarmes / SmartLife)
  String reply;
  Serial.println("🧠 [Gemini] Analisando intencao e executando ferramentas...");
  if (!gemini.processUserQuery(sharedTlsClient, transcript, reply,
                               audio, spotify, smartlife, bible, jarvis)) {
    reply = "Perdao senhor, tive uma instabilidade ao processar sua solicitacao.";
  }

  Serial.printf("🤖 [Jarvis]: \"%s\"\n", reply.c_str());
  webPortal.lastJarvisReply = reply;

  // 3. Sintetiza resposta por voz (Google Neural2 TTS) em streaming nos 2 alto-falantes
  audio.speakText(sharedTlsClient, reply);

  Serial.printf("⚡ [Jarvis] Ciclo completo finalizado em %u ms | RAM Livre: %u bytes\n",
                millis() - tStart, ESP.getFreeHeap());
  Serial.println("=======================================================\n");
}

// ==== PROCESSA COMANDO DIGITADO NO MONITOR SERIAL ====
void runTextCommandRound(const String& commandText) {
  Serial.printf("\n⌨️ [Comando Texto]: \"%s\"\n", commandText.c_str());
  webPortal.lastUserText = commandText;

  String reply;
  if (gemini.processUserQuery(sharedTlsClient, commandText, reply,
                               audio, spotify, smartlife, bible, jarvis)) {
    Serial.printf("🤖 [Jarvis]: \"%s\"\n", reply.c_str());
    webPortal.lastJarvisReply = reply;
    audio.speakText(sharedTlsClient, reply);
  } else {
    Serial.println("❌ [Gemini] Falha ao processar comando de texto.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);

  setCpuFrequencyMhz(240);

  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);
  pinMode(PIN_WAKE_BUTTON, INPUT_PULLUP);
  pinMode(PIN_EXTERNAL_BTN, INPUT_PULLUP);

  // Configura Watchdog tolerante a streaming de audio
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 30000,
    .idle_core_mask = 0,
    .trigger_panic = false
  };
  esp_task_wdt_init(&wdt_config);

  Serial.println("\n=======================================================");
  Serial.println("🤖 J.A.R.V.I.S. AI ASSISTANT — ESP-WROOM-32 EDITION");
  Serial.println("=======================================================");
  Serial.printf("⚡ CPU: %u MHz | RAM Livre: %u KB | Flash: %u MB\n",
                getCpuFrequencyMhz(),
                ESP.getFreeHeap() / 1024,
                ESP.getFlashChipSize() / (1024 * 1024));

  // 1. Inicializa Audio I2S (1 Microfone + 2 Alto-falantes Estereo)
  if (!audio.begin()) {
    Serial.println("❌ Erro critico no subsistema de audio. Reiniciando...");
    delay(3000);
    ESP.restart();
  }

  // Efeito sonoro de inicializacao nos 2 alto-falantes (Esquerdo -> Direito)
  audio.playWakeChime();

  // 2. Conecta ao WiFi
  connectWiFi();

  // 3. Inicializa Relogio NTP, Alarmes Persistentes (NVS), Planos do App Biblia (YouVersion NVS), Timers e Notas
  jarvis.begin();
  bible.begin();

  // 4. Inicializa Painel Web HUD (http://jarvis.local)
  if (WiFi.status() == WL_CONNECTED) {
    webPortal.begin(audio, spotify, smartlife, bible, jarvis, gemini, sharedTlsClient);
  }

  Serial.println("\n✅ J.A.R.V.I.S. ONLINE E PRONTO!");
  Serial.println("👉 Como acionar:");
  Serial.println("   1. Pressione o botao BOOT (GPIO 0) ou botao externo (GPIO 4) para falar");
  Serial.println("   2. Digite ENTER no Monitor Serial para falar, ou digite uma frase + ENTER");
  Serial.printf ("   3. Acesse http://%s.local ou http://%s no navegador\n\n",
                 HOSTNAME_MDNS, WiFi.localIP().toString().c_str());
}

void loop() {
  // 1. Atende requisicoes do Painel Web HUD
  webPortal.handleClient();

  // 2. Verifica se o microfone foi acionado pelo Painel Web
  if (webPortal.remoteMicTriggered) {
    webPortal.remoteMicTriggered = false;
    runVoiceAssistantRound();
    cooldownUntil = millis() + 1500;
  }

  // 3. Verifica Timers Regressivos e Alarmes Despertadores (HH:MM) salvos na NVS
  jarvis.checkTimersAndAlarms(audio, sharedTlsClient);

  // 4. Verifica Botao Fisico (BOOT GPIO 0 ou Botao Externo GPIO 4)
  if (millis() >= cooldownUntil) {
    if (digitalRead(PIN_WAKE_BUTTON) == LOW || digitalRead(PIN_EXTERNAL_BTN) == LOW) {
      delay(40); // Debounce
      if (digitalRead(PIN_WAKE_BUTTON) == LOW || digitalRead(PIN_EXTERNAL_BTN) == LOW) {
        Serial.println("🔘 [Trigger] Botao fisico pressionado!");
        runVoiceAssistantRound();
        cooldownUntil = millis() + 1500;
      }
    }
  }

  // 5. Verifica Monitor Serial (ENTER vazio = ativa microfone | texto + ENTER = envia texto ao Gemini)
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) {
      Serial.println("🎙️ [Trigger] Acionamento manual via Serial -> Microfone!");
      runVoiceAssistantRound();
    } else {
      runTextCommandRound(line);
    }
  }

  delay(10);
}
