#pragma once
#include <Arduino.h>
#include <WiFiClient.h>
#include <math.h>
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include <MP3DecoderHelix.h>
#include "config.h"

// ============================================================================
// AUDIO — microfone INMP441, 2x MAX98357A (estereo) e player MP3 com buffer em anel.
//
// Player: a rede escreve MP3 no anel (40 KB, ~6,5 s); uma tarefa no nucleo 1 decodifica
// (libhelix) e toca. Enquanto o anel toca, o ESP ja baixa a proxima parte do texto:
// leituras longas (devocional inteiro) saem sem pausas e sem estourar a RAM.
// ============================================================================
class AudioIO;
static AudioIO* g_audio = nullptr;
static void mp3PcmCallback(MP3FrameInfo& info, int16_t* pcm, size_t len, void* ref);

class AudioIO {
public:
  volatile int volume = DEFAULT_VOLUME_PCT;
  uint32_t rate = SPK_SAMPLE_RATE;

  void begin() {
    g_audio = this;
    i2s_config_t mic = {};
    mic.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    mic.sample_rate = MIC_SAMPLE_RATE;
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
    spk.sample_rate = SPK_SAMPLE_RATE;
    spk.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    spk.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;   // estereo real L + R
    spk.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    spk.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    spk.dma_buf_count = 8;
    spk.dma_buf_len = 512;
    spk.tx_desc_auto_clear = true;                      // silencio se a rede atrasar
    i2s_pin_config_t spkPins = {};
    spkPins.bck_io_num = SPK_PIN_BCLK;
    spkPins.ws_io_num = SPK_PIN_LRCLK;
    spkPins.data_out_num = SPK_PIN_DOUT;
    spkPins.data_in_num = I2S_PIN_NO_CHANGE;
    i2s_driver_install(I2S_SPK_PORT, &spk, 0, NULL);
    i2s_set_pin(I2S_SPK_PORT, &spkPins);
    i2s_zero_dma_buffer(I2S_SPK_PORT);

    // aloca cedo, com a RAM ainda inteira (blocos contiguos)
    ring = xRingbufferCreate(MP3_RING_BYTES, RINGBUF_TYPE_BYTEBUF);
    mp3.begin();
    xTaskCreatePinnedToCore(decoderTask, "mp3", 6144, this, 3, &task, 1);
  }

  void setVolume(int v) { volume = constrain(v, 0, 100); }

  static bool buttonPressed() {
    return digitalRead(PIN_WAKE_BUTTON) == LOW || digitalRead(PIN_EXTERNAL_BTN) == LOW;
  }

  // ---------------------------------------------------------------- efeitos sonoros (sintetizados)
  void tone(float hz, int ms, float pan = 0.0f, float amp = 0.32f) {
    waitIdle(3000);
    setRate(SPK_SAMPLE_RATE);
    const int total = (int)rate * ms / 1000;
    const float g = (volume / 100.0f) * amp * 32767.0f;
    const float gl = g * (pan <= 0 ? 1.0f : 1.0f - pan), gr = g * (pan >= 0 ? 1.0f : 1.0f + pan);
    const int att = total / 10 + 1, rel = total / 4 + 1;
    int n = 0;
    for (int s = 0; s < total; ++s) {
      float env = s < att ? (float)s / att : (s > total - rel ? (float)(total - s) / rel : 1.0f);
      float w = sinf(2.0f * (float)M_PI * hz * s / rate) * env;
      toneBuf[n * 2] = (int16_t)(w * gl);
      toneBuf[n * 2 + 1] = (int16_t)(w * gr);
      if (++n == TONE_FRAMES) { writeI2S(toneBuf, n); n = 0; }
    }
    if (n) writeI2S(toneBuf, n);
  }
  void chime()   { tone(587.33f, 70, -0.5f); tone(880.0f, 95, 0.5f); tone(1174.66f, 110, 0.0f); }
  void blip()    { tone(987.77f, 60); tone(783.99f, 70); }
  void ok()      { tone(659.25f, 65, -0.3f); tone(987.77f, 110, 0.3f); }
  void noSpeech(){ tone(440.0f, 90); tone(330.0f, 120); }
  void error()   { tone(220.0f, 160); delay(60); tone(196.0f, 220); }
  void bell()    { tone(1318.5f, 120, -0.4f, 0.4f); tone(1567.98f, 160, 0.4f, 0.4f); delay(120); tone(1318.5f, 120, -0.4f, 0.4f); tone(1567.98f, 220, 0.4f, 0.4f); }
  void alarm()   { for (int i = 0; i < 3; ++i) { tone(1046.5f, 140, -0.6f, 0.45f); tone(1318.5f, 140, 0.6f, 0.45f); delay(60); } }
  void playMode(const char* m) {
    if (!strcmp(m, "left")) tone(660, 350, -1.0f, 0.45f);
    else if (!strcmp(m, "right")) tone(880, 350, 1.0f, 0.45f);
    else if (!strcmp(m, "alarm")) alarm();
    else if (!strcmp(m, "ok")) ok();
    else if (!strcmp(m, "bell")) bell();
    else chime();
  }

