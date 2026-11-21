#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include "config_private.h"
#include "audio_manager.h"

// ============================================================================
// FUNCIONALIDADES COMPLETAS DO SISTEMA JARVIS (ALARMES NVS, TIMERS, NOTAS, CLIMA)
// ============================================================================
// - Despertadores / Alarmes por Horario (HH:MM) SALVOS NA FLASH NVS (persistentes!)
//   Suporta alarmes diarios recorrentes ou unicos, com aviso sonoro estereo + voz TTS
// - Timers regressivos em background com alerta sonoro estereo + fala TTS
// - Lista de Lembretes / Tarefas / Compras salva na Flash NVS (nao perde ao desligar!)
// - Previsao do Tempo em Tempo Real via Open-Meteo (ou Servidor Externo)
// - Relogio NTP sincronizado (Hora, Data, Dia da Semana)
// ============================================================================

struct JarvisTimer {
  bool active = false;
  uint32_t triggerMillis = 0;
  String label = "";
};

struct JarvisAlarm {
  bool active = false;
  int hour = 7;           // 0 a 23
  int minute = 0;         // 0 a 59
  bool repeatDaily = true;
  int lastTriggeredYday = -1; // Evita disparar 2x no mesmo minuto/dia
  String label = "";
};

class JarvisFeatures {
public:
  static const int MAX_TIMERS = 5;
  static const int MAX_ALARMS = 8;

  JarvisTimer timers[MAX_TIMERS];
  JarvisAlarm alarms[MAX_ALARMS];
  Preferences prefs;
  String notesList = "";

  void begin() {
    // Sincroniza relogio via NTP
    configTime(NTP_TIMEZONE_OFFSET_SEC, 0, "pool.ntp.org", "time.nist.gov", "a.st1.ntp.br");
    prefs.begin("jarvis_nvs", false);
    notesList = prefs.getString("notes", "");
    loadAlarmsFromNVS();
    Serial.println("🕒 [Jarvis] NTP, Alarmes Persistentes e Memoria NVS inicializados.");
  }

  // ==== PERSISTENCIA DE ALARMES NA FLASH NVS (E SINCRONIZACAO COM SERVIDOR EXTERNO) ====
  void saveAlarmsToNVS() {
    DynamicJsonDocument doc(2048);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < MAX_ALARMS; ++i) {
      if (alarms[i].active) {
        JsonObject o = arr.createNestedObject();
        o["h"] = alarms[i].hour;
        o["m"] = alarms[i].minute;
        o["r"] = alarms[i].repeatDaily;
        o["l"] = alarms[i].label;
      }
    }
    String json;
    serializeJson(doc, json);
    prefs.putString("alarms", json);

