#pragma once
// ============================================================================
// ALTO-FALANTES — 2x MAX98357A em I2S estereo, player MP3 (libhelix) e sons.
// - O MP3 vai para um anel de 256 KB na PSRAM; uma tarefa no nucleo 1 decodifica e toca.
// - Tudo o que sai no alto-falante tambem vai, a 16 kHz, para o anel de REFERENCIA que o
//   AFE usa no cancelamento de eco: o Jarvis ouve "Jarvis" mesmo enquanto fala.
// ============================================================================
#include <Arduino.h>
#include <math.h>
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "esp_heap_caps.h"
#include <MP3DecoderHelix.h>
#include "config.h"

class Speaker;
static Speaker* g_speaker = nullptr;
static void mp3PcmCallback(MP3FrameInfo& info, short* pcm, size_t len, void* ref);

static StreamBufferHandle_t makeStream(size_t bytes, size_t trigger, bool psram) {
  uint8_t* storage = (uint8_t*)heap_caps_malloc(bytes + 1, psram ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  StaticStreamBuffer_t* ctl = (StaticStreamBuffer_t*)heap_caps_malloc(sizeof(StaticStreamBuffer_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return (storage && ctl) ? xStreamBufferCreateStatic(bytes, trigger, storage, ctl) : nullptr;
}

class Speaker {
public:
  volatile int volume = DEFAULT_VOLUME_PCT;
  bool (*interrupt)() = nullptr;                // botao ou "Jarvis": interrompe a fala
  StreamBufferHandle_t reference = nullptr;     // 16 kHz mono para o AEC (lido pelo Listener)

  void begin() {
    g_speaker = this;
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.dma_desc_num = 6;
    cc.dma_frame_num = 480;
    cc.auto_clear = true;                       // silencio se a rede atrasar
    i2s_new_channel(&cc, &tx, nullptr);
    i2s_std_config_t sc = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SPK_SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = (gpio_num_t)SPK_PIN_BCLK, .ws = (gpio_num_t)SPK_PIN_WS,
                    .dout = (gpio_num_t)SPK_PIN_DOUT, .din = I2S_GPIO_UNUSED, .invert_flags = { false, false, false } },
    };
    i2s_channel_init_std_mode(tx, &sc);
    i2s_channel_enable(tx);
    rate = SPK_SAMPLE_RATE;

    lock = xSemaphoreCreateMutex();
    mp3Ring = makeStream(MP3_RING_BYTES, 1, true);
    reference = makeStream(16 * 1024, 1, false);
    xTaskCreatePinnedToCore(decoderTask, "mp3", 8192, this, 4, nullptr, 1);
  }

  void setVolume(int v) { volume = constrain(v, 0, 100); }
  static bool buttonPressed() { return digitalRead(PIN_WAKE_BUTTON) == LOW || digitalRead(PIN_EXTERNAL_BTN) == LOW; }

  // ---------------------------------------------------------------- sons sintetizados
  void tone(float hz, int ms, float pan = 0.0f, float amp = 0.30f) {
    waitIdle(4000);
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
      if (++n == FRAMES) { out(toneBuf, n); n = 0; }
    }
    if (n) out(toneBuf, n);
  }
  void wake()    { tone(880.0f, 60, -0.3f, 0.22f); tone(1318.5f, 80, 0.3f, 0.22f); }   // curto: voce ja pode falar
  void chime()   { tone(587.33f, 70, -0.5f); tone(880.0f, 95, 0.5f); tone(1174.66f, 110, 0.0f); }
  void blip()    { tone(987.77f, 50, 0, 0.2f); tone(783.99f, 60, 0, 0.2f); }
  void ok()      { tone(659.25f, 65, -0.3f); tone(987.77f, 110, 0.3f); }
  void noSpeech(){ tone(440.0f, 90, 0, 0.22f); tone(330.0f, 120, 0, 0.22f); }
  void error()   { tone(220.0f, 160); delay(60); tone(196.0f, 220); }
  void bell()    { tone(1318.5f, 120, -0.4f, 0.4f); tone(1567.98f, 160, 0.4f, 0.4f); delay(120); tone(1318.5f, 120, -0.4f, 0.4f); tone(1567.98f, 220, 0.4f, 0.4f); }
  void alarm()   { for (int i = 0; i < 3; ++i) { tone(1046.5f, 140, -0.6f, 0.45f); tone(1318.5f, 140, 0.6f, 0.45f); delay(60); } }
  void muted()   { tone(523.25f, 90, 0, 0.25f); tone(392.0f, 140, 0, 0.25f); }
  void playMode(const char* m) {
    if (!strcmp(m, "left")) tone(660, 350, -1.0f, 0.45f);
    else if (!strcmp(m, "right")) tone(880, 350, 1.0f, 0.45f);
    else if (!strcmp(m, "alarm")) alarm();
    else if (!strcmp(m, "ok")) ok();
    else if (!strcmp(m, "bell")) bell();
    else chime();
  }

  // ---------------------------------------------------------------- player MP3
  /** Poe bytes MP3 no anel (bloqueia se cheio: o audio dita o ritmo). false = fala interrompida. */
  bool push(const uint8_t* d, size_t n) {
    while (n > 0) {
      if (interrupt && interrupt()) stopReq = true;
      if (stopReq) return false;
      size_t sent = xStreamBufferSend(mp3Ring, d, n, pdMS_TO_TICKS(50));
      pushed += sent; d += sent; n -= sent;
    }
    return true;
  }
  void startSpeech() { stopReq = false; speaking = true; }
  /** Interrompe a fala (botao ou "Jarvis" durante a fala). */
  void stop() { stopReq = true; }
  bool stopped() const { return stopReq; }
  /** Espera tocar o que esta no anel. false = interrompido. */
  bool drain() {
    while (!isIdle()) {
      if (interrupt && interrupt()) stopReq = true;
      if (stopReq) { flush(); speaking = false; return false; }
      delay(10);
    }
    speaking = false;
    return !stopReq;
  }
  void flush() { flushReq = true; uint32_t t = millis(); while (flushReq && millis() - t < 600) delay(5); }
  bool isIdle() const { return consumed == pushed && !decoding; }
  bool isSpeaking() const { return speaking || !isIdle(); }
  void waitIdle(uint32_t ms) { uint32_t t = millis(); while (!isIdle() && millis() - t < ms) delay(5); }

  // chamado pela tarefa do decoder (callback do libhelix)
  void onPcm(MP3FrameInfo& info, int16_t* in, size_t len) {
    if (flushReq) return;
    if (info.samprate > 0 && (uint32_t)info.samprate != rate) setRate(info.samprate);
    const float g = volume / 100.0f;
    const int ch = info.nChans > 0 ? info.nChans : 1;
    const size_t frames = len / ch;
    int n = 0;
    for (size_t i = 0; i < frames; ++i) {
      int16_t l = in[i * ch], r = ch > 1 ? in[i * ch + 1] : l;
      decBuf[n * 2] = (int16_t)(l * g);
      decBuf[n * 2 + 1] = (int16_t)(r * g);
      if (++n == FRAMES) { out(decBuf, n); n = 0; }
    }
    if (n) out(decBuf, n);
  }

private:
  static const int FRAMES = 240;
  i2s_chan_handle_t tx = nullptr;
  uint32_t rate = SPK_SAMPLE_RATE;
  SemaphoreHandle_t lock = nullptr;
  StreamBufferHandle_t mp3Ring = nullptr;
  libhelix::MP3DecoderHelix mp3{mp3PcmCallback};
  int16_t toneBuf[FRAMES * 2];
  int16_t decBuf[FRAMES * 2];
  int16_t refBuf[FRAMES];
  float resamplePos = 0;
  volatile uint32_t pushed = 0, consumed = 0;
  volatile bool decoding = false, flushReq = false, stopReq = false, speaking = false;

  void setRate(uint32_t r) {
    if (r == rate) return;
    i2s_channel_disable(tx);
    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(r);
    i2s_channel_reconfig_std_clock(tx, &clk);
    i2s_channel_enable(tx);
    rate = r;
  }

  /** Escreve no I2S e alimenta a referencia do AEC (mono, reamostrada para 16 kHz). */
  void out(const int16_t* stereo, int frames) {
    xSemaphoreTake(lock, portMAX_DELAY);
    size_t bw = 0;
    i2s_channel_write(tx, stereo, frames * 2 * sizeof(int16_t), &bw, pdMS_TO_TICKS(500));
    const float step = (float)rate / MIC_SAMPLE_RATE;      // 1,5 para 24 kHz
    int n = 0;
    while (resamplePos < frames) {
      int i = (int)resamplePos;
      float f = resamplePos - i;
      int a = (stereo[i * 2] + stereo[i * 2 + 1]) / 2;
      int b = i + 1 < frames ? (stereo[(i + 1) * 2] + stereo[(i + 1) * 2 + 1]) / 2 : a;
      if (n < FRAMES) refBuf[n++] = (int16_t)(a + (b - a) * f);
      resamplePos += step;
    }
    resamplePos -= frames;
    if (n) xStreamBufferSend(reference, refBuf, n * sizeof(int16_t), 0);   // sem espaco: descarta (sem bloquear o audio)
    xSemaphoreGive(lock);
  }

  static void decoderTask(void* arg) {
    Speaker* s = (Speaker*)arg;
    static uint8_t buf[2048];
    s->mp3.begin();
    for (;;) {
      if (s->flushReq) {                          // descarta o que falta e reinicia o decoder
        size_t n;
        while ((n = xStreamBufferReceive(s->mp3Ring, buf, sizeof(buf), 0)) > 0) {}
        s->mp3.end();
        s->mp3.begin();
        s->consumed = s->pushed;
        s->decoding = false;
        s->flushReq = false;
        continue;
      }
      size_t n = xStreamBufferReceive(s->mp3Ring, buf, sizeof(buf), pdMS_TO_TICKS(20));
      if (!n) { s->decoding = false; continue; }
      s->decoding = true;
      s->mp3.write(buf, n);
      s->consumed += n;
      if (s->consumed == s->pushed) s->decoding = false;
    }
  }
};

static void mp3PcmCallback(MP3FrameInfo& info, short* pcm, size_t len, void* ref) {
  if (g_speaker) g_speaker->onPcm(info, (int16_t*)pcm, len);
}
