#include "carusos_config.h"
#include "backend_core.h"
#include "imu_qmi8658.h"
#include "lvgl_port.h"
#include "lv_conf.h"
#include <lvgl.h>
#include "ui_core.h"

static lv_obj_t * boot_screen;
static lv_obj_t * main_screen;
static lv_obj_t * boot_label;
static lv_obj_t * wifi_label;
static lv_obj_t * time_label;
static lv_obj_t * app_time_label = NULL;
static lv_obj_t * app_wifi_label = NULL;
static lv_obj_t * ntp_live_label = NULL; // For live NTP updates inside the app
static lv_obj_t * sys_info_stats_label = NULL; // For live Sys Info updates
static lv_obj_t * mic_level_bar = NULL; // Mic test level meter
static lv_timer_t * mic_timer = NULL;   // Updates the mic level meter
static lv_obj_t * datetime_date_label = NULL; // Date & Time app: date line
static lv_obj_t * imu_ball = NULL;      // IMU app: tilt ball
static lv_obj_t * imu_box = NULL;       // IMU app: bounding box
static lv_timer_t * imu_timer = NULL;   // Reads the IMU and moves the ball
static lv_timer_t * boot_timer;
static lv_timer_t * ui_poll_timer;
static int boot_step = 0;

#include "language.h"

// ---------------------------------------------------------------------------
// App Registry
// ---------------------------------------------------------------------------
// Each app provides a build() function that fills the window content area.
// To add a new app: write an app_build_xxx() function and add one row to
// g_apps[] below. The launcher and menus pick it up automatically.
//
// - enabled == false  -> icon is greyed out and not clickable.
// - build   == NULL   -> "action" app (no window), handled in the launcher
//                        (e.g. Sleep turns the backlight off).

typedef void (*app_build_fn)(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);

typedef struct {
    int            id;
    const char *   icon;     // LV_SYMBOL_* glyph
    const char *   name;     // label shown under the icon
    bool           enabled;  // greyed-out and non-clickable when false
    app_build_fn   build;    // NULL = special action (handled in launcher)
} carusos_app_t;

