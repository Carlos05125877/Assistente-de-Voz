#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config_private.h"
#include "audio_manager.h"
#include "spotify_client.h"
#include "smartlife_tuya.h"
#include "bible_client.h"
#include "jarvis_features.h"

// ============================================================================
// GEMINI AI AGENT COM FUNCTION CALLING NATIVO (TOOLS COMPLETAS)
// ============================================================================
// Integra o Gemini 2.5 Flash com 4 ferramentas completas:
// 1. spotify_control   -> Toca musicas/playlists/artistas, pausa, pula, volume
// 2. bible_app         -> Conecta ao App Biblia (versiculos, capitulos, salmos, temas)
// 3. smartlife_control -> Liga/desliga dispositivos Tuya/SmartLife, cor e brilho
// 4. jarvis_system     -> Alarmes salvos em NVS (HH:MM), Timers, Lembretes, Clima, Volume
// ============================================================================

struct ChatTurn {
  String userText;
  String modelText;
};

class GeminiAgent {
public:
  static const int HISTORY_SIZE = 3;
  ChatTurn history[HISTORY_SIZE];
  int historyCount = 0;

  void addHistory(const String& userMsg, const String& modelMsg) {
    if (historyCount < HISTORY_SIZE) {
      history[historyCount].userText = userMsg;
      history[historyCount].modelText = modelMsg;
      historyCount++;
    } else {
      for (int i = 0; i < HISTORY_SIZE - 1; ++i) {
        history[i] = history[i + 1];
      }
      history[HISTORY_SIZE - 1].userText = userMsg;
      history[HISTORY_SIZE - 1].modelText = modelMsg;
    }
  }

  void clearHistory() {
    historyCount = 0;
  }

  // Lista os dispositivos SmartLife cadastrados para o System Prompt
  String buildDeviceListString() {
    String list = "";
    for (size_t i = 0; i < SMARTLIFE_DEVICE_COUNT; ++i) {
      if (i > 0) list += ", ";
      list += SMARTLIFE_DEVICES[i].friendlyName;
    }
    return list;
  }

