#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#include <time.h>
#include "config_private.h"

// ============================================================================
// SMARTLIFE / TUYA IOT CLOUD CLIENT PARA ESP32 (JARVIS)
// ============================================================================
// Suporta:
// 1. Tuya IoT Cloud OpenAPI v1.0 com assinatura oficial HMAC-SHA256 nativa
// 2. Mapeamento por nome falado ou sinonimos ("luz da sala", "ar", "fita led")
// 3. Controle de Ligar/Desligar, Brilho (10-1000) e Cor RGB/HSV (colour_data_v2)
// 4. Ponte Webhook opcional (Home Assistant / Node-RED / IFTTT)
// ============================================================================

class SmartLifeClient {
public:
  String tuyaAccessToken = "";
  uint32_t tokenExpireMillis = 0;

  bool isTuyaConfigured() {
    if (!SMARTLIFE_ENABLED) return false;
    String aid = TUYA_ACCESS_ID;
    String sec = TUYA_ACCESS_SECRET;
    return (aid.length() > 8 && !aid.startsWith("SEU_") &&
            sec.length() > 8 && !sec.startsWith("SEU_"));
  }

  bool isWebhookConfigured() {
    return strlen(SMARTLIFE_WEBHOOK_URL) > 8;
  }

  // Calcula SHA256 em hexadecimal minusculo (usado no Content-SHA256 da Tuya)
  static String sha256Hex(const String& payload) {
    uint8_t hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const unsigned char*)payload.c_str(), payload.length());
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);

    char hex[65];
    for (int i = 0; i < 32; i++) {
      snprintf(hex + (i * 2), 3, "%02x", hash[i]);
    }
    hex[64] = '\0';
    return String(hex);
  }

  // Calcula HMAC-SHA256 em hexadecimal MAIUSCULO (padrao Tuya OpenAPI)
  static String hmacSha256Upper(const String& key, const String& msg) {
    uint8_t hmac[32];
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char*)key.c_str(), key.length());
    mbedtls_md_hmac_update(&ctx, (const unsigned char*)msg.c_str(), msg.length());
    mbedtls_md_hmac_finish(&ctx, hmac);
    mbedtls_md_free(&ctx);

    char hex[65];
    for (int i = 0; i < 32; i++) {
      snprintf(hex + (i * 2), 3, "%02X", hmac[i]);
    }
    hex[64] = '\0';
    return String(hex);
  }

  // Retorna timestamp epoch em milissegundos (sincronizado via NTP)
  static String getTimestampMs() {
    time_t now;
    time(&now);
    uint64_t ms = (uint64_t)now * 1000ULL + (millis() % 1000ULL);
    char buf[24];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)ms);
    return String(buf);
  }

  // Obtem token na Tuya Cloud OpenAPI (/v1.0/token?grant_type=1)
  bool ensureTuyaToken(WiFiClientSecure& cli) {
    if (!isTuyaConfigured()) return false;
    if (tuyaAccessToken.length() > 0 && millis() < tokenExpireMillis) {
      return true;
    }

    String path = "/v1.0/token?grant_type=1";
    String t = getTimestampMs();
    String contentHash = sha256Hex("");
    String stringToSign = String("GET\n") + contentHash + "\n\n" + path;
    String signPayload = String(TUYA_ACCESS_ID) + t + stringToSign;
    String sign = hmacSha256Upper(TUYA_ACCESS_SECRET, signPayload);

    cli.stop();
    cli.setInsecure();

    HTTPClient http;
    String url = String("https://") + TUYA_HOST + path;
    if (!http.begin(cli, url)) return false;

    http.addHeader("client_id", TUYA_ACCESS_ID);
    http.addHeader("sign", sign);
    http.addHeader("t", t);
    http.addHeader("sign_method", "HMAC-SHA256");

    int code = http.GET();
    String resp = http.getString();
    http.end();

    if (code != 200) {
      Serial.printf("❌ [SmartLife] Erro HTTP Tuya Token (%d)\n", code);
      return false;
    }

    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, resp)) return false;
    if (!(doc["success"] | false)) {
      Serial.printf("❌ [SmartLife] Tuya Auth erro: %s\n", (const char*)(doc["msg"] | "erro"));
      return false;
    }

    tuyaAccessToken = doc["result"]["access_token"] | "";
    int expireSec = doc["result"]["expire_time"] | 7200;
    tokenExpireMillis = millis() + (uint32_t)(expireSec - 120) * 1000UL;
    Serial.println("✅ [SmartLife] Token Tuya Cloud obtido com sucesso!");
    return tuyaAccessToken.length() > 0;
  }

  // Envia comando DP para um Device ID na Tuya Cloud
  bool sendTuyaCommands(WiFiClientSecure& cli, const String& deviceId, const String& commandsJson) {
    if (!ensureTuyaToken(cli)) return false;

    String path = String("/v1.0/iot-03/devices/") + deviceId + "/commands";
    String t = getTimestampMs();
    String contentHash = sha256Hex(commandsJson);
    String stringToSign = String("POST\n") + contentHash + "\n\n" + path;
    String signPayload = String(TUYA_ACCESS_ID) + tuyaAccessToken + t + stringToSign;
    String sign = hmacSha256Upper(TUYA_ACCESS_SECRET, signPayload);

    cli.stop();
    cli.setInsecure();

    HTTPClient http;
    String url = String("https://") + TUYA_HOST + path;
    if (!http.begin(cli, url)) return false;

    http.addHeader("client_id", TUYA_ACCESS_ID);
    http.addHeader("access_token", tuyaAccessToken);
    http.addHeader("sign", sign);
    http.addHeader("t", t);
    http.addHeader("sign_method", "HMAC-SHA256");
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(commandsJson);
    String resp = http.getString();
    http.end();

    DynamicJsonDocument doc(512);
    deserializeJson(doc, resp);
    bool ok = (code == 200) && (doc["success"] | false);
    Serial.printf("🏠 [SmartLife] Tuya Command -> Device=%s | Payload=%s | OK=%s\n",
                  deviceId.c_str(), commandsJson.c_str(), ok ? "SIM" : "NAO");
    return ok;
  }

  // Localiza dispositivo pelo nome falado ou apelido
  const SmartDeviceMap* findDevice(const String& spokenName) {
    String target = spokenName;
    target.toLowerCase();
    target.trim();

    for (size_t i = 0; i < SMARTLIFE_DEVICE_COUNT; ++i) {
      String fname = SMARTLIFE_DEVICES[i].friendlyName;
      fname.toLowerCase();
      if (target == fname || fname.indexOf(target) >= 0 || target.indexOf(fname) >= 0) {
        return &SMARTLIFE_DEVICES[i];
      }
      // Checa aliases
      String aliases = SMARTLIFE_DEVICES[i].aliases;
      aliases.toLowerCase();
      int start = 0;
      while (start < (int)aliases.length()) {
        int comma = aliases.indexOf(',', start);
        if (comma < 0) comma = aliases.length();
        String alias = aliases.substring(start, comma);
        alias.trim();
        if (alias.length() > 0 && (target == alias || target.indexOf(alias) >= 0)) {
          return &SMARTLIFE_DEVICES[i];
        }
        start = comma + 1;
      }
    }
    return nullptr;
  }

  // Converte nome de cor (portugues/ingles) para JSON HSV da Tuya (H: 0-360, S: 0-1000, V: 0-1000)
  static bool colorNameToTuyaHsv(const String& colorName, int& h, int& s, int& v) {
    String c = colorName;
    c.toLowerCase();
    s = 1000;
    v = 1000;
    if (c.indexOf("vermelh") >= 0 || c == "red")       { h = 0;   return true; }
    if (c.indexOf("laranja") >= 0 || c == "orange")    { h = 30;  return true; }
    if (c.indexOf("amarel") >= 0  || c == "yellow")    { h = 60;  return true; }
    if (c.indexOf("verde") >= 0   || c == "green")     { h = 120; return true; }
    if (c.indexOf("ciano") >= 0   || c == "cyan")      { h = 180; return true; }
    if (c.indexOf("azul") >= 0    || c == "blue")      { h = 240; return true; }
    if (c.indexOf("rox") >= 0     || c == "purple")    { h = 275; return true; }
    if (c.indexOf("rosa") >= 0    || c == "pink")      { h = 320; return true; }
    if (c.indexOf("branc") >= 0   || c == "white")     { h = 0; s = 0; v = 1000; return true; }
    if (c.indexOf("quente") >= 0  || c == "warm")      { h = 35; s = 350; v = 1000; return true; }
    return false;
  }

  // Executa controle de dispositivo (chamado pelo Gemini Function Calling ou Painel Web)
  String controlDevice(WiFiClientSecure& cli, const String& deviceName,
                       const String& state, int brightnessPct, const String& colorName) {
    const SmartDeviceMap* dev = findDevice(deviceName);
    String matchedName = dev ? String(dev->friendlyName) : deviceName;

    // Se houver Webhook configurado (ex: Home Assistant / Node-RED), dispara tambem
    if (isWebhookConfigured()) {
      HTTPClient http;
      http.begin(SMARTLIFE_WEBHOOK_URL);
      http.addHeader("Content-Type", "application/json");
      DynamicJsonDocument wdoc(256);
      wdoc["device"] = matchedName;
      wdoc["state"] = state;
      wdoc["brightness"] = brightnessPct;
      wdoc["color"] = colorName;
      String wbody;
      serializeJson(wdoc, wbody);
      int wcode = http.POST(wbody);
      http.end();
      if (wcode >= 200 && wcode < 300) {
        return "Dispositivo '" + matchedName + "' atualizado via Webhook SmartLife.";
      }
    }

    if (!dev) {
      return "Dispositivo '" + deviceName + "' nao encontrado na lista SMARTLIFE_DEVICES em config_private.h.";
    }

    if (!isTuyaConfigured()) {
      return "Simulacao SmartLife: '" + matchedName + "' -> estado=" + state +
             (brightnessPct >= 0 ? (", brilho=" + String(brightnessPct) + "%") : "") +
             (colorName.length() > 0 ? (", cor=" + colorName) : "") +
             " (configure TUYA_ACCESS_ID em config_private.h para envio real).";
    }

    DynamicJsonDocument cmdDoc(512);
    JsonArray commands = cmdDoc.createNestedArray("commands");

    if (state == "on" || state == "off") {
      JsonObject c1 = commands.createNestedObject();
      c1["code"] = dev->switchCode;
      c1["value"] = (state == "on");
    }

    if (dev->isRgbLight && brightnessPct >= 1 && brightnessPct <= 100) {
      JsonObject c2 = commands.createNestedObject();
      c2["code"] = "bright_value_v2";
      c2["value"] = map(brightnessPct, 1, 100, 10, 1000);
    }

    if (dev->isRgbLight && colorName.length() > 0) {
      int h, s, v;
      if (colorNameToTuyaHsv(colorName, h, s, v)) {
        if (s == 0) {
          JsonObject modeCmd = commands.createNestedObject();
          modeCmd["code"] = "work_mode";
          modeCmd["value"] = "white";
        } else {
          JsonObject modeCmd = commands.createNestedObject();
          modeCmd["code"] = "work_mode";
          modeCmd["value"] = "colour";

          JsonObject colCmd = commands.createNestedObject();
          colCmd["code"] = "colour_data_v2";
          char hsvJson[64];
          snprintf(hsvJson, sizeof(hsvJson), "{\"h\":%d,\"s\":%d,\"v\":%d}", h, s, v);
          colCmd["value"] = String(hsvJson);
        }
      }
    }

    String payload;
    serializeJson(cmdDoc, payload);

    bool ok = sendTuyaCommands(cli, dev->deviceId, payload);
    if (ok) {
      return "Comando SmartLife enviado com sucesso para " + matchedName + ".";
    }
    return "Erro ao comunicar com a nuvem Tuya para o dispositivo " + matchedName + ".";
  }
};

