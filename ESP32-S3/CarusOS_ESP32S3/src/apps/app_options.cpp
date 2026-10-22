// Options: runtime toggles persisted in NVS (brightness, WiFi, Telnet, animations, Dashboard beep, BLE).
#include "apps.h"
#include "../../lvgl_port.h"

// One "label ............ [switch]" row.
static lv_obj_t * options_add_switch(lv_obj_t * parent, const char * text, bool on, lv_event_cb_t cb) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 60);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(row, 0, 0);

    lv_obj_t * label = lv_label_create(row);
    lv_label_set_text(label, text);

    lv_obj_t * sw = lv_switch_create(row);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
}

// "Brightness [=====o----]" row. Dragging applies the level live; NVS is
// written once, on release, so a drag doesn't hammer the flash.
static void options_add_brightness(lv_obj_t * parent) {
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 60);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(row, 0, 0);

    lv_obj_t * label = lv_label_create(row);
    lv_label_set_text(label, TXT_OPT_BRIGHTNESS);

    lv_obj_t * slider = lv_slider_create(row);
    lv_obj_set_width(slider, LV_PCT(55));
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, backend_get_brightness(), LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, [](lv_event_t * e) {
        lv_obj_t * s = (lv_obj_t *)lv_event_get_target(e);
        lvgl_port_set_brightness((uint8_t)lv_slider_get_value(s));
    }, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, [](lv_event_t * e) {
        lv_obj_t * s = (lv_obj_t *)lv_event_get_target(e);
        backend_set_brightness(lv_slider_get_value(s));
    }, LV_EVENT_RELEASED, NULL);
}

static bool switch_is_on(lv_event_t * e) {
    return lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED);
}

void app_build_options(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, TXT_APP_OPTIONS_NAME);
    lv_label_set_text(content, "");

    lv_obj_t * cont = lv_obj_create(app_screen);
    lv_obj_set_size(cont, LV_PCT(90), LV_PCT(70));
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x1e1e1e), 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_text_color(cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(cont, 10, 0);

    options_add_brightness(cont);
    options_add_switch(cont, "WiFi", backend_get_wifi_enabled(),
        [](lv_event_t * e) { backend_set_wifi_enabled(switch_is_on(e)); });
#if CARUSOS_USE_TELNET_LOG
    options_add_switch(cont, TXT_OPT_TELNET, backend_get_telnet(),
        [](lv_event_t * e) { backend_set_telnet(switch_is_on(e)); });
#endif
    options_add_switch(cont, TXT_OPT_ANIM, backend_get_animations(),
        [](lv_event_t * e) { backend_set_animations(switch_is_on(e)); });
#if CARUSOS_DASHBOARD_ALERT
    options_add_switch(cont, TXT_OPT_DASH_ALERT, backend_get_dash_alert(),
        [](lv_event_t * e) { backend_set_dash_alert(switch_is_on(e)); });
#endif
#if CARUSOS_USE_BLUETOOTH
    options_add_switch(cont, TXT_OPT_BT, backend_get_bluetooth(),
        [](lv_event_t * e) { backend_set_bluetooth(switch_is_on(e)); });
#endif

    // WiFi Reconnect Button
    lv_obj_t * wifi_btn = lv_button_create(cont);
    lv_obj_set_size(wifi_btn, LV_PCT(100), 50);
    lv_obj_t * wifi_btn_label = lv_label_create(wifi_btn);
    lv_label_set_text(wifi_btn_label, TXT_OPT_WIFI_RECONNECT);
    lv_obj_center(wifi_btn_label);
    lv_obj_add_event_cb(wifi_btn, [](lv_event_t * e) {
        backend_wifi_reconnect();
    }, LV_EVENT_CLICKED, NULL);
}
