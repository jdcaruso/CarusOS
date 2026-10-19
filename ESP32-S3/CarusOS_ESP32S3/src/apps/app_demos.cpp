// Hardware / demo apps: Animation, Mic Test (ES7210), Motion (QMI8658 IMU).
// Each one is compiled out entirely when its flag is 0.
#include "apps.h"

#if ENABLE_APP_IMU
#include "../../imu_qmi8658.h"
#endif

// ---------------------------------------------------------------------------
// Animation: a blocky "8-bit" sprite that bounces around the screen and cycles
// colour on every wall hit. Pure lv_timer — no assets, no PSRAM, no network.
// ---------------------------------------------------------------------------
#if ENABLE_APP_ANIM
#define ANIM_SPRITE_SIZE 64
#define ANIM_AREA_TOP    55   // keep the sprite below the 50px top bar

typedef struct {
    lv_obj_t * sprite;
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

void app_build_anim(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, TXT_APP_ANIM_NAME);
    lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN); // hide the default content label

    anim_state_t * st = (anim_state_t *)ui_screen_alloc(app_screen, sizeof(anim_state_t));
    if (!st) return;
    st->x = 30;
    st->y = ANIM_AREA_TOP + 30;
    st->vx = 4;
    st->vy = 3;

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
    ui_screen_timer(app_screen, anim_tick_cb, 30, st);
}
#endif // ENABLE_APP_ANIM

// ---------------------------------------------------------------------------
// Mic Test: ES7210 capture + live level meter
// ---------------------------------------------------------------------------
#if ENABLE_APP_MIC_TEST
static void mic_switch_event_cb(lv_event_t * e) {
    lv_obj_t * sw = (lv_obj_t *)lv_event_get_target(e);
    if (lv_obj_has_state(sw, LV_STATE_CHECKED)) backend_mic_start();
    else                                        backend_mic_stop();
}

static void mic_bar_update_cb(lv_timer_t * t) {
    lv_bar_set_value((lv_obj_t *)lv_timer_get_user_data(t), backend_get_mic_level(), LV_ANIM_OFF);
}

// Leaving the app stops the capture (no-op if it was never started).
static void mic_cleanup_cb(lv_event_t * e) {
    backend_mic_stop();
}

void app_build_mic(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, TXT_APP_MIC_NAME);
    lv_label_set_text(content, TXT_MIC_DESC);
    lv_obj_set_style_text_align(content, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 70);

    // Mic ON/OFF switch
    lv_obj_t * sw = lv_switch_create(app_screen);
    lv_obj_align(sw, LV_ALIGN_CENTER, 0, -30);
    lv_obj_add_event_cb(sw, mic_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Live input level meter
    lv_obj_t * bar = lv_bar_create(app_screen);
    lv_obj_set_size(bar, 320, 30);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 40);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);

    // Refresh the meter ~10x/s while the app is open
    ui_screen_timer(app_screen, mic_bar_update_cb, 100, bar);
    lv_obj_add_event_cb(app_screen, mic_cleanup_cb, LV_EVENT_DELETE, NULL);
}
#endif // ENABLE_APP_MIC_TEST

// ---------------------------------------------------------------------------
// Motion: tilt the board and a ball rolls "downhill" inside a box
// ---------------------------------------------------------------------------
#if ENABLE_APP_IMU
#define IMU_BOX_SIZE   300
#define IMU_BALL_SIZE  44

static void imu_tick_cb(lv_timer_t * t) {
    lv_obj_t * ball = (lv_obj_t *)lv_timer_get_user_data(t);
    float ax, ay, az;
    if (!imu_get_accel(ax, ay, az)) return;

    // ax/ay are in g (~ -1..1 when tilted). Map to pixels within the box.
    int range = (IMU_BOX_SIZE - IMU_BALL_SIZE) / 2 - 4;
    int dx = (int)(ax * range);
    int dy = (int)(-ay * range); // screen Y grows downward; invert so it feels natural
    if (dx >  range) dx =  range;
    if (dx < -range) dx = -range;
    if (dy >  range) dy =  range;
    if (dy < -range) dy = -range;
    lv_obj_align(ball, LV_ALIGN_CENTER, dx, dy);
}

void app_build_imu(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, TXT_APP_IMU_TITLE);
    lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN); // hide the default content label

    if (!imu_begin()) {
        lv_obj_clear_flag(content, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(content, TXT_IMU_NOT_FOUND);
        lv_obj_center(content);
        return;
    }

    // Bounding box
    lv_obj_t * box = lv_obj_create(app_screen);
    lv_obj_set_size(box, IMU_BOX_SIZE, IMU_BOX_SIZE);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 25);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x101010), 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    // Ball (moves with tilt)
    lv_obj_t * ball = lv_obj_create(box);
    lv_obj_set_size(ball, IMU_BALL_SIZE, IMU_BALL_SIZE);
    lv_obj_set_style_radius(ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ball, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_border_width(ball, 0, 0);
    lv_obj_align(ball, LV_ALIGN_CENTER, 0, 0);

    // ~33 FPS read + move
    ui_screen_timer(app_screen, imu_tick_cb, 30, ball);
}
#endif // ENABLE_APP_IMU
