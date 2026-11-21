#pragma once
// ============================================================================
// J.A.R.V.I.S. — ESP32 HIBRIDO (nucleo sempre ligado) + Netlify (sob demanda)
// ============================================================================
// No ESP32 (24 h, sem custo): botao, microfone, alto-falantes, alarmes, lembretes,
//   timers, agenda das rotinas, voz -> texto direto no Groq, tocar MP3.
// No Netlify (so quando chamado, plano free): IA Gemini + ferramentas, voz neural
//   (Edge TTS em MP3), YouVersion, Spotify, casa inteligente, clima/noticias, painel.
//
// Coloque WiFi, URL e token em secrets_local.h (fica fora do git):
//   #define WIFI_SSID      "MinhaRede"
//   #define WIFI_PASSWORD  "minha-senha"
//   #define NETLIFY_URL    "https://seu-jarvis.netlify.app"
//   #define DEVICE_TOKEN   "o-mesmo-valor-de-DEVICE_TOKEN-no-Netlify"
//   #define GROQ_API_KEY   "gsk_..."   // opcional: tambem pode ser enviada pelo painel
// ============================================================================
#if __has_include("secrets_local.h")
#include "secrets_local.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "SEU_WIFI_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "SUA_SENHA_WIFI"
#endif
#if !defined(NETLIFY_URL) && defined(JARVIS_SERVER_URL)
#define NETLIFY_URL JARVIS_SERVER_URL                 // compatibilidade com o secrets_local.h anterior
#endif
#ifndef NETLIFY_URL
#define NETLIFY_URL "https://seu-jarvis.netlify.app"
#endif
#if !defined(DEVICE_TOKEN) && defined(JARVIS_DEVICE_TOKEN)
#define DEVICE_TOKEN JARVIS_DEVICE_TOKEN
#endif
#ifndef DEVICE_TOKEN
#define DEVICE_TOKEN ""
#endif
#ifndef GROQ_API_KEY
#define GROQ_API_KEY ""
#endif
#define GROQ_STT_MODEL_DEFAULT "whisper-large-v3-turbo"

#ifndef OTA_PASSWORD
#define OTA_PASSWORD "jarvis-ota"      // troque! usado para atualizar o firmware pelo WiFi
#endif
#define HOSTNAME_MDNS        "jarvis"
#define JARVIS_FW_VERSION    "4.0-hibrido"
#define NTP_TZ               "<-03>3"  // Brasilia (GMT-3)

// ==== Microfone INMP441 (I2S_NUM_0) ====
#define I2S_MIC_PORT         I2S_NUM_0
#define MIC_PIN_BCLK         26
#define MIC_PIN_LRCLK        25
#define MIC_PIN_DIN          33
#define MIC_SHIFT_BITS       11        // ganho digital 32-bit -> 16-bit

// ==== 2x MAX98357A estereo (I2S_NUM_1) ====
#define I2S_SPK_PORT         I2S_NUM_1
#define SPK_PIN_BCLK         14
#define SPK_PIN_LRCLK        15
#define SPK_PIN_DOUT         27

// ==== Botoes e LED (GPIO 6-11 proibidos: SPI flash) ====
#define PIN_WAKE_BUTTON      0         // BOOT
#define PIN_EXTERNAL_BTN     4
#define PIN_STATUS_LED       2

// ==== Audio ====
#define MIC_SAMPLE_RATE      16000     // Whisper
#define SPK_SAMPLE_RATE      24000     // MP3 do Edge TTS (o decoder ajusta se vier outro)
#define MP3_RING_BYTES       (40 * 1024)  // ~6,5 s de MP3 48 kbps: absorve a rede e a troca de partes
#define MAX_RECORD_SECONDS   10
#define VAD_RMS_THRESHOLD    350
#define VAD_SILENCE_MS       1100      // encerra 1,1 s depois que voce para de falar
#define VAD_NO_SPEECH_MS     4000      // desiste se ninguem falar em 4 s
#define DEFAULT_VOLUME_PCT   80

// ==== Agenda local ====
#define MAX_ALARMS           12
#define MAX_REMINDERS        16
#define MAX_TIMERS           4
#define MAX_ROUTINES         8
#define SYNC_IDLE_S          180       // sync normal (3 min): ~15 mil chamadas/mes, bem dentro do free
