// Dashboard: a column of small cards, refreshed every second.
// Card types so far: recurring countdown (events from CARUSOS_DASHBOARD_EVENTS
// in customizations/config_local.h) and, with CARUSOS_DASHBOARD_REMOTE, items
// from a JSON list polled on Core 0. Build-time events always keep their card;
// remote items fill the slots left, most urgent first. A new card type adds
// its own create function that fills a dash_card_t with a tick callback.
// With CARUSOS_DASHBOARD_ALERT, a separate 1 s timer (not tied to the app
// screen) beeps softly once per occurrence, `alert` seconds before it.
#include "apps.h"

#if CARUSOS_APP_DASHBOARD
#include <time.h>

#define DASH_MAX_CARDS 4

// --- Recurring countdown config -------------------------------------------
typedef struct {
    const char * label;
    int64_t anchor; // any occurrence, unix epoch seconds (UTC)
    int32_t period; // seconds between occurrences
    // Seconds before each occurrence to beep (0 = no beep). Optional 4th field.
    int32_t alert = CARUSOS_DASHBOARD_ALERT_LEAD;
} dash_event_t;

#ifdef CARUSOS_DASHBOARD_EVENTS
static const dash_event_t k_events[] = { CARUSOS_DASHBOARD_EVENTS };
static const int k_event_count = sizeof(k_events) / sizeof(k_events[0]);
static_assert(sizeof(k_events) / sizeof(k_events[0]) <= DASH_MAX_CARDS,
              "CARUSOS_DASHBOARD_EVENTS: at most 4 events fit on the screen");
#else
static const dash_event_t * const k_events = NULL;
static const int k_event_count = 0;
#endif

// --- Cards ----------------------------------------------------------------
struct dash_card_t;
// `now` is unix time, or -1 while the clock is not valid yet.
typedef void (*dash_tick_fn)(dash_card_t * card, time_t now);

struct dash_card_t {
    dash_tick_fn tick;
    const void * cfg;
    int idx;           // remote cards: rank among the remote items
    lv_obj_t * box;
    lv_obj_t * title;
    lv_obj_t * value;  // big line
    lv_obj_t * detail; // small line under it
};

typedef struct {
    dash_card_t cards[DASH_MAX_CARDS];
    int count;
#if CARUSOS_DASHBOARD_REMOTE
    dash_remote_t remote;                   // copy of the last good list
    uint8_t order[DASH_REMOTE_MAX_ITEMS];   // remote.items indices, most urgent first
#endif
} dash_state_t;

// Time left as "1d 02h 03m 04s" / "2h 03m 04s" / "3m 04s"; negative with a "-".
static void dash_set_left(lv_obj_t * label, int64_t left) {
    const char * sign = left < 0 ? "-" : "";
    if (left < 0) left = -left;
    int d = (int)(left / 86400), h = (int)(left / 3600 % 24);
    int m = (int)(left / 60 % 60), s = (int)(left % 60);
    if (d > 0)      lv_label_set_text_fmt(label, "%s%dd %02dh %02dm %02ds", sign, d, h, m, s);
    else if (h > 0) lv_label_set_text_fmt(label, "%s%dh %02dm %02ds", sign, h, m, s);
    else            lv_label_set_text_fmt(label, "%s%dm %02ds", sign, m, s);
}

// Smallest occurrence >= now. Works for anchors in the past or the future.
static int64_t dash_next_occurrence(int64_t anchor, int64_t period, int64_t now) {
    int64_t d = now - anchor;
    int64_t k = d >= 0 ? (d + period - 1) / period : -((-d) / period);
    return anchor + k * period;
}

