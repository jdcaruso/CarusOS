// CarusOS - main entry point. Version is defined by CARUSOS_VERSION in carusos_config.h
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "lv_conf.h"
#include "carusos_config.h"
#include "lvgl_port.h"
#include "ui_core.h"
#include "backend_core.h"
#include <lvgl.h>

TaskHandle_t TaskGUI;

void Task_GUI(void *pvParameters) {
  while (1) {
    lv_task_handler();
    lvgl_port_flush();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void my_log_cb(lv_log_level_t level, const char * buf) {
    backend_log(buf);
}

void setup() {
  Serial.begin(115200);
  delay(1500); // Esperar a que el USB CDC se estabilice para evitar simbolos basura
  Serial.println("\n\nBooting CarusOS...");

  // Initialize hardware and LVGL
  lvgl_port_init();
  lv_log_register_print_cb(my_log_cb);

  // Initialize UI (Splash screen)
  ui_core_init();

  // Initialize Backend Tasks (WiFi, Audio, etc) on Core 0
  backend_core_init();

  // Create the GUI Task pinned to Core 1
  xTaskCreatePinnedToCore(
    Task_GUI,
    "TaskGUI",
    32768,
    NULL,
    1,
    &TaskGUI,
    1
  );

  Serial.println("CarusOS Initialized.");
}

void loop() {
  // Empty loop. Handled by FreeRTOS tasks.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
