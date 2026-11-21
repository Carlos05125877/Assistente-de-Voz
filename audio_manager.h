#pragma once

#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "driver/i2s.h"
#include "mbedtls/base64.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include <math.h>
#include "config_private.h"

// ============================================================================
// AUDIO MANAGER — OTIMIZADO PARA ESP-WROOM-32 (2 ALTO-FALANTES + 1 MICROFONE)
// ============================================================================
// Diferenciais em relacao ao codigo de referencia:
// 1. Funciona em ESP-WROOM-32 SEM PSRAM (usa buffers compactos em SRAM com DMA).
// 2. Saida I2S True Stereo (Canal Esquerdo + Canal Direito) para 2 Alto-falantes
//    com controle de volume digital, balanco L/R e sintetizador de efeitos Sci-Fi.
// 3. VAD (Voice Activity Detection): detecta inicio e fim da fala automaticamente,
//    encerrando a gravacao assim que voce termina de falar (muito mais rapido!).
// 4. TTS Zero-Copy Streaming: decodifica o audio Base64 direto do socket TCP para
//    o DMA do I2S sem carregar o arquivo inteiro na RAM (permite falas longas!).
// ============================================================================

// Audio "pendente" no Bridge Server: quando o BibleClient recebe um devocional longo,
// o ESP32 guarda so um resumo e o texto COMPLETO e falado via /api/tts?key=<chave>.
// So e usado se o texto a falar comecar com g_bridgeTtsPrefix (evita tocar audio errado).
static String g_bridgeTtsKey = "";
static String g_bridgeTtsPrefix = "";

class AudioManager {
public:
  int volumePercent = DEFAULT_VOLUME_PCT; // 0 a 100
  float balanceLeft = 1.0f;               // Balanco alto-falante esquerdo
  float balanceRight = 1.0f;              // Balanco alto-falante direito
  int vadThreshold = VAD_RMS_THRESHOLD;

  // Buffers compactos (alocados em SRAM ou PSRAM se disponivel)
  static const size_t MIC_READ_SAMPLES = 512;
  static const size_t B64_CHUNK_RAW    = 768; // Multiplo de 3 para Base64 continuo
  static const size_t SPK_FRAME_COUNT  = 512; // 512 frames estereo (L+R)

  int32_t* micRawBuf   = nullptr;
  uint8_t* ulawBuf     = nullptr;
  uint8_t* b64OutBuf   = nullptr;
  int16_t* stereoTxBuf = nullptr; // Intercalado: [L0, R0, L1, R1, ...]

  uint8_t b64Tail[3];
  size_t  b64TailLen = 0;
  size_t  b64TotalSent = 0;

  void* safeAlloc(size_t bytes) {
    if (psramFound()) {
      void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
      if (p) return p;
    }
    return malloc(bytes);
  }

  bool begin() {
    micRawBuf   = (int32_t*)safeAlloc(MIC_READ_SAMPLES * sizeof(int32_t));
    ulawBuf     = (uint8_t*)safeAlloc(MIC_READ_SAMPLES * sizeof(uint8_t));
    b64OutBuf   = (uint8_t*)safeAlloc(((B64_CHUNK_RAW + 2) / 3) * 4 + 32);
    stereoTxBuf = (int16_t*)safeAlloc(SPK_FRAME_COUNT * 2 * sizeof(int16_t));

    if (!micRawBuf || !ulawBuf || !b64OutBuf || !stereoTxBuf) {
      Serial.println("❌ [Audio] Falha ao alocar buffers de audio!");
      return false;
    }

    initMicrophoneI2S();
    initStereoSpeakersI2S();
    Serial.printf("✅ [Audio] Inicializado (RAM livre: %u bytes | Volume: %d%%)\n",
                  ESP.getFreeHeap(), volumePercent);
    return true;
  }

  void initMicrophoneI2S() {
    i2s_config_t cfg = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = AUDIO_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0
    };

    i2s_pin_config_t pins = {
      .bck_io_num = MIC_PIN_BCLK,
      .ws_io_num = MIC_PIN_LRCLK,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = MIC_PIN_DIN
    };