    // Se houver Servidor Externo configurado, sincroniza os alarmes com ele tambem
    if (strlen(EXTERNAL_SERVER_URL) > 8 && WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      if (http.begin(String(EXTERNAL_SERVER_URL) + "/api/alarms/sync")) {
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(2500);
        http.POST(json);
        http.end();
      }
    }
  }

  void loadAlarmsFromNVS() {
    for (int i = 0; i < MAX_ALARMS; ++i) {
      alarms[i].active = false;
    }
    String json = prefs.getString("alarms", "");
    if (json.length() == 0) return;

    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, json)) return;

    JsonArray arr = doc.as<JsonArray>();
    int idx = 0;
    for (JsonObject o : arr) {
      if (idx >= MAX_ALARMS) break;
      alarms[idx].active = true;
      alarms[idx].hour = o["h"] | 7;
      alarms[idx].minute = o["m"] | 0;
      alarms[idx].repeatDaily = o["r"] | true;
      alarms[idx].label = o["l"] | "Despertador";
      alarms[idx].lastTriggeredYday = -1;
      idx++;
    }
    Serial.printf("⏰ [Jarvis] %d alarme(s) carregado(s) da memoria NVS.\n", idx);
  }

  // Adiciona ou atualiza um alarme de relogio (HH:MM) salvo na NVS
  String addAlarm(int hour, int minute, const String& label, bool repeatDaily = true) {
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
      return "Horario de alarme invalido. Informe hora entre 0 e 23 e minuto entre 0 e 59.";
    }

    // Se ja existir alarme no mesmo horario, atualiza
    int slot = -1;
    for (int i = 0; i < MAX_ALARMS; ++i) {
      if (alarms[i].active && alarms[i].hour == hour && alarms[i].minute == minute) {
        slot = i;
        break;
      }
    }
    if (slot < 0) {
      for (int i = 0; i < MAX_ALARMS; ++i) {
        if (!alarms[i].active) {
          slot = i;
          break;
        }
      }
    }

    if (slot < 0) {
      return "Limite maximo de 8 alarmes salvos atingido. Remova um alarme antes.";
    }

    alarms[slot].active = true;
    alarms[slot].hour = hour;
    alarms[slot].minute = minute;
    alarms[slot].repeatDaily = repeatDaily;
    alarms[slot].label = label.length() > 0 ? label : "Despertador";
    alarms[slot].lastTriggeredYday = -1;

    saveAlarmsToNVS();

    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", hour, minute);
    String msg = "Alarme '" + alarms[slot].label + "' salvo para as " + String(timeBuf) +
                 (repeatDaily ? " (todos os dias)." : " (uma vez).");
    Serial.println("⏰ [Jarvis] " + msg);
    return msg;
  }

  // Remove um alarme especifico por horario (HH:MM) ou pelo nome/label
  String removeAlarm(int hour, int minute, const String& label) {
    int removed = 0;
    String targetLbl = label;
    targetLbl.toLowerCase();
    targetLbl.trim();

    for (int i = 0; i < MAX_ALARMS; ++i) {
      if (!alarms[i].active) continue;
      bool matchTime = (hour >= 0 && minute >= 0 && alarms[i].hour == hour && alarms[i].minute == minute);
      String curLbl = alarms[i].label;
      curLbl.toLowerCase();
      bool matchLabel = (targetLbl.length() > 0 && (curLbl == targetLbl || curLbl.indexOf(targetLbl) >= 0));

      if (matchTime || matchLabel) {
        alarms[i].active = false;
        removed++;
      }
    }

    if (removed > 0) {
      saveAlarmsToNVS();
      return "Foram removidos " + String(removed) + " alarme(s) da memoria.";
    }
    return "Nenhum alarme correspondente foi encontrado para remover.";
  }

  String clearAlarms() {
    int count = 0;
    for (int i = 0; i < MAX_ALARMS; ++i) {
      if (alarms[i].active) {
        alarms[i].active = false;
        count++;
      }
    }
    saveAlarmsToNVS();
    return "Todos os " + String(count) + " alarmes salvos foram apagados.";
  }

  String listAlarms() {
    String out = "";
    for (int i = 0; i < MAX_ALARMS; ++i) {
      if (alarms[i].active) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%02d:%02d (%s%s)",
                 alarms[i].hour, alarms[i].minute,
                 alarms[i].label.c_str(),
                 alarms[i].repeatDaily ? ", diario" : "");
        if (out.length() > 0) out += "; ";
        out += buf;
      }
    }
    return out.length() > 0 ? out : "Nenhum alarme salvo.";
  }

  // Retorna data e hora formatadas em Portugues
  String getFormattedDateTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 200)) {
      return "Horario ainda sincronizando via NTP";
    }
    const char* dias[] = {"Domingo", "Segunda-feira", "Terca-feira", "Quarta-feira",
                          "Quinta-feira", "Sexta-feira", "Sabado"};
    char buf[80];
    snprintf(buf, sizeof(buf), "%s, %02d/%02d/%04d, %02d:%02d",
             dias[timeinfo.tm_wday],
             timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
             timeinfo.tm_hour, timeinfo.tm_min);
    return String(buf);
  }

  // ==== TIMERS REGRESSIVOS ====
  String addTimer(int durationSeconds, const String& label) {
    if (durationSeconds <= 0) return "Duracao de timer invalida.";
    for (int i = 0; i < MAX_TIMERS; ++i) {
      if (!timers[i].active) {
        timers[i].active = true;
        timers[i].triggerMillis = millis() + (uint32_t)durationSeconds * 1000UL;
        timers[i].label = label.length() > 0 ? label : "Timer";
        int mins = durationSeconds / 60;
        int secs = durationSeconds % 60;
        String msg = "Timer '" + timers[i].label + "' programado para ";
        if (mins > 0) msg += String(mins) + " minuto(s) ";
        if (secs > 0) msg += String(secs) + " segundo(s)";
        Serial.println("⏲️ [Jarvis] " + msg);
        return msg;
      }
    }
    return "Limite maximo de 5 timers simultaneos atingido.";
  }

  String clearTimers() {
    int count = 0;
    for (int i = 0; i < MAX_TIMERS; ++i) {
      if (timers[i].active) {
        timers[i].active = false;
        count++;
      }
    }
    return "Foram cancelados " + String(count) + " timers ativos.";
  }

  String listActiveTimers() {
    String out = "";
    uint32_t now = millis();
    for (int i = 0; i < MAX_TIMERS; ++i) {
      if (timers[i].active) {
        int32_t remSec = (int32_t)(timers[i].triggerMillis - now) / 1000;
        if (remSec < 0) remSec = 0;
        if (out.length() > 0) out += "; ";
        out += timers[i].label + " (" + String(remSec / 60) + "m" + String(remSec % 60) + "s)";
      }
    }
    return out.length() > 0 ? out : "Nenhum timer ativo.";
  }

  // Verifica no loop principal tanto os TIMERS regressivos quanto os ALARMES de relogio (HH:MM)
  void checkTimersAndAlarms(AudioManager& audio, WiFiClientSecure& tlsClient) {
    uint32_t now = millis();

    // 1. Verifica Timers Regressivos
    for (int i = 0; i < MAX_TIMERS; ++i) {
      if (timers[i].active && (int32_t)(now - timers[i].triggerMillis) >= 0) {
        String lbl = timers[i].label;
        timers[i].active = false;
        Serial.printf("🔔 [Jarvis] TIMER DISPARADO: %s\n", lbl.c_str());
        audio.playAlarmSound();
        String speech = "Senhor, o seu timer para " + lbl + " acabou de finalizar.";
        audio.speakText(tlsClient, speech);
      }
    }

    // 2. Verifica Alarmes por Horario Real (NTP) a cada 2 segundos
    static uint32_t lastClockCheck = 0;
    if (now - lastClockCheck >= 2000) {
      lastClockCheck = now;
      struct tm timeinfo;
      if (getLocalTime(&timeinfo, 50)) {
        for (int i = 0; i < MAX_ALARMS; ++i) {
          if (!alarms[i].active) continue;
          if (timeinfo.tm_hour == alarms[i].hour &&
              timeinfo.tm_min == alarms[i].minute &&
              alarms[i].lastTriggeredYday != timeinfo.tm_yday) {
            alarms[i].lastTriggeredYday = timeinfo.tm_yday;
            String lbl = alarms[i].label;
            char hBuf[12];
            snprintf(hBuf, sizeof(hBuf), "%02d:%02d", alarms[i].hour, alarms[i].minute);

            if (!alarms[i].repeatDaily) {
              alarms[i].active = false;
              saveAlarmsToNVS();
            }

            Serial.printf("⏰ [Jarvis] ALARME DISPARADO (%s): %s\n", hBuf, lbl.c_str());
            audio.playAlarmSound();
            String speech = "Atencao senhor! Sao " + String(hBuf) + ". Alarme programado: " + lbl + ".";
            audio.speakText(tlsClient, speech);
          }
        }
      }
    }
  }

  // ==== LISTA DE LEMBRETES / NOTAS / COMPRAS (SALVA EM NVS) ====
  String addNote(const String& item) {
    if (item.length() == 0) return "Item vazio.";
    if (notesList.length() > 0) notesList += "; ";
    notesList += item;
    prefs.putString("notes", notesList);
    return "Adicionado a sua lista: " + item + ". Lista atual: " + notesList;
  }

  String getNotes() {
    return notesList.length() > 0 ? ("Itens na sua lista: " + notesList) : "Sua lista de lembretes esta vazia.";
  }

  String clearNotes() {
    notesList = "";
    prefs.putString("notes", "");
    return "Lista de lembretes limpa com sucesso.";
  }

  // ==== PREVISAO DO TEMPO (OPEN-METEO API) ====
  String getWeatherReport(WiFiClientSecure& cli) {
    cli.stop();
    cli.setInsecure();

    HTTPClient http;
    String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + WEATHER_LATITUDE +
                 "&longitude=" + WEATHER_LONGITUDE +
                 "&current=temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,wind_speed_10m";
    if (!http.begin(cli, url)) {
      return "Nao foi possivel conectar ao servico de meteorologia.";
    }

    int code = http.GET();
    String resp = http.getString();
    http.end();

    if (code != 200) {
      return "Servico de clima indisponivel no momento.";
    }

    DynamicJsonDocument doc(1536);
    if (deserializeJson(doc, resp)) {
      return "Erro ao processar dados meteorologicos.";
    }

    float temp = doc["current"]["temperature_2m"] | 0.0f;
    float feels = doc["current"]["apparent_temperature"] | 0.0f;
    int hum = doc["current"]["relative_humidity_2m"] | 0;
    float wind = doc["current"]["wind_speed_10m"] | 0.0f;
    float rain = doc["current"]["precipitation"] | 0.0f;

    char buf[220];
    snprintf(buf, sizeof(buf),
             "Temperatura atual de %.1f graus Celsius (sensacao termica de %.1f graus), umidade de %d%%, vento a %.1f km/h e precipitacao de %.1f mm.",
             temp, feels, hum, wind, rain);
    return String(buf);
  }

  // ==== DIAGNOSTICO DO SISTEMA (STATUS DO JARVIS) ====
  String getSystemDiagnostics(AudioManager& audio) {
    uint32_t upSec = millis() / 1000;
    char buf[300];
    snprintf(buf, sizeof(buf),
             "Hardware ESP-WROOM-32 a %u MHz, RAM livre %u KB, WiFi %d dBm (IP %s), uptime %u min, volume estereo %d%%. Alarmes: %s. Timers: %s.",
             getCpuFrequencyMhz(),
             (unsigned)(ESP.getFreeHeap() / 1024),
             WiFi.RSSI(),
             WiFi.localIP().toString().c_str(),
             (unsigned)(upSec / 60),
             audio.volumePercent,
             listAlarms().c_str(),
             listActiveTimers().c_str());
    return String(buf);
  }
};
