// CarusOS shell (Core 1): boot screen, desktop, app registry, window chrome
// and the launcher. The apps themselves live in src/apps/.
#include "src/apps/apps.h"
#include "lvgl_port.h"
#include "ui_core.h"

static lv_obj_t * boot_screen;
static lv_obj_t * main_screen;
static lv_obj_t * boot_label;
static int boot_step = 0;

// ---------------------------------------------------------------------------
// App Registry
// ---------------------------------------------------------------------------
// To add an app: write its builder in src/apps/, declare it in apps.h, add one
// row here and an ui_add_app_icon() call in a menu.
//
// - enabled == false  -> hidden from menus (or greyed out and non-clickable
//                        with CARUSOS_SHOW_DISABLED_APPS 1).
// - build   == NULL   -> "action" app (no window), handled in the launcher
//                        (e.g. Sleep). Apps compiled out also end up NULL, but
//                        they are disabled, so the launcher never reaches them.

typedef struct {
    int            id;
    const char *   icon;     // LV_SYMBOL_* glyph
    const char *   name;     // label shown under the icon
    bool           enabled;
    app_build_fn   build;    // NULL = special action (handled in launcher)
} carusos_app_t;

// Builders of optional apps only exist when their flag is on.
#if CARUSOS_USE_AUDIO
#define BUILD_AUDIO app_build_audio
#else
#define BUILD_AUDIO NULL
#endif
#if CARUSOS_USE_OTA
#define BUILD_OTA app_build_ota
#else
#define BUILD_OTA NULL
#endif
#if CARUSOS_APP_EXPLORER
#define BUILD_EXPLORER app_build_explorer
#else
#define BUILD_EXPLORER NULL
#endif
#if CARUSOS_APP_GALLERY
#define BUILD_GALLERY app_build_gallery
#else
#define BUILD_GALLERY NULL
#endif
#if ENABLE_APP_ANIM
#define BUILD_ANIM app_build_anim
#else
#define BUILD_ANIM NULL
#endif
#if ENABLE_APP_MIC_TEST
#define BUILD_MIC app_build_mic
#else
#define BUILD_MIC NULL
#endif
#if ENABLE_APP_IMU
#define BUILD_IMU app_build_imu
#else
#define BUILD_IMU NULL
#endif
#if CARUSOS_APP_DASHBOARD
#define BUILD_DASHBOARD app_build_dashboard
#else
#define BUILD_DASHBOARD NULL
#endif

static const carusos_app_t g_apps[] = {
    { APP_CALC,     LV_SYMBOL_PLUS,      TXT_APP_CALC_TITLE,   true,                 app_build_calc },
    { APP_DATETIME, LV_SYMBOL_BELL,      TXT_APP_NTP_TITLE,    true,                 app_build_datetime },
    { APP_AUDIO,    LV_SYMBOL_AUDIO,     TXT_APP_AUDIO_NAME,   CARUSOS_USE_AUDIO,    BUILD_AUDIO },
    { APP_SETTINGS, LV_SYMBOL_SETTINGS,  TXT_APP_INFO_TITLE,   true,                 app_build_settings },
    { APP_SLEEP,    LV_SYMBOL_POWER,     TXT_SCREEN_SLEEP,     true,                 NULL /* action: Sleep */ },
    { APP_SYSINFO,  LV_SYMBOL_FILE,      TXT_APP_SYSINFO_NAME, true,                 app_build_sysinfo },
    { APP_OTA,      LV_SYMBOL_DOWNLOAD,  TXT_APP_UPDATE_NAME,  CARUSOS_USE_OTA,      BUILD_OTA },
    { APP_EXPLORER, LV_SYMBOL_DIRECTORY, TXT_APP_FILES_NAME,   CARUSOS_APP_EXPLORER, BUILD_EXPLORER },
    { APP_OPTIONS,  LV_SYMBOL_SETTINGS,  TXT_APP_OPTIONS_NAME, true,                 app_build_options },
    { APP_GALLERY,  LV_SYMBOL_IMAGE,     TXT_APP_GALLERY_NAME, CARUSOS_APP_GALLERY,  BUILD_GALLERY },
    { APP_ANIM,     LV_SYMBOL_PLAY,      TXT_APP_ANIM_NAME,    ENABLE_APP_ANIM,      BUILD_ANIM },
    { APP_MIC,      LV_SYMBOL_AUDIO,     TXT_APP_MIC_NAME,     ENABLE_APP_MIC_TEST,  BUILD_MIC },
    { APP_IMU,      LV_SYMBOL_GPS,       TXT_APP_IMU_NAME,     ENABLE_APP_IMU,       BUILD_IMU },
    { APP_DASHBOARD, LV_SYMBOL_LIST,     TXT_APP_DASHBOARD_NAME, CARUSOS_APP_DASHBOARD, BUILD_DASHBOARD },
    { APP_DEEPSLEEP, LV_SYMBOL_EJECT,    TXT_DEEP_SLEEP,       CARUSOS_USE_DEEP_SLEEP, NULL /* action: deep sleep */ },
};
static const int g_app_count = sizeof(g_apps) / sizeof(g_apps[0]);

