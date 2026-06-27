#include "carusos_config.h"

#if ENABLE_APP_MIC_TEST
#include <Arduino.h>
#include <Wire.h>
#include "ESP_I2S.h"   // new I2S driver (same one the speaker uses) - NOT driver/i2s.h
#include "es7210.h"    // ES7210 chip config over I2C only
#include "mic_es7210.h"

// The ESP-IDF forbids mixing the legacy (driver/i2s.h) and new I2S drivers in
// one firmware. The speaker uses ESP_I2S, so the mic must too: we read the
// ES7210 ADC through a second ESP_I2S instance on I2S_NUM_1. The ES7210 chip
// itself is configured purely over I2C (es7210_*), which does not touch I2S.

#define MIC_FRAME_SAMPLES 256

static I2SClass     mic_i2s(I2S_NUM_1);
static volatile bool mic_active = false;
static volatile int  mic_level = 0; // 0-100
static TaskHandle_t  TaskMic = NULL;

static void Task_Mic(void *pv) {
    int16_t *buf = (int16_t *)malloc(MIC_FRAME_SAMPLES * sizeof(int16_t));
    while (mic_active) {
        if (!buf) break;
        size_t n = mic_i2s.readBytes((char *)buf, MIC_FRAME_SAMPLES * sizeof(int16_t));
        int samples = n / sizeof(int16_t);
        int32_t peak = 0;
        for (int i = 0; i < samples; i++) {
            int32_t a = buf[i] < 0 ? -(int32_t)buf[i] : buf[i];
            if (a > peak) peak = a;
        }
        int lvl = (int)((peak * 100) / 32767);
        mic_level = lvl > 100 ? 100 : lvl;
    }
    if (buf) free(buf);
    mic_level = 0;
    TaskMic = NULL;
    vTaskDelete(NULL);
}

void mic_capture_start() {
    if (mic_active) return;

    // 1) Configure the ES7210 ADC chip over I2C (no I2S driver involved here)
    audio_hal_codec_config_t cfg = {
        .adc_input  = AUDIO_HAL_ADC_INPUT_ALL,
        .dac_output = AUDIO_HAL_DAC_OUTPUT_ALL,
        .codec_mode = AUDIO_HAL_CODEC_MODE_ENCODE,
        .i2s_iface  = {
            .mode    = AUDIO_HAL_MODE_SLAVE,
            .fmt     = AUDIO_HAL_I2S_NORMAL,
            .samples = AUDIO_HAL_16K_SAMPLES,
            .bits    = AUDIO_HAL_BIT_LENGTH_16BITS,
        },
    };
    es7210_adc_init(&Wire, &cfg);
    es7210_adc_config_i2s(cfg.codec_mode, &cfg.i2s_iface);
    es7210_adc_set_gain((es7210_input_mics_t)(ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2), GAIN_30DB);
    es7210_adc_set_gain((es7210_input_mics_t)(ES7210_INPUT_MIC3 | ES7210_INPUT_MIC4), GAIN_30DB);
    es7210_adc_ctrl_state(cfg.codec_mode, AUDIO_HAL_CTRL_START);

    // 2) Read the ADC via the new I2S driver (RX only: dout = -1) on I2S_NUM_1
    mic_i2s.setPins(PIN_ES7210_BCLK, PIN_ES7210_LRCK, -1, PIN_ES7210_DIN, PIN_ES7210_MCLK);
    if (!mic_i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
        Serial.println("[Mic] I2S RX init failed!");
        es7210_adc_deinit();
        return;
    }
    mic_i2s.setTimeout(80); // bound readBytes() so the task can exit promptly

    mic_active = true;
    xTaskCreatePinnedToCore(Task_Mic, "TaskMic", 4096, NULL, 1, &TaskMic, 0);
    Serial.println("[Mic] Capture started.");
}

void mic_capture_stop() {
    if (!mic_active) return;
    mic_active = false;
    vTaskDelay(pdMS_TO_TICKS(200)); // let Task_Mic finish its current read and exit
    mic_i2s.end();
    es7210_adc_deinit();
    mic_level = 0;
    Serial.println("[Mic] Capture stopped. Reboot to use the speaker again.");
}

int mic_capture_level() {
    return mic_level;
}
#endif // ENABLE_APP_MIC_TEST
