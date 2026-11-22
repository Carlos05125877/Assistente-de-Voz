#pragma once
// ============================================================================
// MICROFONE — INMP441 + ESP-SR AFE (cancelamento de eco, supressao de ruido, VAD) + WakeNet.
// Fica sempre escutando "Jarvis" sem internet. Depois da palavra (ou do botao), a fala limpa
// vai para um anel de 14 s na PSRAM, que o laco principal envia ao Groq ENQUANTO voce fala.
// O fim da fala e detectado pelo VAD do AFE.
// ============================================================================
#include <Arduino.h>
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "esp_afe_sr_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_config.h"
#include "esp_wn_iface.h"
#include "model_path.h"
#include "config.h"
#include "speaker.h"

class Listener {
public:
  // configuracao (vem do painel pelo sync; persistida na NVS)
  volatile bool wakeEnabled = true;   // ouvir a palavra "Jarvis"
  volatile bool bargeIn = true;       // "Jarvis" interrompe enquanto ele fala
  volatile bool micMuted = false;     // modo privacidade (botao longo)
  // eventos lidos pelo laco principal
  volatile bool wakeDetected = false;
  volatile uint32_t wakeCount = 0;

  bool begin(Speaker* spk, bool sensitive) {
    speaker = spk;
    // ---- I2S do microfone (32-bit, canal esquerdo)
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    cc.dma_desc_num = 6;
    cc.dma_frame_num = 320;
    i2s_new_channel(&cc, nullptr, &rx);
    i2s_std_config_t sc = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = (gpio_num_t)MIC_PIN_BCLK, .ws = (gpio_num_t)MIC_PIN_WS,
                    .dout = I2S_GPIO_UNUSED, .din = (gpio_num_t)MIC_PIN_DIN, .invert_flags = { false, false, false } },
    };
    sc.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    i2s_channel_init_std_mode(rx, &sc);
    i2s_channel_enable(rx);

    // ---- AFE: "MR" = 1 microfone + referencia do alto-falante (AEC)
    srmodel_list_t* models = esp_srmodel_init("model");
    if (!models) { Serial.println("❌ Modelos do ESP-SR ausentes: use o Partition Scheme \"ESP SR 16M\" (veja o README)"); return false; }
    // "Jarvis" se o modelo estiver gravado (srmodels_jarvis.bin); senao a palavra padrao ("Hi ESP")
    char* wn = esp_srmodel_filter(models, ESP_WN_PREFIX, "jarvis");
    data = createAfe(models, wn, sensitive);
    if (!data && wn) { Serial.println("⚠️ Modelo Jarvis incompatível; usando a palavra padrão"); data = createAfe(models, nullptr, sensitive); }
    if (!data) { Serial.println("❌ Falha ao iniciar o AFE"); return false; }

    capture = makeStream(CAPTURE_RING_BYTES, 1, true);
    xTaskCreatePinnedToCore(feedTask, "afe_feed", 6144, this, 5, nullptr, 0);
    xTaskCreatePinnedToCore(fetchTask, "afe_fetch", 6144, this, 5, nullptr, 1);
    ready = true;
    return true;
  }

  esp_afe_sr_data_t* createAfe(srmodel_list_t* models, char* wn, bool sensitive) {
    afe_config_t* cfg = afe_config_init("MR", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (!cfg) return nullptr;
    if (wn) cfg->wakenet_model_name = wn;
    cfg->aec_init = true;
    cfg->ns_init = true;
    cfg->vad_init = true;
    cfg->wakenet_init = true;
    cfg->wakenet_mode = sensitive ? DET_MODE_95 : DET_MODE_90;
    cfg->afe_perferred_core = 1;
    cfg->afe_perferred_priority = 5;
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    Serial.printf("🎙️ Palavra de ativação: %s (%s)\n", cfg->wakenet_model_name ? cfg->wakenet_model_name : "?", sensitive ? "sensível" : "normal");
    afe = esp_afe_handle_from_config(cfg);
    esp_afe_sr_data_t* d = afe ? afe->create_from_config(cfg) : nullptr;
    afe_config_free(cfg);
    return d;
  }

  bool isReady() const { return ready; }
  /** Comeca a gravar sem a palavra (botao ou pergunta de continuacao). */
  void startCapture() { if (ready) startReq = true; }
  bool capturing() const { return mode == CAPTURE || startReq; }
  bool heardSpeech() const { return speechSeen; }
  /** Le a fala capturada. 0 = nada no momento (veja capturing()). */
  size_t read(uint8_t* buf, size_t n, uint32_t waitMs) { return xStreamBufferReceive(capture, buf, n, pdMS_TO_TICKS(waitMs)); }
  void cancelCapture() { stopReq = true; }

private:
  enum Mode { IDLE, CAPTURE };
  Speaker* speaker = nullptr;
  i2s_chan_handle_t rx = nullptr;
  const esp_afe_sr_iface_t* afe = nullptr;
  esp_afe_sr_data_t* data = nullptr;
  StreamBufferHandle_t capture = nullptr;
  volatile Mode mode = IDLE;
  volatile bool startReq = false, stopReq = false, speechSeen = false, ready = false;
  uint32_t capStart = 0, lastSpeech = 0;

  void beginCapture() {
    xStreamBufferReset(capture);
    speechSeen = false;
    capStart = lastSpeech = millis();
    mode = CAPTURE;
    afe->disable_wakenet(data);                     // economiza CPU enquanto grava
  }
  void endCapture() {
    mode = IDLE;
    afe->enable_wakenet(data);
  }

  static void feedTask(void* arg) {
    Listener* L = (Listener*)arg;
    const int chunk = L->afe->get_feed_chunksize(L->data);
    const int nch = L->afe->get_feed_channel_num(L->data);       // 2 (M + R)
    int32_t* raw = (int32_t*)heap_caps_malloc(chunk * sizeof(int32_t), MALLOC_CAP_INTERNAL);
    int16_t* ref = (int16_t*)heap_caps_malloc(chunk * sizeof(int16_t), MALLOC_CAP_INTERNAL);
    int16_t* in = (int16_t*)heap_caps_malloc(chunk * nch * sizeof(int16_t), MALLOC_CAP_INTERNAL);
    const size_t maxBacklog = chunk * sizeof(int16_t) * 6;          // ~200 ms: mantem a referencia alinhada
    for (;;) {
      size_t br = 0;
      if (i2s_channel_read(L->rx, raw, chunk * sizeof(int32_t), &br, pdMS_TO_TICKS(200)) != ESP_OK || br < chunk * sizeof(int32_t)) continue;
      StreamBufferHandle_t rs = L->speaker->reference;
      size_t avail = xStreamBufferBytesAvailable(rs);
      while (avail > maxBacklog) {                                   // descarta atraso acumulado
        size_t drop = avail - maxBacklog > chunk * sizeof(int16_t) ? chunk * sizeof(int16_t) : avail - maxBacklog;
        xStreamBufferReceive(rs, ref, drop, 0);
        avail -= drop;
      }
      size_t got = xStreamBufferReceive(rs, ref, chunk * sizeof(int16_t), 0) / sizeof(int16_t);
      for (int i = 0; i < chunk; ++i) {
        int32_t v = raw[i] >> MIC_SHIFT_BITS;
        v = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
        in[i * nch] = L->micMuted ? 0 : (int16_t)v;
        if (nch > 1) in[i * nch + 1] = i < (int)got ? ref[i] : 0;
      }
      L->afe->feed(L->data, in);
    }
  }

  static void fetchTask(void* arg) {
    Listener* L = (Listener*)arg;
    for (;;) {
      afe_fetch_result_t* r = L->afe->fetch(L->data);
      if (!r || r->ret_value == ESP_FAIL) continue;
      const uint32_t now = millis();
      if (L->stopReq) { L->stopReq = false; L->startReq = false; if (L->mode == CAPTURE) L->endCapture(); }
      if (L->mode == IDLE) {
        if (L->startReq) { L->startReq = false; L->beginCapture(); }
        else if (r->wakeup_state == WAKENET_DETECTED && L->wakeEnabled && !L->micMuted &&
                 (L->bargeIn || !L->speaker->isSpeaking())) {
          L->wakeCount++;
          L->wakeDetected = true;
          L->beginCapture();
        }
        continue;
      }
      // ---- CAPTURE
      if (r->data && r->data_size > 0) xStreamBufferSend(L->capture, r->data, r->data_size, 0);
      if (r->vad_state == VAD_SPEECH) { L->speechSeen = true; L->lastSpeech = now; }
      const bool silenceEnd = L->speechSeen && now - L->lastSpeech >= END_SILENCE_MS;
      const bool noSpeech = !L->speechSeen && now - L->capStart >= NO_SPEECH_MS;
      const bool tooLong = now - L->capStart >= (uint32_t)MAX_RECORD_SECONDS * 1000UL;
      if (silenceEnd || noSpeech || tooLong) L->endCapture();
    }
  }
};