static const carusos_app_t * app_by_id(int id) {
    for (int i = 0; i < g_app_count; i++) {
        if (g_apps[i].id == id) return &g_apps[i];
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Screen-owned resources (see apps.h)
// ---------------------------------------------------------------------------
static void owned_timer_delete_cb(lv_event_t * e) {
    lv_timer_delete((lv_timer_t *)lv_event_get_user_data(e));
}

lv_timer_t * ui_screen_timer(lv_obj_t * screen, lv_timer_cb_t cb, uint32_t period_ms, void * user_data) {
    lv_timer_t * t = lv_timer_create(cb, period_ms, user_data);
    lv_obj_add_event_cb(screen, owned_timer_delete_cb, LV_EVENT_DELETE, t);
    return t;
}

static void owned_mem_free_cb(lv_event_t * e) {
    lv_free(lv_event_get_user_data(e));
}

void * ui_screen_alloc(lv_obj_t * screen, size_t size) {
    void * p = lv_malloc_zeroed(size);
    if (p) lv_obj_add_event_cb(screen, owned_mem_free_cb, LV_EVENT_DELETE, p);
    return p;
}

// ---------------------------------------------------------------------------
// Status bar (clock + WiFi icon)
// ---------------------------------------------------------------------------
// The desktop has one, and every app window has its own copy in its title bar
// (stored as the window screen's user_data). The poll timer refreshes the
// desktop bar and the bar of whatever screen is active, so nested windows
// (Settings -> Sys Info -> back) keep working.
typedef struct {
    lv_obj_t * wifi;
    lv_obj_t * time;
    lv_obj_t * batt; // NULL when the PMU is compiled out
} status_bar_t;

static status_bar_t main_bar;

// Battery icon text for the status bar: level, plus a bolt while charging.
// USB symbol when running from USB without a battery. "" if no PMU.
static const char * battery_symbol() {
    int pct, mv; bool chg, usb;
    if (!backend_get_power(pct, chg, usb, mv)) return "";
    if (pct < 0) return usb ? LV_SYMBOL_USB : "";
    const char * lvl = pct > 80 ? LV_SYMBOL_BATTERY_FULL :
                       pct > 55 ? LV_SYMBOL_BATTERY_3 :
                       pct > 30 ? LV_SYMBOL_BATTERY_2 :
                       pct > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
    static char buf[16];
    snprintf(buf, sizeof(buf), "%s%s", chg ? LV_SYMBOL_CHARGE : "", lvl);
    return buf;
}

static void status_bar_refresh(const status_bar_t * sb, lv_color_t wifi_col, const struct tm * dt,
                               const char * batt) {
    lv_obj_set_style_text_color(sb->wifi, wifi_col, 0);
    if (dt) lv_label_set_text_fmt(sb->time, "%02d:%02d", dt->tm_hour, dt->tm_min);
    if (sb->batt) lv_label_set_text(sb->batt, batt);
}

// The battery icon sits left of the WiFi icon.
static lv_obj_t * status_bar_add_battery(lv_obj_t * bar) {
#if CARUSOS_USE_PMU
    lv_obj_t * b = lv_label_create(bar);
    lv_label_set_text(b, "");
    lv_obj_set_style_text_color(b, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, -40, 0);
    return b;
#else
    (void)bar;
    return NULL;
#endif
}

static void ui_poll_timer_cb(lv_timer_t * timer) {
    lv_color_t wifi_col = backend_is_wifi_connected() ? lv_color_hex(0x00FF00) : lv_color_hex(0x555555);

    // The system time is valid if NTP synced OR the RTC seeded it at boot,
    // so the clock runs regardless of WiFi.
    struct tm dt;
    const struct tm * dtp = backend_get_datetime(dt) ? &dt : NULL;

    const char * batt = battery_symbol();
    status_bar_refresh(&main_bar, wifi_col, dtp, batt);

    lv_obj_t * act = lv_scr_act();
    if (act != main_screen) {
        const status_bar_t * sb = (const status_bar_t *)lv_obj_get_user_data(act);
        if (sb) status_bar_refresh(sb, wifi_col, dtp, batt);
    }
}

// ---------------------------------------------------------------------------
// Window chrome + launcher
// ---------------------------------------------------------------------------
static void app_back_event_cb(lv_event_t * e) {
    lv_obj_t * return_screen = (lv_obj_t *)lv_event_get_user_data(e);
    if (!return_screen) return;
    // auto_del = true: LVGL deletes the app screen once the transition ends,
    // and its LV_EVENT_DELETE handlers release everything the app owned.
    lv_scr_load_anim_t anim = backend_get_animations() ? LV_SCR_LOAD_ANIM_FADE_ON : LV_SCR_LOAD_ANIM_NONE;
    lv_scr_load_anim(return_screen, anim, 200, 0, true);
}

static void create_app_window(const carusos_app_t * app, lv_obj_t * return_screen) {
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
    lv_obj_add_event_cb(back_btn, app_back_event_cb, LV_EVENT_CLICKED, (void *)return_screen);

    lv_obj_t * back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT TXT_BACK);
    lv_obj_center(back_label);

    // App Title
    lv_obj_t * title_label = lv_label_create(top_bar);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, 0);
    lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 0);

    // This window's own clock + WiFi icon, refreshed by ui_poll_timer_cb
    status_bar_t * sb = (status_bar_t *)ui_screen_alloc(app_screen, sizeof(status_bar_t));
    if (sb) {
        sb->wifi = lv_label_create(top_bar);
        lv_label_set_text(sb->wifi, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(sb->wifi, lv_color_hex(0x555555), 0);
        lv_obj_align(sb->wifi, LV_ALIGN_RIGHT_MID, -10, 0);

        sb->batt = status_bar_add_battery(top_bar);

        sb->time = lv_label_create(top_bar);
        lv_label_set_text(sb->time, "");
        lv_obj_set_style_text_color(sb->time, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(sb->time, LV_ALIGN_RIGHT_MID, CARUSOS_USE_PMU ? -85 : -40, 0);

        lv_obj_set_user_data(app_screen, sb);
    }

    // Content Area
    lv_obj_t * content = lv_label_create(app_screen);
    lv_obj_set_style_text_color(content, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 70);

    // Build the app-specific UI
    Serial.printf("[UI] Building app id=%d (%s)\n", app->id, app->name);
    app->build(app_screen, content, title_label);

    // Transition
    lv_scr_load_anim_t anim = backend_get_animations() ? LV_SCR_LOAD_ANIM_FADE_ON : LV_SCR_LOAD_ANIM_NONE;
    lv_scr_load_anim(app_screen, anim, 200, 0, false);
}

#if CARUSOS_USE_DEEP_SLEEP
// Deep sleep: the only wake source on this board is the BOOT button (GPIO0,
// active low, external pull-up): no interrupt line from the touch, PMU, RTC
// or IMU reaches the ESP32. Waking is a full reboot.
static void deep_sleep_timer_cb(lv_timer_t * t) {
    Serial.println("[Power] Entering deep sleep (BOOT wakes).");
    Serial.flush();
    lvgl_port_prepare_deep_sleep();
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
    esp_deep_sleep_start();
}

static void deep_sleep_start_ui() {
    // Tell the user how to wake it, then sleep 2 s later.
    lv_obj_t * scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_t * lbl = lv_label_create(scr);
    lv_label_set_text(lbl, TXT_DEEP_SLEEP_HINT);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl);
    lv_scr_load(scr);
    lv_timer_t * t = lv_timer_create(deep_sleep_timer_cb, 2000, NULL);
    lv_timer_set_repeat_count(t, 1);
}
#endif