    ESP_ERROR_CHECK(i2s_driver_install(I2S_MIC_PORT, &cfg, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_MIC_PORT, &pins));
    ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_MIC_PORT));
    Serial.println("🎙️ [Audio] Microfone I2S pronto (16kHz, 32-bit)");
  }

  void initStereoSpeakersI2S() {
    i2s_config_t cfg = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = AUDIO_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // ESTEREO REAL (2 Alto-falantes L + R)
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0
    };

    i2s_pin_config_t pins = {
      .bck_io_num = SPK_PIN_BCLK,
      .ws_io_num = SPK_PIN_LRCLK,
      .data_out_num = SPK_PIN_DOUT,
      .data_in_num = I2S_PIN_NO_CHANGE
    };

    ESP_ERROR_CHECK(i2s_driver_install(I2S_SPK_PORT, &cfg, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_SPK_PORT, &pins));
    ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_SPK_PORT));
    Serial.println("🔊 [Audio] 2 Alto-falantes I2S Estereo prontos (Canal L + Canal R)");
  }

  void setVolume(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    volumePercent = pct;
    Serial.printf("🔊 [Audio] Volume ajustado para %d%%\n", volumePercent);
  }

  void setStereoBalance(float leftGain, float rightGain) {
    balanceLeft  = constrain(leftGain, 0.0f, 1.5f);
    balanceRight = constrain(rightGain, 0.0f, 1.5f);
  }

  void silenceOutput() {
    memset(stereoTxBuf, 0, 128 * 2 * sizeof(int16_t));
    size_t bw = 0;
    i2s_write(I2S_SPK_PORT, stereoTxBuf, 128 * 2 * sizeof(int16_t), &bw, 0);
  }

  // ==== SINTETIZADOR DE EFEITOS SONOROS SCI-FI (ESTILO JARVIS) ====
  // Gera tons estereo com panning espacial entre o Alto-falante Esquerdo e Direito
  void playToneStereo(float freqHz, int durationMs, float panLR = 0.0f, float amplitude = 0.35f) {
    int totalSamples = (AUDIO_SAMPLE_RATE * durationMs) / 1000;
    float vol = (volumePercent / 100.0f) * amplitude;
    // panLR: -1.0 = so Esquerdo, 0.0 = Centro, +1.0 = so Direito
    float gainL = vol * balanceLeft  * (panLR <= 0.0f ? 1.0f : (1.0f - panLR));
    float gainR = vol * balanceRight * (panLR >= 0.0f ? 1.0f : (1.0f + panLR));

    int idx = 0;
    for (int s = 0; s < totalSamples; ++s) {
      // Envelope suave de ataque e decay para evitar cliques
      float env = 1.0f;
      int attack = totalSamples / 10 + 1;
      int release = totalSamples / 4 + 1;
      if (s < attack) env = (float)s / (float)attack;
      else if (s > totalSamples - release) env = (float)(totalSamples - s) / (float)release;

      float wave = sinf(2.0f * PI * freqHz * ((float)s / (float)AUDIO_SAMPLE_RATE));
      int16_t sampleL = (int16_t)(wave * 32767.0f * gainL * env);
      int16_t sampleR = (int16_t)(wave * 32767.0f * gainR * env);

      stereoTxBuf[idx * 2]     = sampleL;
      stereoTxBuf[idx * 2 + 1] = sampleR;
      idx++;

      if (idx >= (int)SPK_FRAME_COUNT) {
        size_t bw = 0;
        i2s_write(I2S_SPK_PORT, stereoTxBuf, idx * 2 * sizeof(int16_t), &bw, pdMS_TO_TICKS(200));
        idx = 0;
      }
    }
    if (idx > 0) {
      size_t bw = 0;
      i2s_write(I2S_SPK_PORT, stereoTxBuf, idx * 2 * sizeof(int16_t), &bw, pdMS_TO_TICKS(200));
    }
    silenceOutput();
  }

  // Som de Wake ("Jarvis pronto para ouvir") - Efeito espacial L -> R
  void playWakeChime() {
    playToneStereo(587.33f, 70, -0.5f, 0.30f); // D5 (Esquerda)
    playToneStereo(880.00f, 95,  0.5f, 0.35f); // A5 (Direita)
    playToneStereo(1174.66f, 110, 0.0f, 0.30f); // D6 (Centro)
  }

  // Som de fim de captura / processando
  void playThinkingBlip() {
    playToneStereo(987.77f, 60, 0.0f, 0.25f);
    playToneStereo(783.99f, 70, 0.0f, 0.22f);
  }

  // Som de acao concluida (Spotify / SmartLife)
  void playSuccessBeep() {
    playToneStereo(659.25f, 65, -0.3f, 0.28f);
    playToneStereo(987.77f, 110, 0.3f, 0.32f);
  }

  // Som de Alarme / Timer do Jarvis
  void playAlarmSound() {
    for (int i = 0; i < 3; ++i) {
      playToneStereo(1046.50f, 140, -0.6f, 0.45f);
      playToneStereo(1318.51f, 140,  0.6f, 0.45f);
      delay(60);
    }
  }

  // ==== CONVERSAO DE AUDIO (PCM 16-BIT <-> MU-LAW 8-BIT) ====
  static inline int16_t i2s32ToPcm16(int32_t s32) {
    int32_t shifted = s32 >> MIC_SHIFT_BITS;
    if (shifted > 32767) shifted = 32767;
    if (shifted < -32768) shifted = -32768;
    return (int16_t)shifted;
  }

  static inline uint8_t pcm16ToMulaw(int16_t s) {
    const uint16_t BIAS = 132;
    uint16_t mag = (s < 0) ? (uint16_t)(-s) : (uint16_t)s;
    mag += BIAS;
    if (mag > 0x1FFF) mag = 0x1FFF;
    uint8_t seg = 0;
    uint16_t tmp = mag >> 6;
    while (tmp) { seg++; tmp >>= 1; }
    uint8_t uval = (seg << 4) | ((mag >> (seg + 3)) & 0x0F);
    uval = ~uval;
    if (s >= 0) uval |= 0x80;
    else uval &= 0x7F;
    return uval;
  }

  static inline int16_t mulawToPcm16(uint8_t u) {
    u = ~u;
    int t = ((u & 0x0F) << 3) + 0x84;
    t <<= ((unsigned)u & 0x70) >> 4;
    return (u & 0x80) ? (0x84 - t) : (t - 0x84);
  }

  static inline int b64Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -2;
    return -1;
  }

  // ==== STREAMING HTTP CHUNKED + BASE64 PARA STT COM VAD ====
  // Envia via Transfer-Encoding: chunked para permitir encerrar a gravacao
  // dinamicamente assim que o usuario parar de falar (VAD)!
  bool writeHttpChunk(WiFiClientSecure& cli, const uint8_t* data, size_t len) {
    if (len == 0) return true;
    char hdr[16];
    snprintf(hdr, sizeof(hdr), "%X\r\n", (unsigned)len);
    if (cli.print(hdr) == 0) return false;
    if (cli.write(data, len) != len) return false;
    if (cli.print("\r\n") == 0) return false;
    return true;
  }

  bool b64ChunkedWrite(WiFiClientSecure& cli, const uint8_t* data, size_t rawBytes) {
    size_t offset = 0;
    while (offset < rawBytes) {
      size_t take = min(B64_CHUNK_RAW - b64TailLen, rawBytes - offset);
      // Copia para buffer temporario alinhado em multiplos de 3
      uint8_t tempIn[B64_CHUNK_RAW];
      if (b64TailLen > 0) {
        memcpy(tempIn, b64Tail, b64TailLen);
      }
      memcpy(tempIn + b64TailLen, data + offset, take);
      size_t totalIn = b64TailLen + take;
      offset += take;

      size_t encLen = (totalIn / 3) * 3;
      if (encLen > 0) {
        size_t outLen = 0;
        if (mbedtls_base64_encode(b64OutBuf, ((B64_CHUNK_RAW + 2) / 3) * 4 + 32,
                                  &outLen, tempIn, encLen) != 0) {
          return false;
        }
        if (!writeHttpChunk(cli, b64OutBuf, outLen)) return false;
        b64TotalSent += outLen;
      }
      b64TailLen = totalIn - encLen;
      if (b64TailLen > 0) {
        memcpy(b64Tail, tempIn + encLen, b64TailLen);
      }
    }
    return true;
  }

  bool b64ChunkedFlush(WiFiClientSecure& cli) {
    if (b64TailLen > 0) {
      size_t outLen = 0;
      if (mbedtls_base64_encode(b64OutBuf, 64, &outLen, b64Tail, b64TailLen) != 0) {
        return false;
      }
      if (!writeHttpChunk(cli, b64OutBuf, outLen)) return false;
      b64TotalSent += outLen;
      b64TailLen = 0;
    }
    return true;
  }

  // Codifica texto para query string de URL (usado no Edge-TTS do Bridge Server)
  String urlEncodeText(const String& str) {
    String encoded = "";
    encoded.reserve(str.length() * 2);
    const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < str.length(); i++) {
      uint8_t c = (uint8_t)str[i];
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
        encoded += (char)c;
      } else if (c == ' ') {
        encoded += "%20";
      } else {
        encoded += '%';
        encoded += hex[(c >> 4) & 0x0F];
        encoded += hex[c & 0x0F];
      }
    }
    return encoded;
  }

  // Monta um cabecalho WAV PCM 16-bit Mono 16kHz de 44 bytes
  void buildWavHeader16kMono(uint8_t* hdr, uint32_t pcmDataBytes) {
    uint32_t sampleRate = AUDIO_SAMPLE_RATE;
    uint32_t byteRate   = sampleRate * 2;
    uint32_t riffSize   = 36 + pcmDataBytes;
    uint16_t audioFmt   = 1; // PCM
    uint16_t numCh      = 1; // Mono
    uint16_t blockAlign = 2;
    uint16_t bitsPerSmp = 16;

    memcpy(hdr + 0,  "RIFF", 4);
    memcpy(hdr + 4,  &riffSize, 4);
    memcpy(hdr + 8,  "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    uint32_t fmtSize = 16;
    memcpy(hdr + 16, &fmtSize, 4);
    memcpy(hdr + 20, &audioFmt, 2);
    memcpy(hdr + 22, &numCh, 2);
    memcpy(hdr + 24, &sampleRate, 4);
    memcpy(hdr + 28, &byteRate, 4);
    memcpy(hdr + 32, &blockAlign, 2);
    memcpy(hdr + 34, &bitsPerSmp, 2);
    memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &pcmDataBytes, 4);
  }

  // ==== GRAVACAO COM VAD + GROQ CLOUD WHISPER STREAMING (100% GRATUITO SEM CARTAO) ====
  // Transmite audio WAV PCM 16-bit 16kHz em tempo real via Transfer-Encoding: chunked
  // direto para api.groq.com (whisper-large-v3-turbo). Assim que o VAD detecta 1.2s de
  // silencio apos a fala, fecha o stream e recebe a transcricao em ~150ms!
  bool listenAndTranscribe(WiFiClientSecure& cli, String& outTranscript) {
    outTranscript = "";
    cli.stop();
    cli.setInsecure();
    cli.setTimeout(25000);

    uint32_t t0 = millis();
    if (!cli.connect("api.groq.com", 443)) {
      Serial.println("❌ [STT] Falha na conexao TLS com api.groq.com");
      return false;
    }

    const char* boundary = "----JarvisESP32Boundary";
    String partHead =
      String("--") + boundary + "\r\n"
      "Content-Disposition: form-data; name=\"model\"\r\n\r\n" + String(GROQ_STT_MODEL) + "\r\n"
      "--" + boundary + "\r\n"
      "Content-Disposition: form-data; name=\"language\"\r\n\r\npt\r\n"
      "--" + boundary + "\r\n"
      "Content-Disposition: form-data; name=\"response_format\"\r\n\r\njson\r\n"
      "--" + boundary + "\r\n"
      "Content-Disposition: form-data; name=\"file\"; filename=\"speech.wav\"\r\n"
      "Content-Type: audio/wav\r\n\r\n";

    String partTail = String("\r\n--") + boundary + "--\r\n";

    cli.print("POST /openai/v1/audio/transcriptions HTTP/1.1\r\n");
    cli.print("Host: api.groq.com\r\n");
    cli.print(String("Authorization: Bearer ") + GROQ_API_KEY + "\r\n");
    cli.print("User-Agent: JarvisESP32/2.0\r\n");
    cli.print(String("Content-Type: multipart/form-data; boundary=") + boundary + "\r\n");
    cli.print("Transfer-Encoding: chunked\r\n");
    cli.print("Connection: close\r\n\r\n");

    if (!writeHttpChunk(cli, (const uint8_t*)partHead.c_str(), partHead.length())) {
      cli.stop();
      return false;
    }

    const size_t maxSamples = (size_t)AUDIO_SAMPLE_RATE * MAX_RECORD_SECONDS;
    const size_t minSamples = (size_t)AUDIO_SAMPLE_RATE * 1; // Minimo 1.0s

    uint8_t wavHdr[44];
    buildWavHeader16kMono(wavHdr, (uint32_t)(maxSamples * sizeof(int16_t)));
    if (!writeHttpChunk(cli, wavHdr, sizeof(wavHdr))) {
      cli.stop();
      return false;
    }

    // Limpa buffer antigo do DMA do microfone
    i2s_zero_dma_buffer(I2S_MIC_PORT);
    playWakeChime();

    Serial.println("🎙️ [STT Groq Whisper] Ouvindo com VAD inteligente (fale agora)...");
    size_t samplesSent = 0;
    size_t pcmBytesSent = 0;

    bool speechDetected = false;
    uint32_t lastVoiceMillis = millis();
    uint32_t recordStartMillis = millis();
    int16_t maxPeak = 0;
    int16_t pcmChunk[MIC_READ_SAMPLES];

    digitalWrite(PIN_STATUS_LED, HIGH);

    while (samplesSent < maxSamples) {
      size_t br = 0;
      esp_err_t err = i2s_read(I2S_MIC_PORT, (void*)micRawBuf,
                               MIC_READ_SAMPLES * sizeof(int32_t), &br, pdMS_TO_TICKS(100));
      if (err != ESP_OK || br == 0) continue;

      size_t count = br / sizeof(int32_t);
      uint64_t chunkSq = 0;

      for (size_t i = 0; i < count; ++i) {
        int16_t s16 = i2s32ToPcm16(micRawBuf[i]);
        int16_t absVal = (s16 < 0) ? -s16 : s16;
        if (absVal > maxPeak) maxPeak = absVal;
        chunkSq += (int32_t)s16 * (int32_t)s16;
        pcmChunk[i] = s16;
      }

      float chunkRms = sqrtf((float)chunkSq / (float)count);
      if (chunkRms >= (float)vadThreshold) {
        if (!speechDetected) {
          Serial.printf("🗣️ [VAD] Voz detectada (RMS=%.0f)\n", chunkRms);
        }
        speechDetected = true;
        lastVoiceMillis = millis();
      }

      size_t chunkBytes = count * sizeof(int16_t);
      if (!writeHttpChunk(cli, (const uint8_t*)pcmChunk, chunkBytes)) {
        Serial.println("❌ [STT] Erro ao enviar stream WAV para Groq");
        digitalWrite(PIN_STATUS_LED, LOW);
        cli.stop();
        return false;
      }

      samplesSent += count;
      pcmBytesSent += chunkBytes;
      esp_task_wdt_reset();

      // Se ja falou e ficou em silencio por VAD_SILENCE_MS, encerra imediatamente!
      if (speechDetected && samplesSent >= minSamples &&
          (millis() - lastVoiceMillis >= VAD_SILENCE_MS)) {
        Serial.printf("⚡ [VAD] Fim da fala detectado em %.1fs! Finalizando captura.\n",
                      (millis() - recordStartMillis) / 1000.0f);
        break;
      }

      // Se ninguem falou nada por 3.5 segundos, encerra mais cedo
      if (!speechDetected && (millis() - recordStartMillis >= 3500)) {
        Serial.println("⚠️ [VAD] Nenhuma voz detectada em 3.5s.");
        break;
      }
    }

    digitalWrite(PIN_STATUS_LED, LOW);
    playThinkingBlip();

    writeHttpChunk(cli, (const uint8_t*)partTail.c_str(), partTail.length());
    cli.print("0\r\n\r\n");

    Serial.printf("⏱️ [STT Groq] Audio enviado (%u bytes PCM, Peak=%d) em %ums. Aguardando Whisper...\n",
                  (unsigned)pcmBytesSent, maxPeak, millis() - t0);

    if (!speechDetected) {
      cli.stop();
      return false;
    }

    String resp = "";
    uint32_t waitStart = millis();
    while ((cli.connected() || cli.available()) && (millis() - waitStart < 15000)) {
      while (cli.available()) {
        resp += (char)cli.read();
      }
      delay(2);
      esp_task_wdt_reset();
    }
    cli.stop();

    int bodyIdx = resp.indexOf("{");
    int bodyEnd = resp.lastIndexOf("}");
    if (bodyIdx < 0 || bodyEnd <= bodyIdx) {
      Serial.println("⚠️ [STT Groq] Resposta vazia ou sem fala reconhecida.");
      return false;
    }

    String jsonBody = resp.substring(bodyIdx, bodyEnd + 1);
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, jsonBody)) {
      Serial.println("❌ [STT Groq] Erro ao decodificar JSON do Whisper");
      return false;
    }

    outTranscript = doc["text"] | "";
    outTranscript.trim();
    return outTranscript.length() > 0;
  }

  // ==== TTS HIBRIDO 100% GRATUITO SEM CARTAO (EDGE-TTS BRIDGE + FALLBACK GEMINI TTS) ====
  // 1. Se o Bridge Server (jarvis_bridge_server.py) estiver ativo, reproduz a voz neural
  //    Microsoft Edge-TTS (pt-BR-AntonioNeural) em PCM 16kHz puro via HTTP streaming!
  // 2. Se o PC estiver desligado, faz fallback automatico para o Gemini 2.5 Flash TTS
  //    direto pelo Google AI Studio (tambem 100% gratuito e sem cartao), decodificando
  //    PCM 16-bit 24kHz direto do socket TLS para os 2 alto-falantes estereo!
  bool speakText(WiFiClientSecure& cli, const String& text) {
    if (text.length() == 0) return false;

    // 1. Tenta primeiro via Bridge Server (Edge-TTS Neural pt-BR-AntonioNeural, PCM 16 kHz em blocos)
    if (strlen(EXTERNAL_SERVER_URL) > 5) {
      String base = String(EXTERNAL_SERVER_URL) + "/api/tts";
      bool ok = false;
      if (g_bridgeTtsKey.length() > 0 && g_bridgeTtsPrefix.length() > 0 && text.startsWith(g_bridgeTtsPrefix)) {
        // Devocional/plano completo guardado no servidor: nada de texto gigante na RAM/URL
        Serial.println("🔊 [TTS Bridge] Lendo o texto completo guardado no servidor (key=" + g_bridgeTtsKey + ")...");
        String keyUrl = base + "?key=" + g_bridgeTtsKey;
        g_bridgeTtsKey = "";
        ok = streamRawAudioFromUrl(keyUrl, 3600);
      } else if (text.length() > 600) {
        // Texto longo: vai no corpo do POST (evita URL de varios KB)
        Serial.println("🔊 [TTS Bridge] Texto longo via POST /api/tts...");
        ok = streamRawAudioFromUrl(base, 3600, &text);
      } else {
        Serial.println("🔊 [TTS Bridge] Reproduzindo voz neural Edge-TTS (pt-BR-AntonioNeural)...");
        ok = streamRawAudioFromUrl(base + "?text=" + urlEncodeText(text), 600);
      }
      g_bridgeTtsKey = "";
      if (ok) return true;
      Serial.println("⚠️ [TTS Bridge] Servidor externo indisponivel. Usando Gemini TTS direto...");
    }

    // 2. Fallback: Gemini TTS em blocos de ate ~900 caracteres (cada requisicao cabe na RAM)
    bool any = false;
    int pos = 0;
    const int n = (int)text.length();
    while (pos < n) {
      int end = nextTtsChunkEnd(text, pos, 900);
      String part = text.substring(pos, end);
      part.trim();
      if (part.length() > 0) {
        if (!speakGeminiChunk(cli, part)) break;
        any = true;
      }
      pos = end;
      if (digitalRead(PIN_WAKE_BUTTON) == LOW || digitalRead(PIN_EXTERNAL_BTN) == LOW) break;
    }
    return any;
  }

  // Fim do proximo bloco: ultima pontuacao final dentro do limite (ou ultimo espaco)
  static int nextTtsChunkEnd(const String& text, int pos, int maxLen) {
    int n = (int)text.length();
    if (n - pos <= maxLen) return n;
    int limit = pos + maxLen;
    for (int i = limit; i > pos + 120; --i) {
      char c = text.charAt(i - 1);
      if (c == '.' || c == '!' || c == '?' || c == ';') return i;
    }
    for (int i = limit; i > pos + 60; --i) {
      if (text.charAt(i - 1) == ' ') return i;
    }
    return limit;
  }

  bool speakGeminiChunk(WiFiClientSecure& cli, const String& text) {
    // 2. Fallback Standalone: Google AI Studio Gemini TTS (audio/L16;codec=pcm;rate=24000)
    cli.stop();
    cli.setInsecure();
    cli.setTimeout(25000);

    if (!cli.connect("generativelanguage.googleapis.com", 443)) {
      Serial.println("❌ [TTS Gemini] Falha ao conectar em generativelanguage.googleapis.com");
      return false;
    }

    DynamicJsonDocument reqDoc(1024 + text.length() * 2);
    JsonArray contents = reqDoc.createNestedArray("contents");
    JsonObject c0 = contents.createNestedObject();
    JsonArray parts = c0.createNestedArray("parts");
    JsonObject p0 = parts.createNestedObject();
    p0["text"] = String("Leia em voz alta em portugues do Brasil exatamente este texto: ") + text;

    JsonObject genCfg = reqDoc.createNestedObject("generationConfig");
    JsonArray mods = genCfg.createNestedArray("responseModalities");
    mods.add("AUDIO");
    JsonObject spkCfg = genCfg.createNestedObject("speechConfig");
    JsonObject vCfg = spkCfg.createNestedObject("voiceConfig");
    JsonObject pbCfg = vCfg.createNestedObject("prebuiltVoiceConfig");
    pbCfg["voiceName"] = GEMINI_TTS_VOICE;

    String reqBody;
    serializeJson(reqDoc, reqBody);

    String path = String("/v1beta/models/") + GEMINI_TTS_MODEL + ":generateContent?key=" + GEMINI_API_KEY;
    cli.print(String("POST ") + path + " HTTP/1.1\r\n");
    cli.print("Host: generativelanguage.googleapis.com\r\n");
    cli.print("Content-Type: application/json\r\n");
    cli.print("Content-Length: " + String(reqBody.length()) + "\r\n");
    cli.print("Connection: close\r\n\r\n");
    cli.print(reqBody);

    // Localiza a chave `"data"` do inlineData no stream sem carregar tudo na RAM
    const char* marker = "\"data\"";
    int markerIdx = 0;
    const int markerLen = strlen(marker);
    bool foundMarker = false;
    bool inBase64Quote = false;

    uint32_t waitStart = millis();
    while ((cli.connected() || cli.available()) && !inBase64Quote && (millis() - waitStart < 20000)) {
      if (!cli.available()) {
        delay(1);
        esp_task_wdt_reset();
        continue;
      }
      char c = (char)cli.read();
      if (!foundMarker) {
        if (c == marker[markerIdx]) {
          markerIdx++;
          if (markerIdx == markerLen) foundMarker = true;
        } else {
          markerIdx = (c == marker[0]) ? 1 : 0;
        }
      } else {
        if (c == '"') {
          inBase64Quote = true; // Inicio do Base64 PCM 24kHz!
        }
      }
    }

    if (!inBase64Quote) {
      Serial.println("❌ [TTS Gemini] Campo data de audio nao encontrado na resposta!");
      cli.stop();
      return false;
    }

    Serial.println("🔊 [TTS Gemini] Reproduzindo voz nos 2 alto-falantes (24kHz Estereo)...");
    i2s_set_sample_rates(I2S_SPK_PORT, 24000);

    float volScale = (volumePercent / 100.0f);
    float gainL = volScale * balanceLeft;
    float gainR = volScale * balanceRight;

    size_t stereoFrameFill = 0;
    int quartet[4];
    int qi = 0;
    bool done = false;
    uint8_t rxChunk[512];
    bool hasPendingByte = false;
    uint8_t pendingLowByte = 0;

    while ((cli.connected() || cli.available()) && !done) {
      int avail = cli.available();
      if (avail <= 0) {
        delay(1);
        esp_task_wdt_reset();
        continue;
      }
      int toRead = min(avail, (int)sizeof(rxChunk));
      int got = cli.read(rxChunk, toRead);

      for (int i = 0; i < got && !done; ++i) {
        char c = (char)rxChunk[i];
        if (c == '"') {
          done = true;
          while (qi > 0 && qi < 4) quartet[qi++] = -2;
        } else {
          int v = b64Val(c);
          if (v < 0) continue;
          quartet[qi++] = v;
        }

        if (qi == 4) {
          int v0 = quartet[0], v1 = quartet[1], v2 = quartet[2], v3 = quartet[3];
          uint8_t out[3];
          out[0] = (uint8_t)((v0 << 2) | ((v1 & 0x30) >> 4));
          out[1] = (uint8_t)(((v1 & 0x0F) << 4) | ((v2 >= 0 ? v2 : 0) >> 2));
          out[2] = (uint8_t)(((v2 >= 0 ? v2 : 0) << 6) | (v3 >= 0 ? v3 : 0));

          int outN = 3;
          if (v3 == -2) outN = 2;
          if (v2 == -2) outN = 1;

          for (int j = 0; j < outN; ++j) {
            if (!hasPendingByte) {
              pendingLowByte = out[j];
              hasPendingByte = true;
            } else {
              int16_t mono = (int16_t)((uint16_t)pendingLowByte | ((uint16_t)out[j] << 8));
              hasPendingByte = false;

              int32_t sL = (int32_t)(mono * gainL);
              int32_t sR = (int32_t)(mono * gainR);
              if (sL > 32767) sL = 32767; else if (sL < -32768) sL = -32768;
              if (sR > 32767) sR = 32767; else if (sR < -32768) sR = -32768;

              stereoTxBuf[stereoFrameFill * 2]     = (int16_t)sL;
              stereoTxBuf[stereoFrameFill * 2 + 1] = (int16_t)sR;
              stereoFrameFill++;

              if (stereoFrameFill >= SPK_FRAME_COUNT) {
                size_t bw = 0;
                i2s_write(I2S_SPK_PORT, stereoTxBuf,
                          SPK_FRAME_COUNT * 2 * sizeof(int16_t), &bw, pdMS_TO_TICKS(1000));
                stereoFrameFill = 0;
                esp_task_wdt_reset();
              }
            }
          }
          qi = 0;
        }
      }
    }

    if (stereoFrameFill > 0) {
      size_t bw = 0;
      i2s_write(I2S_SPK_PORT, stereoTxBuf,
                stereoFrameFill * 2 * sizeof(int16_t), &bw, pdMS_TO_TICKS(500));
    }

    silenceOutput();
    i2s_set_sample_rates(I2S_SPK_PORT, AUDIO_SAMPLE_RATE);
    cli.stop();
    Serial.println("✅ [TTS Gemini] Reproducao concluida!");
    return true;
  }

  // ==== STREAMING DE MUSICA PCM / RADIO / SPOTIFY DO SERVIDOR EXTERNO NOS 2 ALTO-FALANTES ====
  // Recebe audio PCM 16-bit 16kHz Mono/Estereo via HTTP do Servidor Externo (Jarvis Bridge)
  // e reproduz em tempo real nos 2 alto-falantes do ESP32 ate terminar ou pressionar o botao BOOT!
  // postBody != nullptr -> envia o texto no corpo (POST); o servidor responde PCM em blocos
  // ate fechar a conexao (sem Content-Length), entao maxDurationSec e so um limite de seguranca.
  bool streamRawAudioFromUrl(const String& streamUrl, int maxDurationSec = 45, const String* postBody = nullptr) {
    if (streamUrl.length() < 8) return false;

    Serial.printf("🎵 [AudioStream] Conectando ao stream de audio: %.120s\n", streamUrl.c_str());
    HTTPClient http;
    WiFiClient tcpCli;
    if (!http.begin(tcpCli, streamUrl)) {
      Serial.println("❌ [AudioStream] Falha ao iniciar conexao HTTP de audio");
      return false;
    }
    http.setConnectTimeout(3000);
    http.setTimeout(30000);   // o 1o bloco de voz e sintetizado antes de chegar o cabecalho HTTP

    int code;
    if (postBody != nullptr) {
      http.addHeader("Content-Type", "text/plain; charset=utf-8");
      code = http.POST(*postBody);
    } else {
      code = http.GET();
    }
    if (code != 200) {
      Serial.printf("❌ [AudioStream] HTTP %d ao abrir stream\n", code);
      http.end();
      return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    float volScale = (volumePercent / 100.0f);
    float gainL = volScale * balanceLeft;
    float gainR = volScale * balanceRight;

    uint8_t rawPcm[512]; // 256 amostras PCM 16-bit mono
    uint32_t tStart = millis();
    uint32_t maxMs = (uint32_t)maxDurationSec * 1000UL;

    Serial.println("🎶 [AudioStream] Tocando musica nos 2 alto-falantes (Pressione BOOT para parar)...");
    while ((http.connected() || stream->available() > 0) && (millis() - tStart < maxMs)) {
      if (digitalRead(PIN_WAKE_BUTTON) == LOW || digitalRead(PIN_EXTERNAL_BTN) == LOW) {
        Serial.println("⏹️ [AudioStream] Reproducao interrompida pelo botao!");
        break;
      }

      int avail = stream->available();
      if (avail < 2) {
        delay(1);
        esp_task_wdt_reset();
        continue;
      }

      int toRead = min(avail, (int)sizeof(rawPcm));
      toRead &= ~1; // Garante multiplo de 2 bytes (int16_t)
      int got = stream->readBytes(rawPcm, toRead);
      int samples = got / 2;
      const int16_t* pcmSamples = (const int16_t*)rawPcm;

      for (int i = 0; i < samples; ++i) {
        int32_t sL = (int32_t)(pcmSamples[i] * gainL);
        int32_t sR = (int32_t)(pcmSamples[i] * gainR);
        if (sL > 32767) sL = 32767; else if (sL < -32768) sL = -32768;
        if (sR > 32767) sR = 32767; else if (sR < -32768) sR = -32768;

        stereoTxBuf[i * 2]     = (int16_t)sL;
        stereoTxBuf[i * 2 + 1] = (int16_t)sR;
      }

      size_t bw = 0;
      i2s_write(I2S_SPK_PORT, stereoTxBuf, samples * 2 * sizeof(int16_t), &bw, pdMS_TO_TICKS(500));
      esp_task_wdt_reset();
    }

    silenceOutput();
    http.end();
    Serial.println("✅ [AudioStream] Stream finalizado!");
    return true;
  }
};

