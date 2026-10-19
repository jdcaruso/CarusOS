// Small always-on apps: Calculator, Date & Time, Settings (submenu), OTA, Audio.
#include "apps.h"

void app_build_calc(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, TXT_APP_CALC_TITLE);
    lv_label_set_text(content, "1 + 1 = 2");
    lv_obj_center(content);
}

// ---------------------------------------------------------------------------
// Date & Time
// ---------------------------------------------------------------------------
typedef struct {
    lv_obj_t * time;
    lv_obj_t * date;
} datetime_ui_t;

static void datetime_tick_cb(lv_timer_t * t) {
    datetime_ui_t * ui = (datetime_ui_t *)lv_timer_get_user_data(t);
    struct tm dt;
    if (!backend_get_datetime(dt)) return;
    lv_label_set_text_fmt(ui->time, "%02d:%02d:%02d", dt.tm_hour, dt.tm_min, dt.tm_sec);
    lv_label_set_text_fmt(ui->date, "%02d/%02d/%04d", dt.tm_mday, dt.tm_mon + 1, dt.tm_year + 1900);
}

void app_build_datetime(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, TXT_APP_NTP_TITLE);
    lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN); // hide default content label

    datetime_ui_t * ui = (datetime_ui_t *)ui_screen_alloc(app_screen, sizeof(datetime_ui_t));
    if (!ui) return;

    // Big live time (HH:MM:SS)
    ui->time = lv_label_create(app_screen);
    lv_label_set_text(ui->time, "--:--:--");
    lv_obj_set_style_text_font(ui->time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(ui->time, lv_color_hex(0x00FF00), 0);
    lv_obj_align(ui->time, LV_ALIGN_CENTER, 0, -20);

    // Date line (DD/MM/YYYY)
    ui->date = lv_label_create(app_screen);
    lv_label_set_text(ui->date, "--/--/----");
    lv_obj_set_style_text_font(ui->date, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ui->date, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(ui->date, LV_ALIGN_CENTER, 0, 45);

    lv_timer_t * t = ui_screen_timer(app_screen, datetime_tick_cb, 500, ui);
    lv_timer_ready(t); // first refresh right away, not after 500 ms
}

// ---------------------------------------------------------------------------
// Settings: a submenu grid, same look as the desktop
// ---------------------------------------------------------------------------
void app_build_settings(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, TXT_APP_INFO_TITLE);
    lv_label_set_text(content, ""); // Clear default top-aligned text

    lv_obj_t * ver_label = lv_label_create(app_screen);
    lv_label_set_text_fmt(ver_label, "CarusOS %s", CARUSOS_VERSION);
    lv_obj_set_style_text_color(ver_label, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_font(ver_label, &lv_font_montserrat_14, 0);
    lv_obj_align(ver_label, LV_ALIGN_BOTTOM_MID, 0, -10);

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

    // Disabled apps are skipped (or greyed out) by ui_add_app_icon itself.
    ui_add_app_icon(app_grid, APP_DATETIME);
    ui_add_app_icon(app_grid, APP_AUDIO);
    ui_add_app_icon(app_grid, APP_SYSINFO);
    ui_add_app_icon(app_grid, APP_EXPLORER);
    ui_add_app_icon(app_grid, APP_OTA);
    ui_add_app_icon(app_grid, APP_OPTIONS);
    ui_add_app_icon(app_grid, APP_MIC);
    ui_add_app_icon(app_grid, APP_IMU);
    ui_add_app_icon(app_grid, APP_SLEEP);
    ui_add_app_icon(app_grid, APP_DEEPSLEEP);
}

// ---------------------------------------------------------------------------
// OTA (ArduinoOTA, explicit opt-in per session)
// ---------------------------------------------------------------------------
#if CARUSOS_USE_OTA
void app_build_ota(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, TXT_APP_OTA_TITLE);
    lv_label_set_text(content, "");

    lv_obj_t * ota_label = lv_label_create(app_screen);
    lv_obj_set_style_text_color(ota_label, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(ota_label, &lv_font_montserrat_16, 0);
    lv_label_set_text(ota_label, TXT_OTA_ENABLING);
    lv_obj_align(ota_label, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t * info_label = lv_label_create(app_screen);
    lv_obj_set_style_text_color(info_label, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(info_label, TXT_OTA_HINT);
    lv_obj_align(info_label, LV_ALIGN_CENTER, 0, 20);

    backend_enable_ota(); // Start OTA service in background
    lv_label_set_text_fmt(ota_label, TXT_OTA_READY, backend_get_ip().c_str());
}
#endif // CARUSOS_USE_OTA

// ---------------------------------------------------------------------------
// Audio (ES8311 test tone)
// ---------------------------------------------------------------------------
#if CARUSOS_USE_AUDIO
static void audio_play_event_cb(lv_event_t * e) {
    lv_obj_t * btn = (lv_obj_t *)lv_event_get_target(e);
    bool playing = lv_obj_has_state(btn, LV_STATE_CHECKED);
    lv_obj_t * label = lv_obj_get_child(btn, 0);
    lv_label_set_text(label, playing ? LV_SYMBOL_STOP " " TXT_AUDIO_STOP : LV_SYMBOL_PLAY " " TXT_AUDIO_PLAY);
    backend_set_audio_state(playing);
}

static void audio_vol_event_cb(lv_event_t * e) {
    lv_obj_t * slider = (lv_obj_t *)lv_event_get_target(e);
    backend_set_audio_volume(lv_slider_get_value(slider));
}

void app_build_audio(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
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
#endif // CARUSOS_USE_AUDIO
