#pragma once
#include <Arduino.h>
#include <WiFiClient.h>
#include <math.h>
#include "driver/i2s.h"
#include "config.h"

// ============================================================================
// AUDIO I/O — microfone INMP441 + 2 alto-falantes MAX98357A. Sem logica de IA.
// Buffers estaticos (~5 KB no total), sem malloc.
// ============================================================================
class AudioIO {
public:
  int volume = DEFAULT_VOLUME_PCT;

  void begin() {
    i2s_config_t mic = {};
    mic.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    mic.sample_rate = AUDIO_SAMPLE_RATE;
    mic.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    mic.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    mic.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    mic.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    mic.dma_buf_count = 8;
    mic.dma_buf_len = 256;
    i2s_pin_config_t micPins = {};
    micPins.bck_io_num = MIC_PIN_BCLK;
    micPins.ws_io_num = MIC_PIN_LRCLK;
    micPins.data_out_num = I2S_PIN_NO_CHANGE;
    micPins.data_in_num = MIC_PIN_DIN;
    i2s_driver_install(I2S_MIC_PORT, &mic, 0, NULL);
    i2s_set_pin(I2S_MIC_PORT, &micPins);

    i2s_config_t spk = {};
    spk.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    spk.sample_rate = AUDIO_SAMPLE_RATE;
    spk.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    spk.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;   // estereo real L + R
    spk.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    spk.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    spk.dma_buf_count = 8;
    spk.dma_buf_len = 256;
    spk.tx_desc_auto_clear = true;                      // silencio quando a rede atrasa
    i2s_pin_config_t spkPins = {};
    spkPins.bck_io_num = SPK_PIN_BCLK;
    spkPins.ws_io_num = SPK_PIN_LRCLK;
    spkPins.data_out_num = SPK_PIN_DOUT;
    spkPins.data_in_num = I2S_PIN_NO_CHANGE;
    i2s_driver_install(I2S_SPK_PORT, &spk, 0, NULL);
    i2s_set_pin(I2S_SPK_PORT, &spkPins);
    i2s_zero_dma_buffer(I2S_SPK_PORT);
  }

  void setVolume(int v) { volume = constrain(v, 0, 100); }

  static bool buttonPressed() {
    return digitalRead(PIN_WAKE_BUTTON) == LOW || digitalRead(PIN_EXTERNAL_BTN) == LOW;
  }

  // ---------------------------------------------------------------- efeitos sonoros
  void tone(float hz, int ms, float pan = 0.0f, float amp = 0.32f) {
    const int total = AUDIO_SAMPLE_RATE * ms / 1000;
    const float g = (volume / 100.0f) * amp * 32767.0f;
    const float gl = g * (pan <= 0 ? 1.0f : 1.0f - pan), gr = g * (pan >= 0 ? 1.0f : 1.0f + pan);
    int n = 0;
    for (int s = 0; s < total; ++s) {
      float env = 1.0f;
      const int att = total / 10 + 1, rel = total / 4 + 1;
      if (s < att) env = (float)s / att; else if (s > total - rel) env = (float)(total - s) / rel;
      float w = sinf(2.0f * (float)M_PI * hz * s / AUDIO_SAMPLE_RATE) * env;
      stereo[n * 2] = (int16_t)(w * gl);
      stereo[n * 2 + 1] = (int16_t)(w * gr);
      if (++n == FRAMES) { writeFrames(n); n = 0; }
    }
    if (n) writeFrames(n);
  }
  void chime()   { tone(587.33f, 70, -0.5f); tone(880.0f, 95, 0.5f); tone(1174.66f, 110, 0.0f); }
  void blip()    { tone(987.77f, 60); tone(783.99f, 70); }
  void ok()      { tone(659.25f, 65, -0.3f); tone(987.77f, 110, 0.3f); }
  void noSpeech(){ tone(440.0f, 90); tone(330.0f, 120); }
  void error()   { tone(220.0f, 160); delay(60); tone(196.0f, 220); }
  void alarm()   { for (int i = 0; i < 3; ++i) { tone(1046.5f, 140, -0.6f, 0.45f); tone(1318.5f, 140, 0.6f, 0.45f); delay(60); } }
  void playMode(const String& m) {
    if (m == "left") tone(660, 350, -1.0f, 0.45f);
    else if (m == "right") tone(880, 350, 1.0f, 0.45f);
    else if (m == "alarm") alarm();
    else if (m == "ok") ok();
    else chime();
  }

