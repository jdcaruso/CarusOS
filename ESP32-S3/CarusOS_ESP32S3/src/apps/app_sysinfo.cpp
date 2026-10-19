// Sys Info: live memory stats ("System" tab) + chip list ("Hardware" tab).
#include "apps.h"

static void sysinfo_tick_cb(lv_timer_t * t) {
    lv_obj_t * stats = (lv_obj_t *)lv_timer_get_user_data(t);

    uint32_t f_heap, t_heap, f_psram, t_psram, s_size, s_free, f_size;
    backend_get_sys_info(f_heap, t_heap, f_psram, t_psram, s_size, s_free, f_size);

    String warnings = "";
    if (ESP.getCpuFreqMHz() < 240)  warnings += TXT_SI_WARN_CPU "\n";
    if (ESP.getPsramSize() == 0)    warnings += TXT_SI_WARN_PSRAM "\n";
    if (ESP.getFlashChipMode() > 1) warnings += TXT_SI_WARN_QIO "\n";
    if (warnings.length() > 0) warnings = "\n" TXT_SI_ALERTS "\n" + warnings;

    bool wifi = backend_is_wifi_connected();
    lv_label_set_text_fmt(stats,
        "CarusOS " CARUSOS_VERSION "\n\n"
        "WiFi: %s (%s)\n"
        TXT_SI_AUDIO_VOL ": %d%%\n\n"
        TXT_SI_MEMORY "\n"
        "RAM (Heap): %d / %d KB\n"
        "PSRAM: %d / %d KB\n"
        TXT_SI_STORAGE ": %d / %d KB\n"
        TXT_SI_FLASH_TOTAL ": %d MB%s\n\n"
        TXT_SI_CREATED_BY ": " CARUSOS_AUTHOR "\n"
        TXT_SI_SOURCE_DATE ": " CARUSOS_SOURCE_DATE "\n"
        TXT_SI_BUILD_DATE ": " __DATE__ " " __TIME__,
        wifi ? TXT_CONNECTED : TXT_DISCONNECTED,
        wifi ? backend_get_ip().c_str() : "-",
        backend_get_audio_volume(),
        (int)(f_heap / 1024), (int)(t_heap / 1024),
        (int)(f_psram / 1024), (int)(t_psram / 1024),
        (int)(s_size / 1024), (int)((s_size + s_free) / 1024),
        (int)(f_size / (1024 * 1024)),
        warnings.c_str()
    );
}

void app_build_sysinfo(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, TXT_APP_SYSINFO_TITLE);
    lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN);

    // Tabview filling the area below the 50px top bar.
    lv_obj_t * tv = lv_tabview_create(app_screen);
    lv_tabview_set_tab_bar_size(tv, 40);
    lv_obj_set_size(tv, LV_PCT(100), 430); // 480 screen - 50 top bar
    lv_obj_align(tv, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(tv, lv_color_hex(0x1e1e1e), 0);

    lv_obj_t * tab_sys = lv_tabview_add_tab(tv, TXT_SYSINFO_TAB_SYS);
    lv_obj_t * tab_hw  = lv_tabview_add_tab(tv, TXT_SYSINFO_TAB_HW);

    // --- "System" tab: live stats, refreshed every second ---
    lv_obj_t * stats = lv_label_create(tab_sys);
    lv_obj_set_width(stats, LV_PCT(100));
    lv_label_set_long_mode(stats, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(stats, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(stats, &lv_font_montserrat_14, 0);
    lv_label_set_text(stats, TXT_SYSINFO_LOADING);
    lv_timer_t * t = ui_screen_timer(app_screen, sysinfo_tick_cb, 1000, stats);
    lv_timer_ready(t);

    // --- "Hardware" tab: chips (MCU read live, peripherals gated by flag) ---
    lv_obj_t * hw = lv_label_create(tab_hw);
    lv_obj_set_width(hw, LV_PCT(100));
    lv_label_set_long_mode(hw, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(hw, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(hw, &lv_font_montserrat_14, 0);
    lv_label_set_text_fmt(hw,
        "MCU: %s rev %d\n"
        "Cores: %d @ %d MHz\n"
        "Flash: %d MB  |  PSRAM: %d MB\n\n"
        TXT_SI_PERIPHERALS "\n"
        TXT_SI_DISPLAY ": ST7701 (RGB)\n"
        "Touch: GT911 (I2C)\n"
        TXT_SI_IO_EXPANDER ": XCA9554\n"
#if CARUSOS_USE_AUDIO
        "Audio: ES8311\n"
#endif
#if ENABLE_APP_MIC_TEST
        "Mic ADC: ES7210\n"
#endif
#if ENABLE_APP_IMU
        "IMU: QMI8658\n"
#endif
#if CARUSOS_USE_RTC
        "RTC: PCF85063\n"
#endif
        "PMU: AXP2101",
        ESP.getChipModel(), ESP.getChipRevision(),
        ESP.getChipCores(), ESP.getCpuFreqMHz(),
        (int)(ESP.getFlashChipSize() / (1024 * 1024)),
        (int)(ESP.getPsramSize() / (1024 * 1024)));
}
