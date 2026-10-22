#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "lv_conf.h"
#include <lvgl.h>
#include "pin_config.h"
#include "TouchDrvGT911.hpp"
#include "Arduino_GFX_Library.h"
#include "lvgl_port.h"

TouchDrvGT911 GT911;
int16_t x[5], y[5];

uint32_t screenWidth;
uint32_t screenHeight;
uint32_t bufSize;
lv_display_t *disp;
lv_color_t *disp_draw_buf;

// #define RGB_PANEL
#define DIRECT_RENDER_MODE

// The XCA9554 expander driver caches its output register in RAM and does a
// read-modify-write of that cache on every digitalWrite(). The I2C HAL lock
// makes each transaction atomic, but NOT the cache, so every runtime write to
// the expander (today only the audio amp, from Core 0's Task_Audio) goes
// through this mutex. Boot-time writes in lvgl_port_init() run before any
// other task exists.
static SemaphoreHandle_t expander_mutex = NULL;

// Set by my_disp_flush() whenever LVGL renders new content; consumed by
// lvgl_port_flush() so we only push the ~460 KB framebuffer to the panel when
// something actually changed (huge CPU/bandwidth win on a static screen).
static volatile bool fb_dirty = true;

Arduino_XCA9554SWSPI *expander = new Arduino_XCA9554SWSPI(
  7, 0, 2, 1, &Wire, 0x20);

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  17 /* DE */, 3 /* VSYNC */, 46 /* HSYNC */, 9 /* PCLK */,
  10 /* B0 */, 11 /* B1 */, 12 /* B2 */, 13 /* B3 */, 14 /* B4 */,
  21 /* G0 */, 8 /* G1 */, 18 /* G2 */, 45 /* G3 */, 38 /* G4 */, 39 /* G5 */,
  40 /* R0 */, 41 /* R1 */, 42 /* R2 */, 2 /* R3 */, 1 /* R4 */,
  1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
  1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
  480 /* width */, 480 /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */,
  expander, GFX_NOT_DEFINED /* RST */, st7701_type1_init_operations, sizeof(st7701_type1_init_operations));

uint32_t millis_cb(void) {
  return millis();
}

void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
#ifndef DIRECT_RENDER_MODE
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
#endif
  // LVGL rendered something: mark the framebuffer for the next panel push.
  fb_dirty = true;
  lv_disp_flush_ready(disp);
}

static bool backlight_state = true;
static volatile uint8_t backlight_pct = 100; // brightness when lit, BL_MIN_PCT..100

// Never fully dark while "on": at 0% the screen would look dead.
#define BL_MIN_PCT 10

// The backlight is active LOW (measured on the board 2026-10-19: GPIO4 high =
// dark, low = lit). PWM duty is inverted accordingly: 0 = full brightness.
#define BL_DUTY(on) ((on) ? (255 - (backlight_pct * 255) / 100) : 255)

// Backlight on GPIO4 (LEDC PWM). Before v0.46.1 this toggled expander pin 5,
// which is actually the touch reset: "Sleep" reset the touch controller
// instead of turning the screen off.
void lvgl_port_set_backlight(bool state) {
    backlight_state = state;
    ledcWrite(PIN_LCD_BL, BL_DUTY(state));
}

// Called from the Options slider (Core 1) and once at boot from the backend
// after reading NVS (Core 0). Only touches the PWM duty, never LVGL.
void lvgl_port_set_brightness(uint8_t pct) {
    if (pct < BL_MIN_PCT) pct = BL_MIN_PCT;
    if (pct > 100) pct = 100;
    backlight_pct = pct;
    if (backlight_state) ledcWrite(PIN_LCD_BL, BL_DUTY(true));
}

// GPIOs float in deep sleep and the backlight would come back on, so drive
// GPIO4 high (= off) and hold it. lvgl_port_init() releases the hold on the next boot.
void lvgl_port_prepare_deep_sleep() {
    backlight_state = false;
    ledcDetach(PIN_LCD_BL);
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, HIGH);
    gpio_hold_en((gpio_num_t)PIN_LCD_BL);
    gpio_deep_sleep_hold_en();
    lvgl_port_set_audio_amp(false);
}

void lvgl_port_set_audio_amp(bool state) {
    if (!expander) return;
    if (expander_mutex) xSemaphoreTake(expander_mutex, portMAX_DELAY);
    expander->pinMode(EXP_PIN_AMP, OUTPUT);
    expander->digitalWrite(EXP_PIN_AMP, state ? HIGH : LOW);
    if (expander_mutex) xSemaphoreGive(expander_mutex);
}

