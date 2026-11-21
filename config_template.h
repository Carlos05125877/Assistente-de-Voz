#pragma once

#if __has_include("secrets_local.h")
  #include "secrets_local.h"
#endif

// ============================================================================
// JARVIS ESP32 (ESP-WROOM-32) - ARQUIVO DE CONFIGURACAO E CREDENCIAIS
// ============================================================================
// Instrucoes:
// 1. Preencha suas credenciais abaixo (ou copie de config_template.h para config_private.h)
// 2. Nunca suba seu config_private.h com chaves reais em repositorios publicos!
// ============================================================================

// ==== 1. REDE WIFI ====
#define WIFI_SSID           "SEU_WIFI_SSID"
#define WIFI_PASSWORD       "SUA_SENHA_WIFI"
#define HOSTNAME_MDNS       "jarvis" // Acesse http://jarvis.local na rede local

// ==== 2. SERVIDOR EXTERNO JARVIS BRIDGE (OPCIONAL — ALIVIA O ESP32) ====
// Se voce rodar o servidor Python incluso (external_server/jarvis_bridge_server.py)
// no seu PC, Raspberry Pi ou VPS, coloque a URL HTTP dele abaixo.
// Quando preenchido, o ESP32 delega Voz Neural Edge-TTS, OAuth2 do Spotify, Biblia
// YouVersion e sincronizacao de alarmes para o servidor externo via HTTP leve!
// Se o servidor estiver desligado, o ESP32 usa automaticamente o fallback direto!
#define EXTERNAL_SERVER_URL "http://192.168.1.120:8080"

// ==== 3. PERSONALIDADE & IDIOMA DO ASSISTENTE ====
#define ASSISTANT_NAME      "Jarvis"
#define LANG_CODE           "pt-BR"
#define TTS_VOICE_NAME      "pt-BR-AntonioNeural" // Voz neural masculina estilo J.A.R.V.I.S. (Edge-TTS)
#define GEMINI_TTS_VOICE    "Orus"                // Voz neural masculina no fallback standalone (AI Studio)
#define TTS_SPEAKING_RATE   1.10f
#define TTS_PITCH           -1.5f

// ==== 4. CREDENCIAIS 100% GRATUITAS E SEM CARTAO (GROQ WHISPER + GOOGLE AI STUDIO) ====
// - GROQ_API_KEY: Reconhecimento de voz ultra-rapido (~0.2s) com Whisper Large v3 Turbo
// - GEMINI_API_KEY: Cerebro Gemini + Function Calling + Fallback TTS (Google AI Studio)
#ifndef GROQ_API_KEY
  #define GROQ_API_KEY      "SUA_CHAVE_GROQ_AQUI"
#endif
#define GROQ_STT_MODEL      "whisper-large-v3-turbo"
#ifndef GEMINI_API_KEY
  #define GEMINI_API_KEY    "SUA_CHAVE_GEMINI_AI_STUDIO_AQUI"
#endif
#define GEMINI_MODEL        "gemini-flash-lite-latest"
#define GEMINI_TTS_MODEL    "gemini-2.5-flash-preview-tts"
#define GCP_API_KEY         GEMINI_API_KEY

// ==== 5. SPOTIFY WEB API & CONTA PESSOAL APP BIBLIA (YOUVERSION) ====
// - Spotify: Autentique em http://127.0.0.1:8080/spotify/login
// - App Biblia (YouVersion): Faca login com sua conta pessoal do bible.com em http://127.0.0.1:8080
//   para sincronizar seus Planos de Leitura inscritos, dias concluidos e devocionais da sua conta!
#define YOUVERSION_EMAIL        "" // Opcional: Seu e-mail/usuario do App Biblia (ou logue em http://127.0.0.1:8080)
#define YOUVERSION_PASSWORD     "" // Opcional: Sua senha do App Biblia (ou logue em http://127.0.0.1:8080)

#define SPOTIFY_ENABLED         true
#ifndef SPOTIFY_CLIENT_ID
  #define SPOTIFY_CLIENT_ID     "SEU_SPOTIFY_CLIENT_ID"
#endif
#ifndef SPOTIFY_CLIENT_SECRET
  #define SPOTIFY_CLIENT_SECRET "SEU_SPOTIFY_CLIENT_SECRET"
