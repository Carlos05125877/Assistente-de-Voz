#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config_private.h"
#include "audio_manager.h"

// ============================================================================
// BRIDGE LINK — utilitarios de comunicacao ESP32 <-> jarvis_bridge_server.py
// ============================================================================
// - fetchSpoken(): chama um endpoint do Bridge que responde no formato compacto
//   {spoken, tts_key} e prepara o AudioManager para tocar o audio COMPLETO
//   guardado no servidor (/api/tts?key=...), sem trazer o texto longo para a RAM.
// - Usado pelo briefing matinal, pelo alarme devocional e pela fila de comandos
//   vinda do Painel de Controle no PC.
// ============================================================================

#ifndef JARVIS_FW_VERSION
#define JARVIS_FW_VERSION "2.1"
#endif

namespace BridgeLink {

static bool enabled() {
  return strlen(EXTERNAL_SERVER_URL) > 8 && WiFi.status() == WL_CONNECTED;
}

// GET <EXTERNAL_SERVER_URL><path> -> {spoken, tts_key}. Retorna o resumo falavel.
static bool fetchSpoken(const String& path, String& outSpoken, uint32_t timeoutMs = 45000) {
  if (!enabled()) return false;
  HTTPClient http;
  WiFiClient tcp;
  if (!http.begin(tcp, String(EXTERNAL_SERVER_URL) + path)) return false;
  http.setConnectTimeout(3000);
  http.setTimeout(timeoutMs);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("⚠️ [Bridge] HTTP %d em %s\n", code, path.c_str());
    http.end();
    return false;
  }
  StaticJsonDocument<96> filter;
  filter["spoken"] = true;
  filter["tts_key"] = true;
  DynamicJsonDocument doc(2560);
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) return false;
  String spoken = doc["spoken"] | "";
  String key = doc["tts_key"] | "";
  spoken.replace("*", "");
  spoken.replace("#", "");
  spoken.replace("`", "");
  spoken.trim();
  if (spoken.length() == 0) return false;
  if (key.length() > 0) {
    g_bridgeTtsKey = key;
    g_bridgeTtsPrefix = spoken.substring(0, 48);
  }
  outSpoken = spoken;
  return true;
}

// Prepara o AudioManager para falar o texto guardado no servidor sob `key`
static void armTtsKey(const String& key, const String& shortText) {
  g_bridgeTtsKey = key;
  g_bridgeTtsPrefix = shortText.substring(0, 48);
}

}  // namespace BridgeLink
