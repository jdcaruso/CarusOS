#include "carusos_config.h"
#include "backend_core.h"
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

static carusos_app_t g_apps[] = {
    { 0, LV_SYMBOL_PLUS,      TXT_APP_CALC_TITLE, true,                 app_build_calc },
    { 1, LV_SYMBOL_BELL,      "NTP Sync",         true,                 app_build_ntp },
    { 2, LV_SYMBOL_AUDIO,     "Audio",            CARUSOS_USE_AUDIO,    app_build_audio },
    { 3, LV_SYMBOL_SETTINGS,  TXT_APP_INFO_TITLE, true,                 app_build_settings },
    { 4, LV_SYMBOL_POWER,     TXT_SCREEN_SLEEP,   true,                 NULL /* action: Sleep */ },
    { 5, LV_SYMBOL_FILE,      "Sys Info",         true,                 app_build_sysinfo },
    { 6, LV_SYMBOL_DOWNLOAD,  "Update",           CARUSOS_USE_OTA,      app_build_ota },
    { 7, LV_SYMBOL_DIRECTORY, "Archivos",         CARUSOS_APP_EXPLORER, app_build_explorer },
    { 8, LV_SYMBOL_SETTINGS,  "Opciones",         true,                 app_build_options },
    { 9, LV_SYMBOL_IMAGE,     "Galeria",          CARUSOS_APP_GALLERY,  app_build_gallery },
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
        sys_info_stats_label = NULL; // Clear pointer when leaving Sys Info app
        app_wifi_label = NULL;
        app_time_label = NULL;
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
    lv_label_set_text(content, TXT_APP_NTP_DESC);

    ntp_live_label = lv_label_create(app_screen);
    lv_label_set_text(ntp_live_label, "--:--");
    lv_obj_set_style_text_font(ntp_live_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(ntp_live_label, lv_color_hex(0x00FF00), 0);
    lv_obj_center(ntp_live_label);
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
    // Update WiFi Icon
    if (backend_is_wifi_connected()) {
        lv_obj_set_style_text_color(wifi_label, lv_color_hex(0x00FF00), 0); // Green
        if (app_wifi_label) lv_obj_set_style_text_color(app_wifi_label, lv_color_hex(0x00FF00), 0);

        // Update Time if NTP synced
        int h, m;
        if (backend_get_time(h, m)) {
            lv_label_set_text_fmt(time_label, "%02d:%02d", h, m);
            if (app_time_label) lv_label_set_text_fmt(app_time_label, "%02d:%02d", h, m);
            if (ntp_live_label != NULL) {
                lv_label_set_text_fmt(ntp_live_label, "%02d:%02d", h, m);
            }
        }
    } else {
        lv_obj_set_style_text_color(wifi_label, lv_color_hex(0x555555), 0); // Gray
        if (app_wifi_label) lv_obj_set_style_text_color(app_wifi_label, lv_color_hex(0x555555), 0);
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