#endif
#ifndef SPOTIFY_REFRESH_TOKEN
  #define SPOTIFY_REFRESH_TOKEN "SEU_SPOTIFY_REFRESH_TOKEN"
#endif
#define SPOTIFY_DEFAULT_DEVICE  "" // Opcional: ID do dispositivo Spotify padrao

// ==== 6. SMARTLIFE / TUYA IOT CLOUD ====
// Crie um projeto Cloud em https://iot.tuya.com e vincule sua conta do app SmartLife
// Datacenters:
//   America (Western): "openapi.tuyaus.com"
//   America (Eastern): "openapi-ueaz.tuyaus.com"
//   Europa:            "openapi.tuyaeu.com"
#define SMARTLIFE_ENABLED       true
#define TUYA_HOST               "openapi.tuyaus.com"
#define TUYA_ACCESS_ID          "SEU_TUYA_ACCESS_ID"
#define TUYA_ACCESS_SECRET      "SEU_TUYA_ACCESS_SECRET"

// Webhook opcional (Home Assistant / Node-RED / IFTTT) caso prefira ponte local
#define SMARTLIFE_WEBHOOK_URL   "" // Ex: "http://192.168.1.100:8123/api/webhook/jarvis"

// ==== 7. CLIMA & LOCALIZACAO (Open-Meteo - Gratuito, sem API Key) ====
#define WEATHER_LATITUDE        "-23.5505" // Ex: Sao Paulo
#define WEATHER_LONGITUDE       "-46.6333"
#define NTP_TIMEZONE_OFFSET_SEC (-3 * 3600) // GMT-3 (Brasilia)

// ============================================================================
// 8. PINAGEM DE HARDWARE — OTIMIZADA PARA ESP-WROOM-32 (SEM SENSOR DE DISTANCIA)
// ============================================================================
// ATENCAO: No ESP-WROOM-32, os pinos GPIO 6 a 11 sao usados pela Flash SPI interna!
// NUNCA use GPIO 6-11 no ESP-WROOM-32. Os pinos abaixo sao 100% seguros.

// --- Microfone I2S (INMP441 / ICS-43434) na Porta I2S_NUM_0 ---
#define I2S_MIC_PORT        I2S_NUM_0
#define MIC_PIN_BCLK        26  // SCK / BCLK do INMP441
#define MIC_PIN_LRCLK       25  // WS  / LRC do INMP441
#define MIC_PIN_DIN         33  // SD  / DOUT do INMP441 (L/R do INMP441 no GND)

// --- 2 Alto-Falantes I2S Estereo (2x MAX98357A ou DAC PCM5102A) na Porta I2S_NUM_1 ---
// Para 2x MAX98357A: ligue BCLK, LRCLK e DOUT em paralelo nos dois modulos.
// Modulo Esquerdo (L): pino SD_MODE ligado ao VIN (3.3V)
// Modulo Direito  (R): pino SD_MODE ligado ao VIN via resistor ~100k-470k (ou deixe aberto p/ mix L+R)
#define I2S_SPK_PORT        I2S_NUM_1
#define SPK_PIN_BCLK        14  // BCLK dos amplificadores I2S
#define SPK_PIN_LRCLK       15  // LRC / WS dos amplificadores I2S
#define SPK_PIN_DOUT        27  // DIN dos amplificadores I2S

// --- Botoes Push-to-Talk e LED de Status (Sem sensor de distancia) ---
#define PIN_WAKE_BUTTON     0   // Botao BOOT (GPIO 0) nativo da placa
#define PIN_EXTERNAL_BTN    4   // Botao externo opcional (Touch/Push entre GPIO 4 e GND)
#define PIN_STATUS_LED      2   // LED onboard do ESP-WROOM-32

// ==== 9. PARAMETROS DE AUDIO & VAD (Voice Activity Detection) ====
#define AUDIO_SAMPLE_RATE   16000
#define MAX_RECORD_SECONDS  6       // Tempo maximo de gravacao por comando
#define VAD_SILENCE_MS      1200    // Para de gravar automaticamente apos 1.2s de silencio
#define VAD_RMS_THRESHOLD   350     // Limiar de voz (ajuste conforme o ganho do ambiente)
#define MIC_SHIFT_BITS      11      // Ganho digital 32-bit -> 16-bit do INMP441
#define DEFAULT_VOLUME_PCT  80      // Volume inicial dos 2 alto-falantes (0 a 100%)