  // ---------------------------------------------------------------- microfone -> Groq (chunked)
  // Envia PCM 16-bit 16 kHz mono como chunks HTTP ENQUANTO voce fala; para pelo VAD.
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

  // ---------------------------------------------------------------- player MP3
  /** Coloca bytes MP3 no anel. Bloqueia se o anel estiver cheio (o audio dita o ritmo da rede).
   *  Retorna false se o usuario apertou o botao (interrompe a fala). */
  bool push(const uint8_t* d, size_t n) {
    while (n > 0) {
      if (interrupted()) return false;
      size_t piece = n > 1024 ? 1024 : n;
      if (xRingbufferSend(ring, d, piece, pdMS_TO_TICKS(50)) == pdTRUE) {
        pushed += piece; d += piece; n -= piece;
      }
    }
    return true;
  }
  /** Marca o inicio de uma fala (o botao so interrompe depois de 700 ms, para nao pegar o mesmo toque). */
  void startSpeech() { speechStart = millis(); stopReq = false; }
  bool interrupted() {
    if (!stopReq && millis() - speechStart > 700 && buttonPressed()) stopReq = true;
    return stopReq;
  }
  /** Espera tocar tudo o que esta no anel. false = interrompido pelo botao. */
  bool drain() {
    while (!isIdle()) {
      if (interrupted()) { abortPlayback(); return false; }
      delay(10);
    }
    return true;
  }
  void abortPlayback() { flushReq = true; uint32_t t = millis(); while (flushReq && millis() - t < 500) delay(5); }
  bool isIdle() const { return consumed == pushed && !decoding; }
  void waitIdle(uint32_t ms) { uint32_t t = millis(); while (!isIdle() && millis() - t < ms) delay(5); }

  // chamado pela tarefa do decoder
  void onPcm(MP3FrameInfo& info, int16_t* in, size_t len) {
    if (info.samprate > 0 && (uint32_t)info.samprate != rate) setRate(info.samprate);
    const float g = volume / 100.0f;
    const int ch = info.nChans > 0 ? info.nChans : 1;
    const size_t frames = len / ch;
    int n = 0;
    for (size_t i = 0; i < frames; ++i) {
      int16_t l = in[i * ch], r = ch > 1 ? in[i * ch + 1] : l;
      decBuf[n * 2] = (int16_t)(l * g);
      decBuf[n * 2 + 1] = (int16_t)(r * g);
      if (++n == TONE_FRAMES) { writeI2S(decBuf, n); n = 0; }
    }
    if (n) writeI2S(decBuf, n);
  }

private:
  static const int TONE_FRAMES = 256;
  int32_t mic32[512];
  int16_t pcm[512];
  int16_t toneBuf[TONE_FRAMES * 2];
  int16_t decBuf[TONE_FRAMES * 2];
  RingbufHandle_t ring = nullptr;
  TaskHandle_t task = nullptr;
  libhelix::MP3DecoderHelix mp3{mp3PcmCallback};
  volatile uint32_t pushed = 0, consumed = 0;
  volatile bool decoding = false, flushReq = false, stopReq = false;
  uint32_t speechStart = 0;

  void setRate(uint32_t r) { if (r != rate) { rate = r; i2s_set_sample_rates(I2S_SPK_PORT, r); } }
  void writeI2S(const int16_t* buf, int frames) {
    size_t bw = 0;
    i2s_write(I2S_SPK_PORT, buf, frames * 2 * sizeof(int16_t), &bw, pdMS_TO_TICKS(500));
  }

  static void decoderTask(void* arg) {
    AudioIO* a = (AudioIO*)arg;
    for (;;) {
      if (a->flushReq) {                       // botao: descarta o que falta e reinicia o decoder
        size_t n;
        void* p;
        while ((p = xRingbufferReceiveUpTo(a->ring, &n, 0, 4096)) != nullptr) { vRingbufferReturnItem(a->ring, p); }
        a->mp3.end();
        a->mp3.begin();
        i2s_zero_dma_buffer(I2S_SPK_PORT);
        a->consumed = a->pushed;
        a->decoding = false;
        a->flushReq = false;
        continue;
      }
      size_t n = 0;
      uint8_t* p = (uint8_t*)xRingbufferReceiveUpTo(a->ring, &n, pdMS_TO_TICKS(20), 1024);
      if (!p) { a->decoding = false; continue; }
      a->decoding = true;
      a->mp3.write(p, n);
      vRingbufferReturnItem(a->ring, p);
      a->consumed += n;
      if (a->consumed == a->pushed) a->decoding = false;
    }
  }
};

static void mp3PcmCallback(MP3FrameInfo& info, int16_t* pcm, size_t len, void* ref) {
  if (g_audio) g_audio->onPcm(info, pcm, len);
}