static void countdown_tick(dash_card_t * card, time_t now) {
    const dash_event_t * ev = (const dash_event_t *)card->cfg;
    if (ev->period <= 0) {
        lv_label_set_text(card->value, "--");
        lv_label_set_text(card->detail, TXT_DASH_BAD_PERIOD);
        return;
    }
    if (now < 0) {
        lv_label_set_text(card->value, "--");
        lv_label_set_text(card->detail, TXT_DASH_WAIT_TIME);
        return;
    }

    int64_t next = dash_next_occurrence(ev->anchor, ev->period, now);
    int64_t left = next - now;
    if (left == 0) lv_label_set_text(card->value, TXT_DASH_NOW);
    else           dash_set_left(card->value, left);

    // Local clock time of the next occurrence; the date only if not today.
    time_t nt = (time_t)next;
    struct tm at, today;
    localtime_r(&nt, &at);
    localtime_r(&now, &today);
#if CARUSOS_DASHBOARD_ALERT
    const char * bell = (ev->alert > 0 && backend_get_dash_alert()) ? "  " LV_SYMBOL_BELL : "";
#else
    const char * bell = "";
#endif
    if (at.tm_yday == today.tm_yday && at.tm_year == today.tm_year)
        lv_label_set_text_fmt(card->detail, TXT_DASH_AT " %02d:%02d%s", at.tm_hour, at.tm_min, bell);
    else
        lv_label_set_text_fmt(card->detail, TXT_DASH_AT " %02d:%02d (%02d/%02d)%s",
                              at.tm_hour, at.tm_min, at.tm_mday, at.tm_mon + 1, bell);
}

#if CARUSOS_DASHBOARD_REMOTE
// Most urgent first: red before yellow before green, then the nearest time;
// items without a time (to-dos) go after the timed ones of the same level.
static bool dash_remote_before(const dash_remote_item_t * a, const dash_remote_item_t * b) {
    if (a->lvl != b->lvl) return a->lvl > b->lvl;
    if ((a->at == 0) != (b->at == 0)) return a->at != 0;
    return a->at < b->at;
}

static void dash_remote_sort(dash_state_t * st) {
    for (int i = 0; i < st->remote.count; i++) {
        int j = i;
        st->order[j] = (uint8_t)i;
        while (j > 0 && dash_remote_before(&st->remote.items[st->order[j]],
                                           &st->remote.items[st->order[j - 1]])) {
            uint8_t tmp = st->order[j]; st->order[j] = st->order[j - 1]; st->order[j - 1] = tmp;
            j--;
        }
    }
}

