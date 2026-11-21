#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "mbedtls/base64.h"
#include "config_private.h"

// ============================================================================
// SPOTIFY WEB API CLIENT PARA ESP32 (JARVIS) + SUPORTE A SERVIDOR EXTERNO
// ============================================================================
// Funcionalidades:
// - Se EXTERNAL_SERVER_URL estiver configurado, delega ao Jarvis Bridge Server
//   em HTTP local ultrarrapido (aliviando TLS e RAM do ESP32) E permite streaming
//   de musica/preview direto nos 2 alto-falantes do ESP32!
// - Se EXTERNAL_SERVER_URL estiver vazio, conecta diretamente na API HTTPS do Spotify
//   com renovacao automatica do Access Token via OAuth2 Refresh Token
// - Busca inteligente por musica, artista, album ou playlist e reproducao imediata
// - Controles de Play, Pause, Proxima faixa (Next), Faixa anterior (Prev), Volume
// ============================================================================

class SpotifyClient {
public:
  String accessToken = "";
  uint32_t tokenExpiryMillis = 0;
  String lastTrackInfo = "Nenhuma musica consultada";

  bool hasExternalServer() {
    return strlen(EXTERNAL_SERVER_URL) > 8;
  }

  bool isConfigured() {
    if (hasExternalServer()) return true;
    if (!SPOTIFY_ENABLED) return false;
    String cid = SPOTIFY_CLIENT_ID;
    String rt  = SPOTIFY_REFRESH_TOKEN;
    return (cid.length() > 10 && !cid.startsWith("SEU_") &&
            rt.length() > 10  && !rt.startsWith("SEU_"));
  }

