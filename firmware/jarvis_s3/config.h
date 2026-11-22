#pragma once
// ============================================================================
// J.A.R.V.I.S. — ESP32-S3 (N16R8) + Netlify, 100% gratis
// ============================================================================
// No ESP32-S3 (24 h, offline): palavra "Jarvis" (WakeNet), cancelamento de eco,
//   supressao de ruido, deteccao de fim de fala, alarmes, lembretes, timers,
//   horarios das rotinas, voz -> texto direto no Groq, player MP3.
// No Netlify (sob demanda, plano free): IA Gemini + ferramentas, voz neural (Edge TTS),
//   YouVersion, Spotify, casa inteligente, clima/noticias/cotacoes, painel.
// WiFi, URL e token: copie secrets_local.example.h para secrets_local.h.
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
#ifndef NETLIFY_URL
#define NETLIFY_URL "https://seu-jarvis.netlify.app"
#endif
#ifndef DEVICE_TOKEN
#define DEVICE_TOKEN ""
#endif
#ifndef GROQ_API_KEY
#define GROQ_API_KEY ""
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD "jarvis-ota"
#endif
#define GROQ_STT_MODEL_DEFAULT "whisper-large-v3-turbo"
#define HOSTNAME_MDNS        "jarvis"
#define JARVIS_FW_VERSION    "5.0-s3"
#define NTP_TZ               "<-03>3"   // Brasilia (GMT-3)

// ==== Pinos (ESP32-S3-DevKitC-1). Evite 19/20 (USB), 26-37 (flash/PSRAM octal), 43/44 (UART0) ====
#define MIC_PIN_BCLK         4          // INMP441 SCK
#define MIC_PIN_WS           5          // INMP441 WS
#define MIC_PIN_DIN          6          // INMP441 SD (L/R no GND = canal esquerdo)
#define SPK_PIN_BCLK         15         // 2x MAX98357A BCLK
#define SPK_PIN_WS           16         // 2x MAX98357A LRC
#define SPK_PIN_DOUT         7          // 2x MAX98357A DIN (SD de um no 3V3 via resistor = esquerdo / outro = direito)
#define PIN_WAKE_BUTTON      0          // BOOT
#define PIN_EXTERNAL_BTN     21         // botao externo para o GND
#define PIN_RGB_LED          48         // LED RGB da placa (DevKitC-1 v1.1 usa o 38)

// ==== Audio ====
#define MIC_SAMPLE_RATE      16000      // WakeNet / AFE / Whisper
#define MIC_SHIFT_BITS       14         // INMP441 24-bit em slot de 32 -> 16-bit (ajuste o ganho aqui)
#define SPK_SAMPLE_RATE      24000      // MP3 do Edge TTS
#define MP3_RING_BYTES       (256 * 1024)   // ~42 s de MP3 48 kbps na PSRAM: troca de partes sem pausa
#define CAPTURE_RING_BYTES   (MIC_SAMPLE_RATE * 2 * 14)  // 14 s de fala limpa na PSRAM
#define MAX_RECORD_SECONDS   12
#define END_SILENCE_MS       900        // fim da fala: 0,9 s de silencio (VAD do AFE)
#define NO_SPEECH_MS         5000       // desiste se ninguem falar em 5 s
#define DEFAULT_VOLUME_PCT   80

// ==== Agenda local ====
#define MAX_ALARMS           12
#define MAX_REMINDERS        24
#define MAX_TIMERS           6
#define MAX_ROUTINES         12
#define SYNC_IDLE_S          180        // sync normal: 3 min (~15 mil chamadas/mes)