static void remote_tick(dash_card_t * card, time_t now) {
    dash_state_t * st = (dash_state_t *)card->cfg;
    if (card->idx >= st->remote.count) {
        lv_obj_add_flag(card->box, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(card->box, LV_OBJ_FLAG_HIDDEN);
    const dash_remote_item_t * it = &st->remote.items[st->order[card->idx]];

    static const uint32_t lvl_color[] = { 0x00FF00, 0xFFC000, 0xFF4040 };
    lv_label_set_text_fmt(card->title, "%s  %s", it->t, it->w);
    lv_obj_set_style_text_color(card->value, lv_color_hex(lvl_color[it->lvl]), 0);

    // Stale: no good sync for longer than the list's ttl. Keeps showing it.
    bool stale = st->remote.ttl > 0 &&
                 (millis() - st->remote.synced_ms) / 1000 > (uint32_t)st->remote.ttl;
    const char * tail = stale ? "  " LV_SYMBOL_WARNING " " TXT_DASH_STALE : "";

    if (it->at == 0) {
        lv_label_set_text(card->value, TXT_DASH_TODO);
        lv_label_set_text(card->detail, tail);
        return;
    }
    if (now < 0) {
        lv_label_set_text(card->value, "--");
        lv_label_set_text(card->detail, TXT_DASH_WAIT_TIME);
        return;
    }
    int64_t left = it->at - (int64_t)now;
    if (left <= 0 && left > -60) lv_label_set_text(card->value, TXT_DASH_NOW);
    else                         dash_set_left(card->value, left);

    time_t at_t = (time_t)it->at;
    struct tm at, today;
    localtime_r(&at_t, &at);
    localtime_r(&now, &today);
    if (at.tm_yday == today.tm_yday && at.tm_year == today.tm_year)
        lv_label_set_text_fmt(card->detail, TXT_DASH_AT_ONCE " %02d:%02d%s", at.tm_hour, at.tm_min, tail);
    else
        lv_label_set_text_fmt(card->detail, TXT_DASH_AT_ONCE " %02d:%02d (%02d/%02d)%s",
                              at.tm_hour, at.tm_min, at.tm_mday, at.tm_mon + 1, tail);
}
#endif // CARUSOS_DASHBOARD_REMOTE

// Card frame: title, big value, small detail line. Only fonts other apps
// already use: each new Montserrat size costs tens of KB of flash.
static dash_card_t * dash_add_card(dash_state_t * st, lv_obj_t * list, const char * title) {
    dash_card_t * card = &st->cards[st->count++];

    lv_obj_t * box = lv_obj_create(list);
    card->box = box;
    lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x2a2a2a), 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_pad_all(box, 10, 0);
    lv_obj_set_layout(box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 2, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * t = lv_label_create(box);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xAAAAAA), 0);
    card->title = t;

    card->value = lv_label_create(box);
    lv_obj_set_style_text_font(card->value, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(card->value, lv_color_hex(0x00FF00), 0);

    card->detail = lv_label_create(box);
    lv_obj_set_style_text_font(card->detail, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(card->detail, lv_color_hex(0xFFFFFF), 0);
    return card;
}

static void dash_tick_cb(lv_timer_t * t) {
    dash_state_t * st = (dash_state_t *)lv_timer_get_user_data(t);
    struct tm dt;
    time_t now = backend_get_datetime(dt) ? time(NULL) : (time_t)-1;
#if CARUSOS_DASHBOARD_REMOTE
    if (backend_dash_remote_get(st->remote)) dash_remote_sort(st); // else keep the last copy
#endif
    for (int i = 0; i < st->count; i++) st->cards[i].tick(&st->cards[i], now);
}

void app_build_dashboard(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, TXT_APP_DASHBOARD_TITLE);

#if CARUSOS_DASHBOARD_REMOTE
    const bool remote_on = CARUSOS_DASHBOARD_REMOTE_URL[0] != '\0';
#else
    const bool remote_on = false;
#endif
    if (k_event_count == 0 && !remote_on) {
        lv_obj_set_width(content, LV_PCT(90));
        lv_label_set_long_mode(content, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(content, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(content, TXT_DASH_EMPTY);
        return;
    }
    lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN);

    // Card column below the 50px top bar.
    lv_obj_t * list = lv_obj_create(app_screen);
    lv_obj_set_size(list, LV_PCT(100), 430);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(list, 0, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 10, 0);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    dash_state_t * st = (dash_state_t *)ui_screen_alloc(app_screen, sizeof(dash_state_t));
    for (int i = 0; i < k_event_count; i++) {
        dash_card_t * card = dash_add_card(st, list, k_events[i].label);
        card->tick = countdown_tick;
        card->cfg = &k_events[i];
    }
#if CARUSOS_DASHBOARD_REMOTE
    // Remote items take the slots the build-time events leave free; each card
    // stays hidden until there is an item for it.
    if (remote_on) {
        for (int i = 0; st->count < DASH_MAX_CARDS; i++) {
            dash_card_t * card = dash_add_card(st, list, "");
            card->tick = remote_tick;
            card->cfg = st;
            card->idx = i;
            lv_obj_add_flag(card->box, LV_OBJ_FLAG_HIDDEN);
        }
    }
#endif

    lv_timer_t * t = ui_screen_timer(app_screen, dash_tick_cb, 1000, st);
    lv_timer_ready(t); // first refresh right away
}

// --- Pre-event beep (runs from boot, whatever screen is shown) -------------
#if CARUSOS_DASHBOARD_ALERT
static int64_t s_alerted[DASH_MAX_CARDS]; // occurrence already beeped for, per event

static void dash_alert_cb(lv_timer_t * t) {
    struct tm dt;
    if (!backend_get_datetime(dt) || !backend_get_dash_alert()) return;
    int64_t now = time(NULL);
    for (int i = 0; i < k_event_count; i++) {
        const dash_event_t * ev = &k_events[i];
        if (ev->alert <= 0 || ev->period <= 0) continue;
        int64_t next = dash_next_occurrence(ev->anchor, ev->period, now);
        int64_t left = next - now;
        if (left > 0 && left <= ev->alert && s_alerted[i] != next) {
            s_alerted[i] = next;
            backend_beep(CARUSOS_DASHBOARD_ALERT_VOLUME, 150);
        }
    }
}

void dashboard_alerts_init() {
    for (int i = 0; i < k_event_count; i++) {
        if (k_events[i].alert > 0) {
            lv_timer_create(dash_alert_cb, 1000, NULL);
            return;
        }
    }
}
#endif // CARUSOS_DASHBOARD_ALERT
#endif // CARUSOS_APP_DASHBOARD
