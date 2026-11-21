#pragma once
#include "config_template.h"

// ============================================================================
// MAPA DE DISPOSITIVOS SMARTLIFE / TUYA DA SUA CASA
// ============================================================================
// Cadastre abaixo os nomes amigaveis (como voce vai falar para o Jarvis)
// e o respectivo Device ID do seu dispositivo no Tuya IoT / SmartLife,
// alem do codigo DP padrao de acionamento (ex: "switch_led" para lampadas,
// "switch_1" para tomadas/interruptores, "switch" para ar-condicionado).
// ============================================================================

struct SmartDeviceMap {
  const char* friendlyName; // Nome falado (ex: "luz da sala")
  const char* aliases;      // Sinonimos separados por virgula
  const char* deviceId;     // Device ID na Tuya Cloud
  const char* switchCode;   // Codigo DP principal: "switch_led", "switch_1", "switch"
  bool isRgbLight;          // Suporta brilho/cor (bright_value_v2 / colour_data_v2)?
};

static const SmartDeviceMap SMARTLIFE_DEVICES[] = {
  { "luz da sala",      "sala,lampada da sala,lustre",       "eb1234567890abcdef01", "switch_led", true  },
  { "luz do quarto",    "quarto,abajur,lampada do quarto",   "eb1234567890abcdef02", "switch_led", true  },
  { "fita led",         "led,led do setup,fita",             "eb1234567890abcdef03", "switch_led", true  },
  { "ar condicionado",  "ar,climatizador,ar do quarto",      "eb1234567890abcdef04", "switch",     false },
  { "ventilador",       "tomada do ventilador",              "eb1234567890abcdef05", "switch_1",   false },
  { "cafeteira",        "cafe,tomada da cafeteira",          "eb1234567890abcdef06", "switch_1",   false }
};

static const size_t SMARTLIFE_DEVICE_COUNT = sizeof(SMARTLIFE_DEVICES) / sizeof(SMARTLIFE_DEVICES[0]);

