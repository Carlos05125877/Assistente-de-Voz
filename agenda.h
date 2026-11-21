#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#include "config.h"

// ============================================================================
// AGENDA LOCAL — o ESP32 e o dono de alarmes, lembretes, timers e horarios das rotinas.
// Fica tudo na NVS (sobrevive a quedas de energia) e dispara na hora exata, mesmo sem
// internet. O Netlify so recebe uma copia no sync, para mostrar no painel e responder
// perguntas como "quais meus alarmes?".
// ============================================================================
struct Alarm    { uint8_t h, m, rep; char label[41]; };
struct Reminder { uint16_t id; uint32_t at; uint8_t rep; char text[111]; };   // rep: 0 uma vez, 1 diario, 2 seg-sex, 3 semanal
struct Timer    { uint32_t endMs; char label[31]; };
struct Routine  { char id[17]; uint8_t h, m, days; };                          // days: bit0 = segunda ... bit6 = domingo

enum EventKind { EV_NONE, EV_ALARM, EV_REMINDER, EV_TIMER, EV_ROUTINE };
struct Event { EventKind kind = EV_NONE; char text[111] = ""; bool briefing = false; };

static const char* REP_NAMES[] = {"none", "daily", "weekdays", "weekly"};

class Agenda {
public:
  Alarm al[MAX_ALARMS];       int nAl = 0;
  Reminder rem[MAX_REMINDERS]; int nRem = 0;
  Timer tm[MAX_TIMERS];       int nTm = 0;
  Routine rt[MAX_ROUTINES];   int nRt = 0;
  uint32_t cfgVersion = 0;
  String groqKey, sttModel;
  bool dirty = false;         // mudou algo: sincronizar logo

  void begin(Preferences* p) {
    prefs = p;
    nAl = loadBlob("al", al, sizeof(al), sizeof(Alarm));
    nRem = loadBlob("rem", rem, sizeof(rem), sizeof(Reminder));
    nRt = loadBlob("rt", rt, sizeof(rt), sizeof(Routine));
    cfgVersion = prefs->getUInt("cv", 0);
    groqKey = prefs->getString("gk", GROQ_API_KEY);
    sttModel = prefs->getString("sm", GROQ_STT_MODEL_DEFAULT);
    nextRemId = prefs->getUInt("rid", 1);
  }

  static bool timeValid() { return time(nullptr) > 1700000000; }

  // ---------------------------------------------------------------- acoes (do Netlify ou do painel)
  /** Aplica uma acao. Retorna true se ela era da agenda. */
  bool apply(JsonObject a) {
    const char* t = a["t"] | "";
    if (!strcmp(t, "alarm_add")) {
      int h = a["h"] | -1, m = a["m"] | 0;
      if (h < 0 || h > 23 || m < 0 || m > 59) return true;
      int i = findAlarm(h, m);
      if (i < 0) { if (nAl >= MAX_ALARMS) return true; i = nAl++; }
      al[i].h = h; al[i].m = m; al[i].rep = a["r"] | 1;
      strlcpy(al[i].label, a["l"] | "Despertador", sizeof(al[i].label));
      saveAlarms();
    } else if (!strcmp(t, "alarm_del")) {
      int h = a["h"] | -1, m = a["m"] | -1;
      for (int i = nAl - 1; i >= 0; --i) if (al[i].h == h && (m < 0 || al[i].m == m)) removeAt(al, nAl, i);
      saveAlarms();
    } else if (!strcmp(t, "alarm_clear")) {
      nAl = 0; saveAlarms();
    } else if (!strcmp(t, "rem_add")) {
      if (nRem >= MAX_REMINDERS) dropOldestReminder();
      Reminder& r = rem[nRem++];
      r.id = nextRemId++;
      r.at = a["at"] | 0;
      r.rep = repCode(a["rep"] | "none");
      strlcpy(r.text, a["x"] | "lembrete", sizeof(r.text));
      prefs->putUInt("rid", nextRemId);
      saveReminders();
    } else if (!strcmp(t, "rem_del")) {
      int id = a["id"] | -1;
      for (int i = nRem - 1; i >= 0; --i) if (rem[i].id == id) removeAt(rem, nRem, i);
      saveReminders();
    } else if (!strcmp(t, "timer")) {
      if (nTm >= MAX_TIMERS) removeAt(tm, nTm, 0);
      Timer& x = tm[nTm++];
      x.endMs = millis() + (uint32_t)(a["s"] | 60) * 1000UL;
      strlcpy(x.label, a["l"] | "timer", sizeof(x.label));
      dirty = true;
    } else if (!strcmp(t, "timer_clear")) {
      nTm = 0; dirty = true;
    } else return false;
    return true;
  }

  /** Configuracao vinda do sync (chave do Groq, modelo do Whisper, horarios das rotinas). */
  void applyConfig(JsonObject cfg) {
    cfgVersion = cfg["v"] | 0;
    prefs->putUInt("cv", cfgVersion);
    const char* gk = cfg["gk"] | "";
    if (strlen(gk) > 10 && groqKey != gk) { groqKey = gk; prefs->putString("gk", groqKey); }
    const char* sm = cfg["sm"] | "";
    if (strlen(sm) && sttModel != sm) { sttModel = sm; prefs->putString("sm", sttModel); }
    nRt = 0;
    for (JsonArray r : cfg["rt"].as<JsonArray>()) {
      if (nRt >= MAX_ROUTINES) break;
      strlcpy(rt[nRt].id, r[0] | "", sizeof(rt[nRt].id));
      rt[nRt].h = r[1] | 0; rt[nRt].m = r[2] | 0; rt[nRt].days = r[3] | 127;
      nRt++;
    }
    prefs->putBytes("rt", rt, nRt * sizeof(Routine));
    Serial.printf("⚙️ Config v%u: %d rotinas com horário\n", (unsigned)cfgVersion, nRt);
  }

