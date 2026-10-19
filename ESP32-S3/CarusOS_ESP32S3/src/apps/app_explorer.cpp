// File Explorer. The FFat work runs on Core 0 (see backend_core.cpp); here we
// only request it and poll for the text result, so the UI core never blocks on
// the filesystem.
#include "apps.h"

#if CARUSOS_APP_EXPLORER
static void fs_poll_cb(lv_timer_t * t) {
    lv_obj_t * list = (lv_obj_t *)lv_timer_get_user_data(t);
    char buf[512];
    if (backend_fs_poll(buf, sizeof(buf))) {
        lv_label_set_text(list, buf);
    }
}

static void fs_format_event_cb(lv_event_t * e) {
    lv_obj_t * list = (lv_obj_t *)lv_event_get_user_data(e);
    lv_label_set_text(list, TXT_FS_FORMATTING);
    backend_fs_request_format(); // Core 0 does the work; result arrives via fs_poll_cb
}

void app_build_explorer(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, TXT_APP_FILES_NAME);
    lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * list = lv_label_create(app_screen);
    lv_obj_set_width(list, LV_PCT(90));
    lv_label_set_long_mode(list, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(list, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_14, 0);
    lv_label_set_text(list, TXT_FS_LOADING);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 70);

    backend_fs_request_list();                         // ask Core 0 for the listing
    ui_screen_timer(app_screen, fs_poll_cb, 150, list); // poll for the result

    // On-demand format (deliberate; never runs at boot)
    lv_obj_t * fmt_btn = lv_button_create(app_screen);
    lv_obj_set_size(fmt_btn, 170, 44);
    lv_obj_align(fmt_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(fmt_btn, fs_format_event_cb, LV_EVENT_CLICKED, list);
    lv_obj_t * fmt_lbl = lv_label_create(fmt_btn);
    lv_label_set_text(fmt_lbl, LV_SYMBOL_SD_CARD " " TXT_FS_FORMAT_BTN);
    lv_obj_center(fmt_lbl);
}
#endif // CARUSOS_APP_EXPLORER