  // Codifica URL para query de busca
  static String urlEncode(const String& str) {
    String encoded = "";
    char c;
    char bufHex[4];
    for (size_t i = 0; i < str.length(); i++) {
      c = str.charAt(i);
      if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
        encoded += c;
      } else if (c == ' ') {
        encoded += "%20";
      } else {
        snprintf(bufHex, sizeof(bufHex), "%%%02X", (uint8_t)c);
        encoded += bufHex;
      }
    }
    return encoded;
  }

  // Garante que temos um Access Token valido
  bool ensureAccessToken(WiFiClientSecure& cli) {
    if (!isConfigured()) {
      Serial.println("⚠️ [Spotify] Credenciais Spotify nao configuradas em config_private.h");
      return false;
    }
    if (accessToken.length() > 0 && millis() < tokenExpiryMillis) {
      return true;
    }

    Serial.println("🔑 [Spotify] Renovando Access Token via OAuth2 Refresh Token...");
    cli.stop();
    cli.setInsecure();

    HTTPClient http;
    if (!http.begin(cli, "https://accounts.spotify.com/api/token")) {
      return false;
    }

    String authRaw = String(SPOTIFY_CLIENT_ID) + ":" + String(SPOTIFY_CLIENT_SECRET);
    unsigned char b64Auth[256];
    size_t b64Len = 0;
    mbedtls_base64_encode(b64Auth, sizeof(b64Auth), &b64Len,
                          (const unsigned char*)authRaw.c_str(), authRaw.length());
    b64Auth[b64Len] = '\0';

    http.addHeader("Authorization", String("Basic ") + (char*)b64Auth);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String body = String("grant_type=refresh_token&refresh_token=") + SPOTIFY_REFRESH_TOKEN;
    int code = http.POST(body);
    String resp = http.getString();
    http.end();

    if (code != 200) {
      Serial.printf("❌ [Spotify] Erro OAuth (%d): %s\n", code, resp.c_str());
      return false;
    }

    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, resp)) return false;

    accessToken = doc["access_token"] | "";
    int expiresIn = doc["expires_in"] | 3600;
    tokenExpiryMillis = millis() + (uint32_t)(expiresIn - 120) * 1000UL;

    Serial.println("✅ [Spotify] Access Token renovado com sucesso!");
    return accessToken.length() > 0;
  }

  int sendPlayerRequest(WiFiClientSecure& cli, const char* method, const String& endpoint,
                        const String& jsonPayload = "", String* outResponse = nullptr) {
    if (!ensureAccessToken(cli)) return -1;

    cli.stop();
    cli.setInsecure();

    HTTPClient http;
    String url = String("https://api.spotify.com/v1") + endpoint;
    if (!http.begin(cli, url)) return -1;

    http.addHeader("Authorization", String("Bearer ") + accessToken);
    http.addHeader("Content-Type", "application/json");

    int code = -1;
    if (strcmp(method, "GET") == 0) {
      code = http.GET();
    } else if (strcmp(method, "POST") == 0) {
      code = http.POST(jsonPayload);
    } else if (strcmp(method, "PUT") == 0) {
      code = http.PUT(jsonPayload);
    }

    if (outResponse && code > 0) {
      *outResponse = http.getString();
    }
    http.end();
    return code;
  }

  bool play(WiFiClientSecure& cli) {
    String ep = "/me/player/play";
    if (strlen(SPOTIFY_DEFAULT_DEVICE) > 0) {
      ep += String("?device_id=") + SPOTIFY_DEFAULT_DEVICE;
    }
    int code = sendPlayerRequest(cli, "PUT", ep, "{}");
    return (code == 200 || code == 204);
  }

  bool pause(WiFiClientSecure& cli) {
    int code = sendPlayerRequest(cli, "PUT", "/me/player/pause", "{}");
    return (code == 200 || code == 204);
  }

  bool nextTrack(WiFiClientSecure& cli) {
    int code = sendPlayerRequest(cli, "POST", "/me/player/next", "{}");
    return (code == 200 || code == 204);
  }

  bool previousTrack(WiFiClientSecure& cli) {
    int code = sendPlayerRequest(cli, "POST", "/me/player/previous", "{}");
    return (code == 200 || code == 204);
  }

  bool setVolume(WiFiClientSecure& cli, int volumePct) {
    volumePct = constrain(volumePct, 0, 100);
    String ep = String("/me/player/volume?volume_percent=") + String(volumePct);
    int code = sendPlayerRequest(cli, "PUT", ep, "{}");
    return (code == 200 || code == 204);
  }

  // Busca uma musica, artista ou playlist e toca imediatamente
  bool searchAndPlay(WiFiClientSecure& cli, const String& query, const String& searchType, String& outPlayedName) {
    String type = (searchType == "playlist" || searchType == "artist" || searchType == "album")
                  ? searchType : "track";
    String ep = String("/search?q=") + urlEncode(query) + "&type=" + type + "&limit=1&market=BR";
    String resp;
    int code = sendPlayerRequest(cli, "GET", ep, "", &resp);
    if (code != 200) {
      Serial.printf("❌ [Spotify] Falha na busca (%d)\n", code);
      return false;
    }

    // Filtra apenas os campos necessarios para economizar RAM no ESP-WROOM-32
    StaticJsonDocument<512> filter;
    filter["tracks"]["items"][0]["uri"] = true;
    filter["tracks"]["items"][0]["name"] = true;
    filter["tracks"]["items"][0]["artists"][0]["name"] = true;
    filter["playlists"]["items"][0]["uri"] = true;
    filter["playlists"]["items"][0]["name"] = true;
    filter["artists"]["items"][0]["uri"] = true;
    filter["artists"]["items"][0]["name"] = true;
    filter["albums"]["items"][0]["uri"] = true;
    filter["albums"]["items"][0]["name"] = true;

    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, resp, DeserializationOption::Filter(filter))) {
      return false;
    }

    String uri = "";
    String playBody = "";

    if (type == "track") {
      uri = doc["tracks"]["items"][0]["uri"] | "";
      String trackName = doc["tracks"]["items"][0]["name"] | "";
      String artistName = doc["tracks"]["items"][0]["artists"][0]["name"] | "";
      if (uri.length() == 0) return false;
      outPlayedName = trackName + " de " + artistName;
      playBody = String("{\"uris\":[\"") + uri + "\"]}";
    } else {
      String key = type + "s";
      uri = doc[key]["items"][0]["uri"] | "";
      outPlayedName = doc[key]["items"][0]["name"] | query;
      if (uri.length() == 0) return false;
      playBody = String("{\"context_uri\":\"") + uri + "\"}";
    }

    lastTrackInfo = outPlayedName;
    String playEp = "/me/player/play";
    if (strlen(SPOTIFY_DEFAULT_DEVICE) > 0) {
      playEp += String("?device_id=") + SPOTIFY_DEFAULT_DEVICE;
    }
    int playCode = sendPlayerRequest(cli, "PUT", playEp, playBody);
    Serial.printf("🎵 [Spotify] Tocando: %s (URI: %s, HTTP %d)\n",
                  outPlayedName.c_str(), uri.c_str(), playCode);
    return (playCode == 200 || playCode == 204);
  }

  // Retorna o que esta tocando agora no Spotify
  bool getCurrentlyPlaying(WiFiClientSecure& cli, String& outStatus) {
    String resp;
    int code = sendPlayerRequest(cli, "GET", "/me/player/currently-playing", "", &resp);
    if (code == 204 || resp.length() == 0) {
      outStatus = "Nada esta tocando no Spotify no momento.";
      return true;
    }
    if (code != 200) return false;

    StaticJsonDocument<384> filter;
    filter["is_playing"] = true;
    filter["item"]["name"] = true;
    filter["item"]["artists"][0]["name"] = true;

    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, resp, DeserializationOption::Filter(filter))) return false;

    String name = doc["item"]["name"] | "Desconhecida";
    String artist = doc["item"]["artists"][0]["name"] | "Artista desconhecido";
    bool playing = doc["is_playing"] | false;

    lastTrackInfo = name + " - " + artist;
    outStatus = (playing ? "Tocando agora: " : "Pausado em: ") + name + ", de " + artist;
    return true;
  }

  // Dispatcher unificado para o Function Calling do Gemini (usa Servidor Externo se configurado)
  String executeCommand(WiFiClientSecure& cli, const String& action,
                        const String& query, const String& searchType, int volume) {
    // 1. Se o Servidor Externo Jarvis Bridge estiver ativo, delega via HTTP leve!
    if (hasExternalServer()) {
      HTTPClient http;
      String url = String(EXTERNAL_SERVER_URL) + "/api/spotify?action=" + urlEncode(action) +
                   "&q=" + urlEncode(query) + "&type=" + urlEncode(searchType) +
                   "&volume=" + String(volume);
      if (http.begin(url)) {
        http.setTimeout(8000);
        int code = http.GET();
        if (code == 200) {
          String resp = http.getString();
          http.end();
          DynamicJsonDocument doc(1024);
          if (!deserializeJson(doc, resp)) {
            String msg = doc["message"] | "Comando Spotify executado no servidor externo.";
            String track = doc["track"] | "";
            if (track.length() > 0) lastTrackInfo = track;
            return msg;
          }
        }
        http.end();
      }
    }

    // 2. Execucao direta no ESP32
    if (!isConfigured()) {
      return "Spotify ainda nao configurado com Client ID e Refresh Token em config_private.h (ou configure EXTERNAL_SERVER_URL).";
    }

    if (volume >= 0) {
      setVolume(cli, volume);
    }

    if (action == "play_query" && query.length() > 0) {
      String played;
      if (searchAndPlay(cli, query, searchType, played)) {
        return "Reproduzindo no Spotify: " + played;
      }
      return "Nao foi possivel encontrar ou iniciar '" + query + "' no Spotify. Verifique se ha um dispositivo Spotify aberto.";
    } else if (action == "pause") {
      return pause(cli) ? "Spotify pausado." : "Falha ao pausar o Spotify.";
    } else if (action == "resume" || action == "play") {
      return play(cli) ? "Reproducao do Spotify retomada." : "Falha ao retomar o Spotify.";
    } else if (action == "next") {
      return nextTrack(cli) ? "Pulou para a proxima musica no Spotify." : "Falha ao pular musica.";
    } else if (action == "previous") {
      return previousTrack(cli) ? "Voltou para a musica anterior no Spotify." : "Falha ao voltar musica.";
    } else if (action == "volume" && volume >= 0) {
      return "Volume do Spotify ajustado para " + String(volume) + "%.";
    } else if (action == "now_playing") {
      String info;
      if (getCurrentlyPlaying(cli, info)) return info;
      return "Nao foi possivel consultar a musica atual no Spotify.";
    }
    return "Comando do Spotify executado.";
  }
};