  /** Estado compacto para o Netlify. */
  void toJson(JsonObject st) {
    JsonArray a = st.createNestedArray("al");
    for (int i = 0; i < nAl; ++i) { JsonArray x = a.createNestedArray(); x.add(al[i].h); x.add(al[i].m); x.add(al[i].rep); x.add((const char*)al[i].label); }
    JsonArray r = st.createNestedArray("rem");
    for (int i = 0; i < nRem; ++i) { JsonArray x = r.createNestedArray(); x.add(rem[i].id); x.add(rem[i].at); x.add(REP_NAMES[rem[i].rep & 3]); x.add((const char*)rem[i].text); }
    JsonArray t = st.createNestedArray("tm");
    for (int i = 0; i < nTm; ++i) { JsonArray x = t.createNestedArray(); x.add((const char*)tm[i].label); x.add((long)(tm[i].endMs - millis()) / 1000); }
    st["cv"] = cfgVersion;
  }

  // ---------------------------------------------------------------- relogio
  /** Chamado ~1x por segundo. Retorna o proximo evento que deve tocar agora. */
  Event tick() {
    Event ev;
    for (int i = 0; i < nTm; ++i) {
      if ((int32_t)(millis() - tm[i].endMs) >= 0) {
        ev.kind = EV_TIMER;
        snprintf(ev.text, sizeof(ev.text), "%s", tm[i].label);
        removeAt(tm, nTm, i); dirty = true;
        return ev;
      }
    }
    if (!timeValid()) return ev;
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    const int wd = (t.tm_wday + 6) % 7;                    // 0 = segunda
    const uint32_t minuteKey = (uint32_t)(now / 60);
    if (minuteKey != lastMinute) {
      lastMinute = minuteKey;
      for (int i = 0; i < nAl; ++i) {
        if (al[i].h == t.tm_hour && al[i].m == t.tm_min) {
          ev.kind = EV_ALARM;
          snprintf(ev.text, sizeof(ev.text), "%s", al[i].label);
          String l = al[i].label; l.toLowerCase();
          ev.briefing = l.indexOf("devocional") >= 0 || l.indexOf("biblia") >= 0 || l.indexOf("bíblia") >= 0 || l.indexOf("briefing") >= 0;
          if (!al[i].rep) { removeAt(al, nAl, i); saveAlarms(); }
          pendingRoutineCheck = true;
          return ev;
        }
      }
      pendingRoutineCheck = true;
    }
    if (pendingRoutineCheck) {
      pendingRoutineCheck = false;
      for (int i = 0; i < nRt; ++i) {
        if (rt[i].h == t.tm_hour && rt[i].m == t.tm_min && (rt[i].days & (1 << wd))) {
          ev.kind = EV_ROUTINE;
          snprintf(ev.text, sizeof(ev.text), "%s", rt[i].id);
          return ev;
        }
      }
    }
    for (int i = 0; i < nRem; ++i) {
      if (rem[i].at && (uint32_t)now >= rem[i].at) {
        const bool late = (uint32_t)now - rem[i].at > 6 * 3600;   // ESP desligado: nao anuncia coisa muito velha
        if (!late) { ev.kind = EV_REMINDER; snprintf(ev.text, sizeof(ev.text), "%s", rem[i].text); }
        reschedule(i, now);
        saveReminders();
        if (!late) return ev;
        break;
      }
    }
    return ev;
  }

private:
  Preferences* prefs = nullptr;
  uint32_t lastMinute = 0;
  bool pendingRoutineCheck = false;
  uint32_t nextRemId = 1;

  template <class T> static void removeAt(T* arr, int& n, int i) { for (int k = i; k < n - 1; ++k) arr[k] = arr[k + 1]; n--; }
  int loadBlob(const char* key, void* dst, size_t cap, size_t item) {
    size_t len = prefs->getBytesLength(key);
    if (len == 0 || len > cap || len % item) return 0;
    prefs->getBytes(key, dst, len);
    return len / item;
  }
  int findAlarm(int h, int m) { for (int i = 0; i < nAl; ++i) if (al[i].h == h && al[i].m == m) return i; return -1; }
  void saveAlarms() { prefs->putBytes("al", al, nAl * sizeof(Alarm)); dirty = true; }
  void saveReminders() { prefs->putBytes("rem", rem, nRem * sizeof(Reminder)); dirty = true; }
  static uint8_t repCode(const char* s) { for (int i = 0; i < 4; ++i) if (!strcmp(s, REP_NAMES[i])) return i; return 0; }
  void dropOldestReminder() { int o = 0; for (int i = 1; i < nRem; ++i) if (rem[i].at < rem[o].at) o = i; removeAt(rem, nRem, o); }
  void reschedule(int i, time_t now) {
    Reminder& r = rem[i];
    if (r.rep == 0) { removeAt(rem, nRem, i); return; }
    do {
      r.at += (r.rep == 3 ? 7 : 1) * 86400UL;
      if (r.rep == 2) {                                   // pula sabado e domingo
        time_t x = r.at; struct tm t; localtime_r(&x, &t);
        if (t.tm_wday == 6) r.at += 2 * 86400UL; else if (t.tm_wday == 0) r.at += 86400UL;
      }
    } while (r.at <= (uint32_t)now);
  }
};
