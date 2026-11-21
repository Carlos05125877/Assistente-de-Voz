#pragma once
// ============================================================================
// J.A.R.V.I.S. — ESP32 THIN CLIENT (so microfone, alto-falantes e botao)
// ============================================================================
// Toda a inteligencia (STT, IA, ferramentas, TTS, alarmes, Spotify, Biblia, casa
// inteligente) roda no servidor (external_server/jarvis_bridge_server.py), local ou
// na nuvem (Fly.io). As chaves de API ficam SO no servidor.
//
// Coloque WiFi, URL e token em secrets_local.h (fica fora do git):
//   #define WIFI_SSID           "MinhaRede"
//   #define WIFI_PASSWORD       "minha-senha"
//   #define JARVIS_SERVER_URL   "https://jarvis-carlos.fly.dev"   // ou "http://192.168.1.120:8080"
//   #define JARVIS_DEVICE_TOKEN "o-mesmo-valor-de-DEVICE_TOKEN-no-servidor"
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
#if !defined(JARVIS_SERVER_URL) && defined(EXTERNAL_SERVER_URL)
#define JARVIS_SERVER_URL EXTERNAL_SERVER_URL            // compatibilidade com o secrets_local.h antigo
#endif
#ifndef JARVIS_SERVER_URL
#define JARVIS_SERVER_URL "http://192.168.1.120:8080"
#endif
#ifndef JARVIS_DEVICE_TOKEN
#define JARVIS_DEVICE_TOKEN ""
#endif
// Opcional: certificado raiz (PEM) do servidor HTTPS. Sem ele a conexao continua criptografada,
// mas o certificado nao e validado (o token do dispositivo protege o acesso).
// #define JARVIS_ROOT_CA "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"

#ifndef OTA_PASSWORD
#define OTA_PASSWORD "jarvis-ota"      // troque! usado para atualizar o firmware pelo WiFi
#endif
#define HOSTNAME_MDNS        "jarvis"
#define JARVIS_FW_VERSION    "3.0-thin"
#define NTP_TZ               "<-03>3"  // Brasilia (GMT-3), so para os alarmes offline

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

// ==== Audio / VAD ====
#define AUDIO_SAMPLE_RATE    16000
#define MAX_RECORD_SECONDS   10
#define VAD_RMS_THRESHOLD    350
#define VAD_SILENCE_MS       1100      // encerra 1,1 s depois que voce para de falar
#define VAD_NO_SPEECH_MS     4000      // desiste se ninguem falar em 4 s
#define DEFAULT_VOLUME_PCT   80