static void app_launcher_event_cb(lv_event_t * e) {
    int app_id = (int)(intptr_t)lv_event_get_user_data(e);
    Serial.printf("[UI] Launch app id=%d\n", app_id);
    const carusos_app_t * app = app_by_id(app_id);

    if (!app || !app->enabled) return; // Disabled apps do nothing

    if (app->build == NULL) {
        // Action app (no window)
        if (app->id == APP_SLEEP) {
            lvgl_port_set_backlight(false); // Instant Sleep
        }
#if CARUSOS_USE_DEEP_SLEEP
        if (app->id == APP_DEEPSLEEP) deep_sleep_start_ui();
#endif
        return;
    }

    // Open window and pass current screen as return screen
    create_app_window(app, lv_scr_act());
}

void ui_add_app_icon(lv_obj_t * parent, int app_id) {
    const carusos_app_t * app = app_by_id(app_id);
    if (!app) return;

    // Hide disabled apps entirely unless configured to show them greyed-out.
    if (!app->enabled && !CARUSOS_SHOW_DISABLED_APPS) return;

    lv_obj_t * btn = lv_button_create(parent);
    lv_obj_set_size(btn, 110, 110);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a2a2a), 0);
    lv_obj_add_event_cb(btn, app_launcher_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)app_id);

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