  // ---------------------------------------------------------------- microfone -> servidor
  // Le o microfone e envia PCM 16-bit 16 kHz mono como chunks HTTP (Transfer-Encoding: chunked)
  // ENQUANTO voce fala. Para sozinho pelo VAD. Retorna true se houve voz.
  bool captureChunked(WiFiClient& c) {
    i2s_zero_dma_buffer(I2S_MIC_PORT);
    size_t dump = 0;                                   // descarta o eco do proprio chime
    i2s_read(I2S_MIC_PORT, mic32, sizeof(mic32), &dump, pdMS_TO_TICKS(60));
    const uint32_t t0 = millis();
    uint32_t lastVoice = t0;
    bool speech = false;
    digitalWrite(PIN_STATUS_LED, HIGH);
    while (millis() - t0 < (uint32_t)MAX_RECORD_SECONDS * 1000UL) {
      size_t br = 0;
      if (i2s_read(I2S_MIC_PORT, mic32, sizeof(mic32), &br, pdMS_TO_TICKS(100)) != ESP_OK || br == 0) continue;
      const size_t n = br / sizeof(int32_t);
      uint64_t sq = 0;
      for (size_t i = 0; i < n; ++i) {
        int32_t v = mic32[i] >> MIC_SHIFT_BITS;
        v = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
        pcm[i] = (int16_t)v;
        sq += (int64_t)v * v;
      }
      if (sqrtf((float)sq / n) >= VAD_RMS_THRESHOLD) { speech = true; lastVoice = millis(); }
      char hdr[8];
      snprintf(hdr, sizeof(hdr), "%X\r\n", (unsigned)(n * 2));
      if (c.print(hdr) == 0 || c.write((const uint8_t*)pcm, n * 2) != n * 2 || c.print("\r\n") == 0) break;
      if (speech && millis() - lastVoice >= VAD_SILENCE_MS) break;
      if (!speech && millis() - t0 >= VAD_NO_SPEECH_MS) break;
    }
    digitalWrite(PIN_STATUS_LED, LOW);
    return speech;
  }

  // ---------------------------------------------------------------- servidor -> alto-falantes
  // Toca PCM 16-bit 16 kHz mono ate o fim (Content-Length ou fechamento da conexao).
  // O botao interrompe (ignorado nos primeiros 700 ms para nao pegar o mesmo toque).
  bool playStream(WiFiClient& c, long contentLength) {
    const float g = volume / 100.0f;
    long remaining = contentLength;                    // -1 = ate fechar
    uint32_t lastData = millis();
    const uint32_t t0 = millis();
    bool haveCarry = false;
    uint8_t carry = 0;
    while (remaining != 0) {
      if (millis() - t0 > 700 && buttonPressed()) { i2s_zero_dma_buffer(I2S_SPK_PORT); return false; }
      int avail = c.available();
      if (avail <= 0) {
        if (!c.connected() || millis() - lastData > 15000) break;
        delay(2);
        continue;
      }
      int want = avail > (int)sizeof(rx) ? (int)sizeof(rx) : avail;
      if (remaining > 0 && want > remaining) want = (int)remaining;
      int got = c.read(rx, want);
      if (got <= 0) continue;
      lastData = millis();
      if (remaining > 0) remaining -= got;
      int frames = 0, i = 0;
      if (haveCarry) {                                   // amostra que ficou dividida entre dois pacotes
        int16_t s = (int16_t)(carry | (rx[0] << 8));
        stereo[0] = stereo[1] = (int16_t)(s * g);
        frames = 1; i = 1; haveCarry = false;
      }
      for (; i + 1 < got; i += 2) {
        int16_t s = (int16_t)(rx[i] | (rx[i + 1] << 8));
        stereo[frames * 2] = stereo[frames * 2 + 1] = (int16_t)(s * g);
        if (++frames == FRAMES) { writeFrames(frames); frames = 0; }
      }
      if (i < got) { carry = rx[i]; haveCarry = true; }
      if (frames) writeFrames(frames);
    }
    return true;
  }

private:
  static const int FRAMES = 256;
  int32_t mic32[512];
  int16_t pcm[512];
  int16_t stereo[FRAMES * 2];
  uint8_t rx[1024];

  void writeFrames(int n) {
    size_t bw = 0;
    i2s_write(I2S_SPK_PORT, stereo, n * 2 * sizeof(int16_t), &bw, pdMS_TO_TICKS(500));
  }
};
