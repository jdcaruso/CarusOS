#pragma once

// Shared API between the shell (ui_core.cpp) and the apps in src/apps/.
// Arduino compiles src/ recursively but does not put the sketch root on the
// include path, hence the relative includes.
#include "../../carusos_config.h"
#include "../../lv_conf.h"
#include <lvgl.h>
#include "../../language.h"
#include "../../backend_core.h"

// Stable app identifiers, used as registry / icon-grid keys.
typedef enum {
    APP_CALC = 0,
    APP_DATETIME,
    APP_AUDIO,
    APP_SETTINGS,
    APP_SLEEP,
    APP_SYSINFO,
    APP_OTA,
    APP_EXPLORER,
    APP_OPTIONS,
    APP_GALLERY,
    APP_ANIM,
    APP_MIC,
    APP_IMU,
    APP_DEEPSLEEP,
} carusos_app_id_t;

// Fills an app window. The title bar and back button already exist; `content`
// is a default label below the bar that the app may use or hide.
typedef void (*app_build_fn)(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);

// ---------------------------------------------------------------------------
// Shell helpers (implemented in ui_core.cpp)
// ---------------------------------------------------------------------------
// Everything an app owns must die with its screen. Use these instead of
// globals: the screen is auto-deleted after the Back transition, and its
// LV_EVENT_DELETE handlers release what was registered here.

// Adds a launcher icon for `app_id` (hidden when the app is disabled, unless
// CARUSOS_SHOW_DISABLED_APPS is 1).
void ui_add_app_icon(lv_obj_t * parent, int app_id);

// lv_timer that is deleted together with `screen`.
lv_timer_t * ui_screen_timer(lv_obj_t * screen, lv_timer_cb_t cb, uint32_t period_ms, void * user_data);

// Zeroed lv_malloc block that is freed together with `screen`. UI core only:
// memory handed to a Core 0 task must use plain malloc (see the Gallery app).
void * ui_screen_alloc(lv_obj_t * screen, size_t size);

// ---------------------------------------------------------------------------
// App builders. Optional apps are compiled only when their flag is on; the
// registry in ui_core.cpp falls back to NULL for the ones compiled out.
// ---------------------------------------------------------------------------
void app_build_calc(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
void app_build_datetime(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
void app_build_settings(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
void app_build_sysinfo(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
void app_build_options(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
#if CARUSOS_USE_AUDIO
void app_build_audio(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
#endif
#if CARUSOS_USE_OTA
void app_build_ota(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
#endif
#if CARUSOS_APP_EXPLORER
void app_build_explorer(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
#endif
#if CARUSOS_APP_GALLERY
void app_build_gallery(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
#endif
#if ENABLE_APP_ANIM
void app_build_anim(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
#endif
#if ENABLE_APP_MIC_TEST
void app_build_mic(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
#endif
#if ENABLE_APP_IMU
void app_build_imu(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
#endif