// ---------------------------------------------------------------------------
// Desktop + boot
// ---------------------------------------------------------------------------
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

    main_bar.time = lv_label_create(status_bar);
    lv_label_set_text(main_bar.time, "--:--");
    lv_obj_set_style_text_color(main_bar.time, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(main_bar.time, LV_ALIGN_CENTER, 0, 0);

    main_bar.wifi = lv_label_create(status_bar);
    lv_label_set_text(main_bar.wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(main_bar.wifi, lv_color_hex(0x555555), 0); // Offline dark color
    lv_obj_align(main_bar.wifi, LV_ALIGN_RIGHT_MID, -10, 0);

    main_bar.batt = status_bar_add_battery(status_bar);

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
    ui_add_app_icon(app_grid, APP_DASHBOARD);
    ui_add_app_icon(app_grid, APP_CALC);
    ui_add_app_icon(app_grid, APP_GALLERY);
    ui_add_app_icon(app_grid, APP_ANIM);
    ui_add_app_icon(app_grid, APP_SETTINGS);
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
    lv_timer_create(boot_timer_cb, 500, NULL);

    // 4. Start the status bar polling timer (updates every 1s)
    lv_timer_create(ui_poll_timer_cb, 1000, NULL);

#if CARUSOS_DASHBOARD_ALERT
    // 5. Pre-event beeps, independent of the Dashboard screen
    dashboard_alerts_init();
#endif
}