/*Read the touchpad*/
void my_touchpad_read(lv_indev_t * indev_driver, lv_indev_data_t * data) {
  uint8_t touched = GT911.getPoint(x, y, GT911.getSupportTouchPoint());
  if (touched > 0) {
    // If backlight is off, turn it on and consume the touch (don't pass to LVGL)
    if (!backlight_state) {
        lvgl_port_set_backlight(true);
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    int16_t touchX = x[0];
    int16_t touchY = y[0];

    switch (gfx->getRotation()) {
      case 1:
        touchX = y[0];
        touchY = gfx->height() - x[0];
        break;
      case 2:
        touchX = gfx->width() - x[0];
        touchY = gfx->height() - y[0];
        break;
      case 3:
        touchX = gfx->width() - y[0];
        touchY = x[0];
        break;
    }
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void rounder_event_cb(lv_event_t *e) {
  lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
  uint16_t x1 = area->x1;
  uint16_t x2 = area->x2;
  uint16_t y1 = area->y1;
  uint16_t y2 = area->y2;
  area->x1 = (x1 >> 1) << 1;
  area->y1 = (y1 >> 1) << 1;
  area->x2 = ((x2 >> 1) << 1) + 1;
  area->y2 = ((y2 >> 1) << 1) + 1;
}

void lvgl_port_init() {
  expander_mutex = xSemaphoreCreateMutex();

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  // GT911 reset with INT held low, which selects I2C address 0x5D
  // (GT911_SLAVE_ADDRESS_L). Afterwards INT is released: the GT911 drives it.
  expander->pinMode(EXP_PIN_TP_RST, OUTPUT);
  expander->pinMode(EXP_PIN_TP_INT, OUTPUT);
  expander->digitalWrite(EXP_PIN_TP_INT, LOW);
  delay(200);
  expander->digitalWrite(EXP_PIN_TP_RST, LOW);
  delay(200);
  expander->digitalWrite(EXP_PIN_TP_RST, HIGH);
  delay(200);
  expander->pinMode(EXP_PIN_TP_INT, INPUT);

  // Backlight: GPIO4 PWM (active low), full on. Released from a deep-sleep hold first, if any.
  gpio_hold_dis((gpio_num_t)PIN_LCD_BL);
  ledcAttach(PIN_LCD_BL, 20000, 8);
  ledcWrite(PIN_LCD_BL, BL_DUTY(true));

  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }
  gfx->fillScreen(RGB565_BLACK);

  GT911.setPins(-1, -1);
  if (!GT911.begin(Wire, GT911_SLAVE_ADDRESS_L, PIN_I2C_SDA, PIN_I2C_SCL)) {
    Serial.println("Failed to find GT911 - check your wiring!");
  }
  GT911.setMaxTouchPoint(1);

  lv_init();
  lv_tick_set_cb(millis_cb);
  // Note: the LVGL log print callback is registered in the .ino (my_log_cb),
  // which routes logs to backend_log(). Do not register a second one here.

  screenWidth = gfx->width();
  screenHeight = gfx->height();

#ifdef DIRECT_RENDER_MODE
  bufSize = screenWidth * screenHeight;
#else
  bufSize = screenWidth * 50;
#endif

#ifdef ESP32
#if defined(DIRECT_RENDER_MODE) && (defined(CANVAS) || defined(RGB_PANEL) || defined(DSI_PANEL))
  disp_draw_buf = (lv_color_t *)gfx->getFramebuffer();
#else   
  disp_draw_buf = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!disp_draw_buf) {
    disp_draw_buf = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_8BIT);
  }
#endif
#else
  disp_draw_buf = (lv_color_t *)malloc(bufSize * 2);
#endif

  if (!disp_draw_buf) {
    Serial.println("LVGL disp_draw_buf allocate failed!");
  } else {
    disp = lv_display_create(screenWidth, screenHeight);
    lv_display_set_flush_cb(disp, my_disp_flush);
#ifdef DIRECT_RENDER_MODE
    lv_display_set_buffers(disp, disp_draw_buf, NULL, bufSize * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
#else
    lv_display_set_buffers(disp, disp_draw_buf, NULL, bufSize * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touchpad_read);
    lv_display_add_event_cb(disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
  }
}

void lvgl_port_flush() {
#ifdef DIRECT_RENDER_MODE
#if defined(CANVAS) || defined(RGB_PANEL) || defined(DSI_PANEL)
  gfx->flush();
#else
  // Only push the framebuffer when LVGL actually drew something.
  if (fb_dirty) {
    gfx->draw16bitRGBBitmap(0, 0, (uint16_t *)disp_draw_buf, screenWidth, screenHeight);
    fb_dirty = false;
  }
#endif
#else
#ifdef CANVAS
  gfx->flush();
#endif
#endif
}