  // Processa a fala do usuario com Gemini + Tools (ou delega ao Servidor Externo se configurado)
  bool processUserQuery(WiFiClientSecure& cli,
                        const String& userText,
                        String& outSpokenReply,
                        AudioManager& audio,
                        SpotifyClient& spotify,
                        SmartLifeClient& smartlife,
                        BibleClient& bible,
                        JarvisFeatures& jarvis) {
    outSpokenReply = "";

    // Monta o JSON de requisicao para a API Gemini generateContent
    DynamicJsonDocument reqDoc(7168);

    // 1. System Instruction (Personalidade J.A.R.V.I.S.)
    JsonObject sysInst = reqDoc.createNestedObject("system_instruction");
    JsonArray sysParts = sysInst.createNestedArray("parts");
    String sysPrompt =
      String("Voce e ") + ASSISTANT_NAME +
      ", um assistente de inteligencia artificial completo embarcado em um ESP32 com 2 alto-falantes estereo, inspirado no J.A.R.V.I.S. "
      "Responda SEMPRE em Portugues do Brasil, em texto puro (SEM asteriscos, SEM markdown, SEM emojis), "
      "de forma direta, elegante, prestativa e concisa (1 a 3 frases curtas, ideal para leitura por voz TTS). "
      "Data/Hora atual: " + jarvis.getFormattedDateTime() + ". "
      "Plano de Leitura Ativo no App da Biblia (YouVersion - bible.com/pt/reading-plans): [" + bible.getActivePlanStatus() + "]. "
      "Alarmes salvos: [" + jarvis.listAlarms() + "]. "
      "Dispositivos SmartLife da casa: [" + buildDeviceListString() + "]. "
      "Sempre que o usuario pedir para tocar musica/controlar Spotify, ler o plano de leitura/devocional de hoje, avancar para o proximo dia do plano, "
      "iniciar ou listar planos do App da Biblia (YouVersion / Holy Bible / bible.com/pt/reading-plans), ler o versiculo do dia ou uma passagem biblica, "
      "guardar/listar/remover alarmes despertadores (hora e minuto), criar timers, anotar lembretes, acionar dispositivos da casa, "
      "ver o clima ou ajustar o volume dos alto-falantes, chame a ferramenta (function call) apropriada.";
    sysParts.createNestedObject()["text"] = sysPrompt;

    // 2. Historico de conversa multi-turno
    JsonArray contents = reqDoc.createNestedArray("contents");
    for (int i = 0; i < historyCount; ++i) {
      JsonObject uTurn = contents.createNestedObject();
      uTurn["role"] = "user";
      uTurn.createNestedArray("parts").createNestedObject()["text"] = history[i].userText;

      JsonObject mTurn = contents.createNestedObject();
      mTurn["role"] = "model";
      mTurn.createNestedArray("parts").createNestedObject()["text"] = history[i].modelText;
    }

    // Turno atual do usuario
    JsonObject currUser = contents.createNestedObject();
    currUser["role"] = "user";
    currUser.createNestedArray("parts").createNestedObject()["text"] = userText;

    // 3. Declaracao das Ferramentas (Tools / Function Calling)
    JsonArray tools = reqDoc.createNestedArray("tools");
    JsonArray fnDecls = tools.createNestedObject().createNestedArray("functionDeclarations");

    // Tool 1: spotify_control
    {
      JsonObject fn = fnDecls.createNestedObject();
      fn["name"] = "spotify_control";
      fn["description"] = "Conecta com o Spotify: busca e toca musicas, artistas, albuns ou playlists, pausa, retoma, pula faixa, ajusta volume ou consulta musica atual.";
      JsonObject params = fn.createNestedObject("parameters");
      params["type"] = "OBJECT";
      JsonObject props = params.createNestedObject("properties");

      JsonObject pAct = props.createNestedObject("action");
      pAct["type"] = "STRING";
      pAct["description"] = "Acao: play_query, pause, resume, next, previous, volume, now_playing";

      JsonObject pQuery = props.createNestedObject("query");
      pQuery["type"] = "STRING";
      pQuery["description"] = "Nome da musica, artista ou playlist para buscar (quando action=play_query)";

      JsonObject pType = props.createNestedObject("search_type");
      pType["type"] = "STRING";
      pType["description"] = "Tipo de busca: track, artist, playlist ou album";

      JsonObject pVol = props.createNestedObject("volume");
      pVol["type"] = "INTEGER";
      pVol["description"] = "Volume do Spotify de 0 a 100 (opcional)";

      JsonArray reqArr = params.createNestedArray("required");
      reqArr.add("action");
    }

    // Tool 2: bible_app (YouVersion / Holy Bible — https://www.bible.com/pt/reading-plans)
    {
      JsonObject fn = fnDecls.createNestedObject();
      fn["name"] = "bible_app";
      fn["description"] = "Conecta com o App da Biblia Oficial (Holy Bible / YouVersion - bible.com/pt/reading-plans): le o Devocional e Plano de Leitura do dia atual, avanca para o proximo dia do plano, inicia planos de leitura por tema (ansiedade, paz, sabedoria, esperanca, gratidao) ou ID do bible.com, lista planos, le o Versiculo do Dia oficial ou qualquer passagem biblica.";
      JsonObject params = fn.createNestedObject("parameters");
      params["type"] = "OBJECT";
      JsonObject props = params.createNestedObject("properties");

      JsonObject pAct = props.createNestedObject("action");
      pAct["type"] = "STRING";
      pAct["description"] = "Acao: read_plan_day (le o devocional e escritura de hoje do plano ativo), next_plan_day (conclui o dia atual e avanca p/ o proximo dia do plano), start_plan (inicia um plano de leitura do YouVersion por tema ou nome), list_plans (lista planos de bible.com/pt/reading-plans), verse_of_day (Versiculo do Dia oficial do YouVersion), get_passage (le um livro/capitulo/versiculo especifico)";

      JsonObject pRef = props.createNestedObject("reference");
      pRef["type"] = "STRING";
      pRef["description"] = "Referencia biblica em portugues quando action=get_passage (ex: Salmos 23:1-4, Joao 3:16, Filipenses 4:6-7, Romanos 8:28)";

      JsonObject pThm = props.createNestedObject("theme");
      pThm["type"] = "STRING";
      pThm["description"] = "Tema ou nome/slug do Plano de Leitura em bible.com/pt/reading-plans quando action=start_plan (ex: ansiedade, paz, sabedoria, proverbios, esperanca, fe, gratidao)";

      JsonObject pDay = props.createNestedObject("plan_day");
      pDay["type"] = "INTEGER";
      pDay["description"] = "Numero do dia do plano de leitura (ex: 1, 2, 3...) caso o usuario peca um dia especifico do plano";

      JsonArray reqArr = params.createNestedArray("required");
      reqArr.add("action");
    }

    // Tool 3: smartlife_control
    {
      JsonObject fn = fnDecls.createNestedObject();
      fn["name"] = "smartlife_control";
      fn["description"] = "Controla lampadas, fitas LED, ar-condicionado e tomadas inteligentes SmartLife / Tuya.";
      JsonObject params = fn.createNestedObject("parameters");
      params["type"] = "OBJECT";
      JsonObject props = params.createNestedObject("properties");

      JsonObject pDev = props.createNestedObject("device_name");
      pDev["type"] = "STRING";
      pDev["description"] = "Nome do dispositivo (ex: luz da sala, luz do quarto, fita led, ar condicionado, ventilador, cafeteira)";

      JsonObject pSt = props.createNestedObject("state");
      pSt["type"] = "STRING";
      pSt["description"] = "Estado desejado: on ou off";

      JsonObject pBr = props.createNestedObject("brightness");
      pBr["type"] = "INTEGER";
      pBr["description"] = "Brilho de 1 a 100 (opcional)";

      JsonObject pCol = props.createNestedObject("color");
      pCol["type"] = "STRING";
      pCol["description"] = "Cor da luz em portugues: vermelho, azul, verde, amarelo, roxo, rosa, laranja, ciano, branco, quente (opcional)";

      JsonArray reqArr = params.createNestedArray("required");
      reqArr.add("device_name");
      reqArr.add("state");
    }

    // Tool 4: jarvis_system (Inclui Alarmes Persistentes HH:MM, Timers, Notas, Clima e Volume)
    {
      JsonObject fn = fnDecls.createNestedObject();
      fn["name"] = "jarvis_system";
      fn["description"] = "Gerencia alarmes despertadores salvos na memoria (hora:minuto), timers regressivos, lista de lembretes, previsao do tempo, volume estereo do ESP32 e status.";
      JsonObject params = fn.createNestedObject("parameters");
      params["type"] = "OBJECT";
      JsonObject props = params.createNestedObject("properties");

      JsonObject pAct = props.createNestedObject("action");
      pAct["type"] = "STRING";
      pAct["description"] = "Acao: set_alarm, list_alarms, remove_alarm, clear_alarms, set_timer, cancel_timers, list_timers, add_note, get_notes, clear_notes, get_weather, set_speaker_volume, system_status";

      JsonObject pHr = props.createNestedObject("alarm_hour");
      pHr["type"] = "INTEGER";
      pHr["description"] = "Hora do alarme despertador de 0 a 23 (para set_alarm ou remove_alarm)";

      JsonObject pMin = props.createNestedObject("alarm_minute");
      pMin["type"] = "INTEGER";
      pMin["description"] = "Minuto do alarme despertador de 0 a 59 (para set_alarm ou remove_alarm)";

      JsonObject pRep = props.createNestedObject("alarm_repeat_daily");
      pRep["type"] = "BOOLEAN";
      pRep["description"] = "True se o alarme deve tocar todos os dias, False para tocar apenas uma vez";

      JsonObject pSec = props.createNestedObject("timer_seconds");
      pSec["type"] = "INTEGER";
      pSec["description"] = "Duracao do timer regressivo em segundos (para set_timer)";

      JsonObject pLbl = props.createNestedObject("label");
      pLbl["type"] = "STRING";
      pLbl["description"] = "Nome/rotulo do alarme, nome do timer ou texto do lembrete/nota";

      JsonObject pVol = props.createNestedObject("volume");
      pVol["type"] = "INTEGER";
      pVol["description"] = "Volume dos 2 alto-falantes do ESP32 de 0 a 100 (para set_speaker_volume)";

      JsonArray reqArr = params.createNestedArray("required");
      reqArr.add("action");
    }

    // 4. Generation Config
    JsonObject gen = reqDoc.createNestedObject("generationConfig");
    gen["temperature"] = 0.4;
    gen["maxOutputTokens"] = 260;

    String requestBody;
    serializeJson(reqDoc, requestBody);

    // Envia para a API do Gemini e libera o reqDoc imediatamente para economizar ~7KB de Heap!
    reqDoc.clear();
    cli.stop();
    cli.setInsecure();
    cli.setTimeout(25000);

    HTTPClient http;
    String url = String("https://generativelanguage.googleapis.com/v1beta/models/") +
                 GEMINI_MODEL + ":generateContent?key=" + GEMINI_API_KEY;
    if (!http.begin(cli, url)) {
      Serial.println("❌ [Gemini] Falha ao iniciar HTTPClient");
      return false;
    }

    http.addHeader("Content-Type", "application/json");
    uint32_t t0 = millis();
    int httpCode = http.POST(requestBody);
    String respBody = http.getString();
    http.end();

    Serial.printf("⏱️ [Gemini] Resposta HTTP %d em %ums\n", httpCode, millis() - t0);

    if (httpCode != 200) {
      Serial.printf("❌ [Gemini] Erro API: %s\n", respBody.substring(0, 400).c_str());
      return false;
    }

    DynamicJsonDocument respDoc(6144);
    if (deserializeJson(respDoc, respBody)) {
      Serial.println("❌ [Gemini] Erro ao decodificar JSON de resposta");
      return false;
    }

    JsonArray parts = respDoc["candidates"][0]["content"]["parts"].as<JsonArray>();
    if (parts.isNull()) {
      return false;
    }

    String directText = "";
    String toolResultSummary = "";
    String calledToolName = "";
    bool isDirectReadoutTool = false;

    for (JsonObject part : parts) {
      if (part.containsKey("text")) {
        directText += part["text"].as<String>();
      }
      if (part.containsKey("functionCall")) {
        JsonObject fnCall = part["functionCall"];
        calledToolName = fnCall["name"] | "";
        JsonObject args = fnCall["args"];

        Serial.printf("🛠️ [Gemini Tool Call] Executando: %s\n", calledToolName.c_str());

        if (calledToolName == "spotify_control") {
          String action = args["action"] | "play_query";
          String query  = args["query"] | "";
          String sType  = args["search_type"] | "track";
          int vol       = args.containsKey("volume") ? (int)args["volume"] : -1;
          toolResultSummary = spotify.executeCommand(cli, action, query, sType, vol);
          audio.playSuccessBeep();
          if (action == "now_playing") isDirectReadoutTool = true;
        }
        else if (calledToolName == "bible_app") {
          String action = args["action"] | "read_plan_day";
          String ref    = args["reference"] | "";
          String theme  = args["theme"] | "";
          int planDay   = args["plan_day"] | 0;
          toolResultSummary = bible.executeCommand(cli, action, ref, theme, planDay);
          audio.playSuccessBeep();
          isDirectReadoutTool = true;
        }
        else if (calledToolName == "smartlife_control") {
          String devName = args["device_name"] | "";
          String state   = args["state"] | "on";
          int bright     = args.containsKey("brightness") ? (int)args["brightness"] : -1;
          String color   = args["color"] | "";
          toolResultSummary = smartlife.controlDevice(cli, devName, state, bright, color);
          audio.playSuccessBeep();
        }
        else if (calledToolName == "jarvis_system") {
          String action = args["action"] | "";
          int alarmHr   = args.containsKey("alarm_hour") ? (int)args["alarm_hour"] : -1;
          int alarmMin  = args.containsKey("alarm_minute") ? (int)args["alarm_minute"] : 0;
          bool alarmRep = args.containsKey("alarm_repeat_daily") ? (bool)args["alarm_repeat_daily"] : true;
          int timerSec  = args["timer_seconds"] | 0;
          String label  = args["label"] | "";
          int vol       = args.containsKey("volume") ? (int)args["volume"] : -1;

          if (action == "set_alarm") {
            toolResultSummary = jarvis.addAlarm(alarmHr, alarmMin, label, alarmRep);
            audio.playSuccessBeep();
          } else if (action == "list_alarms") {
            toolResultSummary = "Alarmes salvos: " + jarvis.listAlarms();
            isDirectReadoutTool = true;
          } else if (action == "remove_alarm") {
            toolResultSummary = jarvis.removeAlarm(alarmHr, alarmMin, label);
            audio.playSuccessBeep();
          } else if (action == "clear_alarms") {
            toolResultSummary = jarvis.clearAlarms();
            audio.playSuccessBeep();
          } else if (action == "set_timer") {
            toolResultSummary = jarvis.addTimer(timerSec, label);
            audio.playSuccessBeep();
          } else if (action == "cancel_timers") {
            toolResultSummary = jarvis.clearTimers();
          } else if (action == "list_timers") {
            toolResultSummary = jarvis.listActiveTimers();
            isDirectReadoutTool = true;
          } else if (action == "add_note") {
            toolResultSummary = jarvis.addNote(label);
            audio.playSuccessBeep();
          } else if (action == "get_notes") {
            toolResultSummary = jarvis.getNotes();
            isDirectReadoutTool = true;
          } else if (action == "clear_notes") {
            toolResultSummary = jarvis.clearNotes();
          } else if (action == "get_weather") {
            toolResultSummary = jarvis.getWeatherReport(cli);
            isDirectReadoutTool = true;
          } else if (action == "set_speaker_volume" && vol >= 0) {
            audio.setVolume(vol);
            audio.playSuccessBeep();
            toolResultSummary = "Volume dos alto-falantes do Jarvis ajustado para " + String(vol) + " por cento.";
          } else if (action == "system_status") {
            toolResultSummary = jarvis.getSystemDiagnostics(audio);
            isDirectReadoutTool = true;
          }
        }
      }
    }

    if (isDirectReadoutTool && toolResultSummary.length() > 0) {
      outSpokenReply = toolResultSummary;
    } else if (directText.length() > 0 && toolResultSummary.length() > 0) {
      outSpokenReply = directText + " " + toolResultSummary;
    } else if (toolResultSummary.length() > 0) {
      outSpokenReply = "Certo senhor! " + toolResultSummary;
    } else {
      outSpokenReply = directText;
    }

    // Remove caracteres markdown residuais para o TTS falar limpo
    outSpokenReply.replace("*", "");
    outSpokenReply.replace("#", "");
    outSpokenReply.replace("`", "");
    outSpokenReply.trim();

    if (outSpokenReply.length() > 0) {
      addHistory(userText, outSpokenReply);
      return true;
    }
    return false;
  }
};