// Forward declarations of the per-app builders
static void app_build_calc(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
static void app_build_ntp(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
static void app_build_audio(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
static void app_build_settings(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
static void app_build_sysinfo(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
static void app_build_ota(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
static void app_build_explorer(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
static void app_build_options(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
static void app_build_gallery(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
#if ENABLE_APP_ANIM
static void app_build_anim(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
#endif
#if ENABLE_APP_MIC_TEST
static void app_build_mic(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
#endif
#if ENABLE_APP_IMU
static void app_build_imu(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label);
#endif

static carusos_app_t g_apps[] = {
    { 0, LV_SYMBOL_PLUS,      TXT_APP_CALC_TITLE, true,                 app_build_calc },
    { 1, LV_SYMBOL_BELL,      "Fecha y Hora",     true,                 app_build_ntp },
    { 2, LV_SYMBOL_AUDIO,     "Audio",            CARUSOS_USE_AUDIO,    app_build_audio },
    { 3, LV_SYMBOL_SETTINGS,  TXT_APP_INFO_TITLE, true,                 app_build_settings },
    { 4, LV_SYMBOL_POWER,     TXT_SCREEN_SLEEP,   true,                 NULL /* action: Sleep */ },
    { 5, LV_SYMBOL_FILE,      "Sys Info",         true,                 app_build_sysinfo },
    { 6, LV_SYMBOL_DOWNLOAD,  "Update",           CARUSOS_USE_OTA,      app_build_ota },
    { 7, LV_SYMBOL_DIRECTORY, "Archivos",         CARUSOS_APP_EXPLORER, app_build_explorer },
    { 8, LV_SYMBOL_SETTINGS,  "Opciones",         true,                 app_build_options },
    { 9, LV_SYMBOL_IMAGE,     "Galeria",          CARUSOS_APP_GALLERY,  app_build_gallery },
#if ENABLE_APP_ANIM
    { 10, LV_SYMBOL_PLAY,     "Animacion",        true,                 app_build_anim },
#endif
#if ENABLE_APP_MIC_TEST
    { 11, LV_SYMBOL_AUDIO,    "Mic Test",         true,                 app_build_mic },
#endif
#if ENABLE_APP_IMU
    { 12, LV_SYMBOL_GPS,      "Movimiento",       true,                 app_build_imu },
#endif
};
static const int g_app_count = sizeof(g_apps) / sizeof(g_apps[0]);

static carusos_app_t * app_by_id(int id) {
    for (int i = 0; i < g_app_count; i++) {
        if (g_apps[i].id == id) return &g_apps[i];
    }
    return NULL;
}

// Forward declarations
static void create_app_window(carusos_app_t * app, lv_obj_t * return_screen);
static void add_app_icon(lv_obj_t * parent, int app_id);
static void app_launcher_event_cb(lv_event_t * e);

// App Window Management
static void app_back_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        ntp_live_label = NULL; // Clear pointer when leaving NTP app
        datetime_date_label = NULL; // Clear pointer when leaving Date & Time app
        sys_info_stats_label = NULL; // Clear pointer when leaving Sys Info app
        app_wifi_label = NULL;
        app_time_label = NULL;
#if ENABLE_APP_MIC_TEST
        // Leaving the Mic Test app: stop capture and tear down its timer
        if (mic_level_bar != NULL) {
            backend_mic_stop();
            if (mic_timer) { lv_timer_delete(mic_timer); mic_timer = NULL; }
            mic_level_bar = NULL;
        }
#endif
#if ENABLE_APP_IMU
        // Leaving the IMU app: tear down its timer
        if (imu_ball != NULL) {
            if (imu_timer) { lv_timer_delete(imu_timer); imu_timer = NULL; }
            imu_ball = NULL;
            imu_box = NULL;
        }
#endif
        lv_obj_t * return_screen = (lv_obj_t *)lv_event_get_user_data(e);
        if (return_screen) {
            // Free Gallery image buffer if we are returning from App 9
            void * dsc = lv_obj_get_user_data(lv_scr_act());
            if (dsc) {
                lv_image_dsc_t * img_dsc = (lv_image_dsc_t *)dsc;
                if (img_dsc->data) free((void*)img_dsc->data);
                free(img_dsc);
                lv_obj_set_user_data(lv_scr_act(), NULL);
            }

            lv_scr_load_anim_t anim = backend_get_animations() ? LV_SCR_LOAD_ANIM_FADE_ON : LV_SCR_LOAD_ANIM_NONE;
            lv_scr_load_anim(return_screen, anim, 200, 0, true);
        }
    }
}

// Audio App Callbacks
static void audio_play_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = (lv_obj_t *)lv_event_get_target(e);
    if(code == LV_EVENT_VALUE_CHANGED) {
        bool playing = lv_obj_has_state(btn, LV_STATE_CHECKED);
        lv_obj_t * label = lv_obj_get_child(btn, 0);
        lv_label_set_text(label, playing ? LV_SYMBOL_STOP " " TXT_AUDIO_STOP : LV_SYMBOL_PLAY " " TXT_AUDIO_PLAY);
        backend_set_audio_state(playing);
    }
}

static void audio_vol_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e);
    if(code == LV_EVENT_VALUE_CHANGED) {
        int vol = lv_slider_get_value(slider);
        backend_set_audio_volume(vol);
    }
}

// ---------------------------------------------------------------------------
// Per-app builders
// ---------------------------------------------------------------------------

static void app_build_calc(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, TXT_APP_CALC_TITLE);
    lv_label_set_text(content, "1 + 1 = 2");
    lv_obj_center(content);
}

static void app_build_ntp(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, TXT_APP_NTP_TITLE);
    lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN); // hide default content label

    // Big live time (HH:MM:SS)
    ntp_live_label = lv_label_create(app_screen);
    lv_label_set_text(ntp_live_label, "--:--:--");
    lv_obj_set_style_text_font(ntp_live_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(ntp_live_label, lv_color_hex(0x00FF00), 0);
    lv_obj_align(ntp_live_label, LV_ALIGN_CENTER, 0, -20);

    // Date line (DD/MM/YYYY)
    datetime_date_label = lv_label_create(app_screen);
    lv_label_set_text(datetime_date_label, "--/--/----");
    lv_obj_set_style_text_font(datetime_date_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(datetime_date_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(datetime_date_label, LV_ALIGN_CENTER, 0, 45);
}

static void app_build_audio(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, TXT_APP_AUDIO_TITLE);
    lv_label_set_text(content, TXT_APP_AUDIO_DESC);

    // Play Button
    lv_obj_t * play_btn = lv_button_create(app_screen);
    lv_obj_set_size(play_btn, 150, 60);
    lv_obj_align(play_btn, LV_ALIGN_CENTER, 0, -20);
    lv_obj_add_flag(play_btn, LV_OBJ_FLAG_CHECKABLE); // Make it a toggle button
    lv_obj_add_event_cb(play_btn, audio_play_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t * play_label = lv_label_create(play_btn);
    lv_label_set_text(play_label, LV_SYMBOL_PLAY " " TXT_AUDIO_PLAY);
    lv_obj_center(play_label);

    // Volume Slider
    lv_obj_t * slider = lv_slider_create(app_screen);
    lv_obj_set_size(slider, 200, 20);
    lv_obj_align_to(slider, play_btn, LV_ALIGN_OUT_BOTTOM_MID, 0, 40);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, backend_get_audio_volume(), LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, audio_vol_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * vol_label = lv_label_create(app_screen);
    lv_label_set_text(vol_label, LV_SYMBOL_VOLUME_MAX " " TXT_AUDIO_VOL);
    lv_obj_align_to(vol_label, slider, LV_ALIGN_OUT_TOP_MID, 0, -10);
}

static void app_build_settings(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, TXT_APP_INFO_TITLE);
    lv_label_set_text(content, ""); // Clear default top-aligned text

    lv_obj_t * ver_label = lv_label_create(app_screen);
    lv_label_set_text_fmt(ver_label, "CarusOS %s", CARUSOS_VERSION);
    lv_obj_set_style_text_color(ver_label, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_font(ver_label, &lv_font_montserrat_14, 0);
    lv_obj_align(ver_label, LV_ALIGN_BOTTOM_MID, 0, -10);

    // Submenu Grid (identical to desktop)
    lv_obj_t * app_grid = lv_obj_create(app_screen);
    lv_obj_set_size(app_grid, LV_PCT(90), LV_PCT(65));
    lv_obj_align(app_grid, LV_ALIGN_BOTTOM_MID, 0, -35);
    lv_obj_set_style_bg_opa(app_grid, 0, 0); // Transparent
    lv_obj_set_style_border_width(app_grid, 0, 0);

    lv_obj_set_layout(app_grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(app_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(app_grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(app_grid, 20, 0);
    lv_obj_set_style_pad_column(app_grid, 20, 0);

    add_app_icon(app_grid, 1); // NTP Sync
    add_app_icon(app_grid, 2); // Audio
    add_app_icon(app_grid, 5); // Sys Info
    add_app_icon(app_grid, 7); // File Explorer
    add_app_icon(app_grid, 6); // OTA Update
    add_app_icon(app_grid, 8); // Options
#if ENABLE_APP_MIC_TEST
    add_app_icon(app_grid, 11); // Mic Test
#endif
#if ENABLE_APP_IMU
    add_app_icon(app_grid, 12); // IMU / Movimiento
#endif
    add_app_icon(app_grid, 4); // Sleep
}

static void app_build_sysinfo(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, TXT_APP_SYSINFO_TITLE);
    lv_label_set_text(content, "");

    sys_info_stats_label = lv_label_create(app_screen);
    lv_obj_set_style_text_color(sys_info_stats_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(sys_info_stats_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(sys_info_stats_label, "Loading stats...");
    lv_obj_align(sys_info_stats_label, LV_ALIGN_CENTER, 0, 0);
}

static void app_build_ota(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, "System Update");
    lv_label_set_text(content, "");

    lv_obj_t * ota_label = lv_label_create(app_screen);
    lv_obj_set_style_text_color(ota_label, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(ota_label, &lv_font_montserrat_16, 0);
    lv_label_set_text(ota_label, "Enabling OTA...");
    lv_obj_align(ota_label, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t * info_label = lv_label_create(app_screen);
    lv_obj_set_style_text_color(info_label, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(info_label, "Arduino IDE -> Ports -> Network Ports");
    lv_obj_align(info_label, LV_ALIGN_CENTER, 0, 20);

    backend_enable_ota(); // Start OTA service in background
    lv_label_set_text_fmt(ota_label, "OTA Ready!\nIP: %s", backend_get_ip().c_str());
}

static void app_build_explorer(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, "Explorador");
    lv_label_set_text(content, "");

    lv_obj_t * fs_label = lv_label_create(app_screen);
    lv_obj_set_width(fs_label, LV_PCT(90));
    lv_label_set_long_mode(fs_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(fs_label, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(fs_label, &lv_font_montserrat_14, 0);

    String files = backend_list_files();
    lv_label_set_text(fs_label, files.c_str());
    lv_obj_align(fs_label, LV_ALIGN_TOP_MID, 0, 80);
}

static void app_build_options(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, "Opciones");
    lv_label_set_text(content, "");

    lv_obj_t * cont = lv_obj_create(app_screen);
    lv_obj_set_size(cont, LV_PCT(90), LV_PCT(70));
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x1e1e1e), 0); // Darker gray instead of white
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_text_color(cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(cont, 10, 0);

    // Telnet Switch
    lv_obj_t * telnet_cont = lv_obj_create(cont);
    lv_obj_set_size(telnet_cont, LV_PCT(100), 60);
    lv_obj_set_flex_flow(telnet_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(telnet_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(telnet_cont, 0, 0);

    lv_obj_t * telnet_label = lv_label_create(telnet_cont);
    lv_label_set_text(telnet_label, "Telnet Logger (Port 23)");

    lv_obj_t * telnet_sw = lv_switch_create(telnet_cont);
    if (backend_get_telnet()) lv_obj_add_state(telnet_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(telnet_sw, [](lv_event_t * e) {
        lv_obj_t * sw = (lv_obj_t *)lv_event_get_target(e);
        backend_set_telnet(lv_obj_has_state(sw, LV_STATE_CHECKED));
    }, LV_EVENT_VALUE_CHANGED, NULL);
    // Animations Switch
    lv_obj_t * anim_cont = lv_obj_create(cont);
    lv_obj_set_size(anim_cont, LV_PCT(100), 60);
    lv_obj_set_flex_flow(anim_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(anim_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(anim_cont, 0, 0);

    lv_obj_t * anim_label = lv_label_create(anim_cont);
    lv_label_set_text(anim_label, "Animaciones UI");

    lv_obj_t * anim_sw = lv_switch_create(anim_cont);
    if (backend_get_animations()) lv_obj_add_state(anim_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(anim_sw, [](lv_event_t * e) {
        lv_obj_t * sw = (lv_obj_t *)lv_event_get_target(e);
        backend_set_animations(lv_obj_has_state(sw, LV_STATE_CHECKED));
    }, LV_EVENT_VALUE_CHANGED, NULL);

#if CARUSOS_USE_BLUETOOTH
    // Bluetooth Switch
    lv_obj_t * bt_cont = lv_obj_create(cont);
    lv_obj_set_size(bt_cont, LV_PCT(100), 60);
    lv_obj_set_flex_flow(bt_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bt_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(bt_cont, 0, 0);

    lv_obj_t * bt_label = lv_label_create(bt_cont);
    lv_label_set_text(bt_label, "Bluetooth (BLE)");

    lv_obj_t * bt_sw = lv_switch_create(bt_cont);
    if (backend_get_bluetooth()) lv_obj_add_state(bt_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(bt_sw, [](lv_event_t * e) {
        lv_obj_t * sw = (lv_obj_t *)lv_event_get_target(e);
        backend_set_bluetooth(lv_obj_has_state(sw, LV_STATE_CHECKED));
    }, LV_EVENT_VALUE_CHANGED, NULL);
#endif

    // WiFi Reconnect Button
    lv_obj_t * wifi_btn = lv_button_create(cont);
    lv_obj_set_size(wifi_btn, LV_PCT(100), 50);
    lv_obj_t * wifi_btn_label = lv_label_create(wifi_btn);
    lv_label_set_text(wifi_btn_label, "Reconectar WiFi");
    lv_obj_center(wifi_btn_label);
    lv_obj_add_event_cb(wifi_btn, [](lv_event_t * e) {
        backend_wifi_reconnect();
    }, LV_EVENT_CLICKED, NULL);
}

static void app_build_gallery(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, "Galeria");
    lv_label_set_text(content, "Iniciando descarga...");

    static struct {
        volatile bool finished;
        void* buffer;
        size_t size;
        lv_obj_t* label;
        lv_obj_t* img;
        lv_obj_t* screen;
    } gal_state;

    gal_state.finished = false;
    gal_state.buffer = NULL;
    gal_state.size = 0;
    gal_state.label = content;
    gal_state.img = lv_image_create(app_screen);
    gal_state.screen = app_screen;
    lv_obj_center(gal_state.img);

    // Create a background task for HTTP so we don't freeze LVGL transitions
    xTaskCreatePinnedToCore([](void* p) {
        size_t s = 0;
        void* b = backend_download_file("https://wineandsorcery.com/hero.png", &s);
        gal_state.buffer = b;
        gal_state.size = s;
        gal_state.finished = true;
        vTaskDelete(NULL);
    }, "GalDL", 8192, NULL, 1, NULL, 0); // Core 0 is backend core

    // Create an LVGL timer to safely update UI once the background task finishes
    lv_timer_create([](lv_timer_t * t) {
        if (gal_state.finished) {
            if (gal_state.buffer && gal_state.size > 0) {
                lv_label_set_text(gal_state.label, ""); // Clear "descargando"

                lv_image_dsc_t * dsc = (lv_image_dsc_t *)malloc(sizeof(lv_image_dsc_t));
                dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
                dsc->header.cf = LV_COLOR_FORMAT_RAW; // Raw image data for LODEPNG
                dsc->header.w = 0;
                dsc->header.h = 0;
                dsc->data_size = gal_state.size;
                dsc->data = (const uint8_t*)gal_state.buffer;

                lv_image_set_src(gal_state.img, dsc);

                // Save descriptor pointer so it can be freed on Back button
                lv_obj_set_user_data(gal_state.screen, dsc);
            } else {
                lv_label_set_text(gal_state.label, "Error: Timeout o red inestable.");
            }
            lv_timer_delete(t); // Stop polling
        }
    }, 100, NULL); // Poll every 100ms
}

#if ENABLE_APP_ANIM
// ---------------------------------------------------------------------------
// Animation app: a blocky "8-bit" sprite that bounces around the screen and
// cycles colour on every wall hit. Pure lv_anim/lv_timer — no assets, no PSRAM,
// no network. The whole thing is compiled out when ENABLE_APP_ANIM is 0.
// ---------------------------------------------------------------------------

#define ANIM_SPRITE_SIZE 64
#define ANIM_AREA_TOP    55   // keep the sprite below the 50px top bar

typedef struct {
    lv_obj_t *   sprite;
    lv_timer_t * timer;
    int x, y;       // top-left position
    int vx, vy;     // velocity (px per tick)
    int color_idx;
} anim_state_t;

static const uint32_t ANIM_COLORS[] = {
    0xFF004D, 0x00B543, 0x1763CF, 0xFFA300, 0x00E436, 0xFF77A8
};
#define ANIM_NUM_COLORS (sizeof(ANIM_COLORS) / sizeof(ANIM_COLORS[0]))

static void anim_tick_cb(lv_timer_t * t) {
    anim_state_t * st = (anim_state_t *)lv_timer_get_user_data(t);
    lv_obj_t * scr = lv_obj_get_screen(st->sprite);
    int w = lv_obj_get_width(scr);
    int h = lv_obj_get_height(scr);

    st->x += st->vx;
    st->y += st->vy;

    bool bounced = false;
    if (st->x <= 0)                    { st->x = 0;                    st->vx = -st->vx; bounced = true; }
    if (st->x >= w - ANIM_SPRITE_SIZE) { st->x = w - ANIM_SPRITE_SIZE; st->vx = -st->vx; bounced = true; }
    if (st->y <= ANIM_AREA_TOP)        { st->y = ANIM_AREA_TOP;        st->vy = -st->vy; bounced = true; }
    if (st->y >= h - ANIM_SPRITE_SIZE) { st->y = h - ANIM_SPRITE_SIZE; st->vy = -st->vy; bounced = true; }

    if (bounced) {
        st->color_idx = (st->color_idx + 1) % ANIM_NUM_COLORS;
        lv_obj_set_style_bg_color(st->sprite, lv_color_hex(ANIM_COLORS[st->color_idx]), 0);
    }

    lv_obj_set_pos(st->sprite, st->x, st->y);
}

// Fires when the app screen is destroyed (Back button). Kills the timer so it
// never ticks on a freed sprite, and frees the state.
static void anim_cleanup_cb(lv_event_t * e) {
    anim_state_t * st = (anim_state_t *)lv_event_get_user_data(e);
    if (st) {
        if (st->timer) lv_timer_delete(st->timer);
        lv_free(st);
    }
}

// Helper: build a small black "pixel" (eye / mouth) as a child of the sprite.
static lv_obj_t * anim_make_pixel(lv_obj_t * parent, int size, lv_align_t align, int x, int y) {
    lv_obj_t * px = lv_obj_create(parent);
    lv_obj_set_size(px, size, size);
    lv_obj_align(px, align, x, y);
    lv_obj_set_style_radius(px, 0, 0);
    lv_obj_set_style_border_width(px, 0, 0);
    lv_obj_set_style_pad_all(px, 0, 0);
    lv_obj_set_style_bg_color(px, lv_color_hex(0x000000), 0);
    lv_obj_set_scrollbar_mode(px, LV_SCROLLBAR_MODE_OFF);
    return px;
}

static void app_build_anim(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, "Animacion");
    lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN); // hide the default content label

    anim_state_t * st = (anim_state_t *)lv_malloc(sizeof(anim_state_t));
    if (!st) return;
    st->x = 30;
    st->y = ANIM_AREA_TOP + 30;
    st->vx = 4;
    st->vy = 3;
    st->color_idx = 0;

    // The sprite: a chunky square with a hard black border = blocky 8-bit look.
    st->sprite = lv_obj_create(app_screen);
    lv_obj_set_size(st->sprite, ANIM_SPRITE_SIZE, ANIM_SPRITE_SIZE);
    lv_obj_set_style_radius(st->sprite, 0, 0);
    lv_obj_set_style_border_width(st->sprite, 4, 0);
    lv_obj_set_style_border_color(st->sprite, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_color(st->sprite, lv_color_hex(ANIM_COLORS[0]), 0);
    lv_obj_set_style_pad_all(st->sprite, 0, 0);
    lv_obj_set_scrollbar_mode(st->sprite, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_pos(st->sprite, st->x, st->y);

    // A simple face so it reads as a character.
    anim_make_pixel(st->sprite, 10, LV_ALIGN_TOP_LEFT,  12, 14); // left eye
    anim_make_pixel(st->sprite, 10, LV_ALIGN_TOP_RIGHT, -12, 14); // right eye
    anim_make_pixel(st->sprite, 28, LV_ALIGN_BOTTOM_MID, 0, -10); // mouth

    // ~30ms tick = ~33 FPS, smooth without hammering the CPU.
    st->timer = lv_timer_create(anim_tick_cb, 30, st);

    // Tie cleanup to the screen's lifetime (survives any exit path).
    lv_obj_add_event_cb(app_screen, anim_cleanup_cb, LV_EVENT_DELETE, st);
}
#endif // ENABLE_APP_ANIM

#if ENABLE_APP_MIC_TEST
static void mic_switch_event_cb(lv_event_t * e) {
    lv_obj_t * sw = (lv_obj_t *)lv_event_get_target(e);
    if (lv_obj_has_state(sw, LV_STATE_CHECKED)) backend_mic_start();
    else                                        backend_mic_stop();
}

static void mic_bar_update_cb(lv_timer_t * t) {
    if (mic_level_bar != NULL) {
        lv_bar_set_value(mic_level_bar, backend_get_mic_level(), LV_ANIM_OFF);
    }
}

static void app_build_mic(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, "Mic Test");
    lv_label_set_text(content, "Activa el mic y hace ruido:\nla barra deberia moverse.");
    lv_obj_set_style_text_align(content, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 70);

    // Mic ON/OFF switch
    lv_obj_t * sw = lv_switch_create(app_screen);
    lv_obj_align(sw, LV_ALIGN_CENTER, 0, -30);
    lv_obj_add_event_cb(sw, mic_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Live input level meter
    mic_level_bar = lv_bar_create(app_screen);
    lv_obj_set_size(mic_level_bar, 320, 30);
    lv_obj_align(mic_level_bar, LV_ALIGN_CENTER, 0, 40);
    lv_bar_set_range(mic_level_bar, 0, 100);
    lv_bar_set_value(mic_level_bar, 0, LV_ANIM_OFF);

    // Refresh the meter ~10x/s while the app is open
    mic_timer = lv_timer_create(mic_bar_update_cb, 100, NULL);
}
#endif // ENABLE_APP_MIC_TEST

#if ENABLE_APP_IMU
#define IMU_BOX_SIZE   300
#define IMU_BALL_SIZE  44

static void imu_tick_cb(lv_timer_t * t) {
    if (imu_ball == NULL) return;
    float ax, ay, az;
    if (!imu_get_accel(ax, ay, az)) return;

    // Tilt the board -> gravity shifts -> ball rolls "downhill".
    // ax/ay are in g (~ -1..1 when tilted). Map to pixels within the box.
    int range = (IMU_BOX_SIZE - IMU_BALL_SIZE) / 2 - 4;
    int dx = (int)(ax * range);
    int dy = (int)(-ay * range); // screen Y grows downward; invert so it feels natural
    if (dx >  range) dx =  range;
    if (dx < -range) dx = -range;
    if (dy >  range) dy =  range;
    if (dy < -range) dy = -range;
    lv_obj_align(imu_ball, LV_ALIGN_CENTER, dx, dy);
}

static void app_build_imu(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, "Movimiento (IMU)");
    lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN); // hide the default content label

    if (!imu_begin()) {
        lv_obj_clear_flag(content, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(content, "IMU no detectado (QMI8658).");
        lv_obj_center(content);
        return;
    }

    // Bounding box
    imu_box = lv_obj_create(app_screen);
    lv_obj_set_size(imu_box, IMU_BOX_SIZE, IMU_BOX_SIZE);
    lv_obj_align(imu_box, LV_ALIGN_CENTER, 0, 25);
    lv_obj_set_style_bg_color(imu_box, lv_color_hex(0x101010), 0);
    lv_obj_set_style_border_color(imu_box, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_border_width(imu_box, 2, 0);
    lv_obj_set_style_radius(imu_box, 8, 0);
    lv_obj_clear_flag(imu_box, LV_OBJ_FLAG_SCROLLABLE);

    // Ball (moves with tilt)
    imu_ball = lv_obj_create(imu_box);
    lv_obj_set_size(imu_ball, IMU_BALL_SIZE, IMU_BALL_SIZE);
    lv_obj_set_style_radius(imu_ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(imu_ball, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_border_width(imu_ball, 0, 0);
    lv_obj_align(imu_ball, LV_ALIGN_CENTER, 0, 0);

    // ~33 FPS read + move
    imu_timer = lv_timer_create(imu_tick_cb, 30, NULL);
}
#endif // ENABLE_APP_IMU

// ---------------------------------------------------------------------------
// Window chrome + launcher
// ---------------------------------------------------------------------------

static void create_app_window(carusos_app_t * app, lv_obj_t * return_screen) {
    lv_obj_t * app_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(app_screen, lv_color_hex(0x1e1e1e), 0);

    // App Status Bar
    lv_obj_t * top_bar = lv_obj_create(app_screen);
    lv_obj_set_size(top_bar, LV_PCT(100), 50);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x101010), 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_radius(top_bar, 0, 0);

    // Back Button
    lv_obj_t * back_btn = lv_button_create(top_bar);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_size(back_btn, 80, 40);
    lv_obj_add_event_cb(back_btn, app_back_event_cb, LV_EVENT_CLICKED, (void*)return_screen);

    lv_obj_t * back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT TXT_BACK);
    lv_obj_center(back_label);

    // App Title
    lv_obj_t * title_label = lv_label_create(top_bar);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, 0);
    lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 0);

    app_wifi_label = lv_label_create(top_bar);
    lv_label_set_text(app_wifi_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(app_wifi_label, lv_color_hex(0x555555), 0);
    lv_obj_align(app_wifi_label, LV_ALIGN_RIGHT_MID, -10, 0);

    app_time_label = lv_label_create(top_bar);
    lv_label_set_text(app_time_label, "");
    lv_obj_set_style_text_color(app_time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(app_time_label, LV_ALIGN_RIGHT_MID, -40, 0);

    // Content Area
    lv_obj_t * content = lv_label_create(app_screen);
    lv_obj_set_style_text_color(content, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 70);

    // Build the app-specific UI
    if (app && app->build) {
        app->build(app_screen, content, title_label);
    }

    // Transition
    lv_scr_load_anim_t anim = backend_get_animations() ? LV_SCR_LOAD_ANIM_FADE_ON : LV_SCR_LOAD_ANIM_NONE;
    lv_scr_load_anim(app_screen, anim, 200, 0, false);
}

// App Launch Handler
static void app_launcher_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        int app_id = (int)(intptr_t)lv_event_get_user_data(e);
        carusos_app_t * app = app_by_id(app_id);

        if (!app || !app->enabled) return; // Disabled apps do nothing

        if (app->build == NULL) {
            // Action app (no window)
            if (app->id == 4) {
                lvgl_port_set_backlight(false); // Instant Sleep
            }
            return;
        }

        // Open window and pass current screen as return screen
        create_app_window(app, lv_scr_act());
    }
}

// Helper to create App Icons from the registry
static void add_app_icon(lv_obj_t * parent, int app_id) {
    carusos_app_t * app = app_by_id(app_id);
    if (!app) return;

    // Hide disabled apps entirely unless configured to show them greyed-out.
    if (!app->enabled && !CARUSOS_SHOW_DISABLED_APPS) return;

    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_set_size(btn, 110, 110);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a2a2a), 0);
    lv_obj_add_event_cb(btn, app_launcher_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)app_id);

    // Use Flex layout for the button contents
    lv_obj_set_layout(btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * icon_label = lv_label_create(btn);
    lv_label_set_text(icon_label, app->icon);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_24, 0); // Large icon

    lv_obj_t * text_label = lv_label_create(btn);
    lv_label_set_text(text_label, app->name);
    lv_obj_set_style_text_font(text_label, &lv_font_montserrat_14, 0); // Smaller text

    // Grey out disabled apps
    if (!app->enabled) {
        lv_obj_set_style_text_color(icon_label, lv_color_hex(0x555555), 0);
        lv_obj_set_style_text_color(text_label, lv_color_hex(0x555555), 0);
    }
}

static void ui_poll_timer_cb(lv_timer_t * timer) {
    // WiFi icon color
    lv_color_t wifi_col = backend_is_wifi_connected() ? lv_color_hex(0x00FF00) : lv_color_hex(0x555555);
    lv_obj_set_style_text_color(wifi_label, wifi_col, 0);
    if (app_wifi_label) lv_obj_set_style_text_color(app_wifi_label, wifi_col, 0);

    // Clock: the system time is valid if NTP synced OR the RTC seeded it at boot,
    // so this runs regardless of WiFi.
    struct tm dt;
    if (backend_get_datetime(dt)) {
        lv_label_set_text_fmt(time_label, "%02d:%02d", dt.tm_hour, dt.tm_min);
        if (app_time_label) lv_label_set_text_fmt(app_time_label, "%02d:%02d", dt.tm_hour, dt.tm_min);
        if (ntp_live_label)
            lv_label_set_text_fmt(ntp_live_label, "%02d:%02d:%02d", dt.tm_hour, dt.tm_min, dt.tm_sec);
        if (datetime_date_label)
            lv_label_set_text_fmt(datetime_date_label, "%02d/%02d/%04d", dt.tm_mday, dt.tm_mon + 1, dt.tm_year + 1900);
    }

    // Update Sys Info if open
    if (sys_info_stats_label != NULL) {
        uint32_t f_heap, t_heap, f_psram, t_psram, s_size, s_free, f_size;
        backend_get_sys_info(f_heap, t_heap, f_psram, t_psram, s_size, s_free, f_size);

        String warnings = "";
        if (ESP.getCpuFreqMHz() < 240) warnings += "WARNING: CPU is slow (<240MHz)!\n";
        if (ESP.getPsramSize() == 0) warnings += "WARNING: PSRAM is disabled!\n";
        if (ESP.getFlashChipMode() > 1) warnings += "WARNING: Flash mode is NOT QIO!\n";
        if (warnings.length() > 0) warnings = "\n-- ALERTS --\n" + warnings;

        lv_label_set_text_fmt(sys_info_stats_label,
            "WiFi: %s (%s)\n"
            "Audio Volume: %d%%\n\n"
            "RAM (Heap): %d (Libre) / %d KB (Total)\n"
            "PSRAM: %d (Libre) / %d KB (Total)\n\n"
            "Storage (APP): %d (Libre) / %d KB (Total)\n"
            "Storage (Total Flash): %d MB%s\n\n"
            "Created by: " CARUSOS_AUTHOR "\n"
            "Source code: " CARUSOS_SOURCE_DATE "\n"
            "Compilation date: " __DATE__ " " __TIME__,
            backend_is_wifi_connected() ? "Connected" : "Disconnected",
            backend_get_ip().c_str(),
            backend_get_audio_volume(),
            (int)(f_heap / 1024), (int)(t_heap / 1024),
            (int)(f_psram / 1024), (int)(t_psram / 1024),
            (int)(s_size / 1024), (int)((s_size + s_free) / 1024),
            (int)(f_size / (1024 * 1024)),
            warnings.c_str()
        );
    }
}

static void create_main_screen() {
    main_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x1e1e1e), 0); // Dark background

    // Top Status Bar
    lv_obj_t * status_bar = lv_obj_create(main_screen);
    lv_obj_set_size(status_bar, LV_PCT(100), 40);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x101010), 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);

    // Time Label
    time_label = lv_label_create(status_bar);
    lv_label_set_text(time_label, "12:00");
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, 0);

    // Icons
    wifi_label = lv_label_create(status_bar);
    lv_label_set_text(wifi_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifi_label, lv_color_hex(0x555555), 0); // Offline dark color
    lv_obj_align(wifi_label, LV_ALIGN_RIGHT_MID, -10, 0);

    // App Launcher Grid Container
    lv_obj_t * app_grid = lv_obj_create(main_screen);
    lv_obj_set_size(app_grid, LV_PCT(90), LV_PCT(80));
    lv_obj_align(app_grid, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_opa(app_grid, 0, 0); // Transparent
    lv_obj_set_style_border_width(app_grid, 0, 0);

    // Flex Layout for the grid
    lv_obj_set_layout(app_grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(app_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(app_grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(app_grid, 20, 0);
    lv_obj_set_style_pad_column(app_grid, 20, 0);

    // Populate Desktop Apps
    add_app_icon(app_grid, 0); // Calculator
    add_app_icon(app_grid, 9); // Web Gallery
#if ENABLE_APP_ANIM
    add_app_icon(app_grid, 10); // Animation
#endif
    add_app_icon(app_grid, 3); // Settings
}

static void boot_timer_cb(lv_timer_t * timer) {
    switch(boot_step) {
        case 0:
            lv_label_set_text_fmt(boot_label, TXT_BOOT_BOOTING, CARUSOS_VERSION);
            break;
        case 1:
            lv_label_set_text_fmt(boot_label, "%s" TXT_BOOT_PSRAM, lv_label_get_text(boot_label));
            break;
        case 2:
            lv_label_set_text_fmt(boot_label, "%s" TXT_BOOT_PANEL, lv_label_get_text(boot_label));
            break;
        case 3:
            lv_label_set_text_fmt(boot_label, "%s" TXT_BOOT_START, lv_label_get_text(boot_label));
            break;
        case 4:
            // Load the main screen with a fade animation
            lv_scr_load_anim(main_screen, LV_SCR_LOAD_ANIM_FADE_ON, 800, 200, false);
            lv_timer_del(timer); // Stop the timer
            break;
    }
    boot_step++;
}

void ui_core_init() {
    // 1. Create the boot screen
    boot_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(boot_screen, lv_color_black(), 0);

    // Boot logs label
    boot_label = lv_label_create(boot_screen);
    lv_label_set_text(boot_label, "");
    lv_obj_set_style_text_color(boot_label, lv_color_hex(0x00FF00), 0); // Hacker green text
    lv_obj_align(boot_label, LV_ALIGN_TOP_LEFT, 10, 10);

    // Load boot screen immediately
    lv_scr_load(boot_screen);

    // 2. Prepare the Main Screen in the background
    create_main_screen();

    // 3. Start the boot animation timer (updates every 500ms)
    boot_timer = lv_timer_create(boot_timer_cb, 500, NULL);

    // 4. Start the UI polling timer (updates every 1s)
    ui_poll_timer = lv_timer_create(ui_poll_timer_cb, 1000, NULL);
}
