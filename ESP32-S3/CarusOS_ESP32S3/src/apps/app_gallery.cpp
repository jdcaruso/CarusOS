// Web Gallery: downloads one PNG (CARUSOS_GALLERY_IMAGE_URL) and shows it.
#include "apps.h"

// Public LVGL API, but since 9.5 lvgl.h only exposes its header with
// LV_USE_PRIVATE_API. Declare it here instead of reaching into lvgl/src.
extern "C" void lv_image_cache_drop(const void * src);

#if CARUSOS_APP_GALLERY
// Per-instance state. Plain malloc (NOT lv_malloc / ui_screen_alloc): if the
// user leaves mid-download, ownership moves to the Core 0 download task, which
// frees it without touching LVGL's heap.
typedef struct {
    volatile bool    finished;   // set by the DL task when the download attempt ends
    volatile bool    orphaned;   // set by cleanup if the screen died mid-download
    void *           buffer;     // downloaded bytes, until moved into dsc
    size_t           size;
    lv_image_dsc_t * dsc;        // what the lv_image shows; freed with the screen
    lv_obj_t *       label;
    lv_obj_t *       img;
} gallery_state_t;

// Core 0: download, publish the result, and self-clean if the UI already left.
static void gallery_dl_task(void * p) {
    gallery_state_t * st = (gallery_state_t *)p;
    size_t s = 0;
    void * b = backend_download_file(CARUSOS_GALLERY_IMAGE_URL, &s);
    st->buffer = b;
    st->size = s;
    st->finished = true;
    // If the screen was destroyed while downloading, nobody else owns st now.
    if (st->orphaned) {
        if (st->buffer) free(st->buffer);
        free(st);
    }
    vTaskDelete(NULL);
}

// Core 1: wait for the download, then show it once.
static void gallery_poll_cb(lv_timer_t * t) {
    gallery_state_t * st = (gallery_state_t *)lv_timer_get_user_data(t);
    if (!st->finished) return;
    lv_timer_pause(t); // done polling; the timer itself dies with the screen

    lv_image_dsc_t * dsc = NULL;
    if (st->buffer && st->size > 0) {
        dsc = (lv_image_dsc_t *)malloc(sizeof(lv_image_dsc_t));
    }
    if (!dsc) {
        lv_label_set_text(st->label, TXT_GAL_ERROR);
        return;
    }

    memset(dsc, 0, sizeof(*dsc));
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_RAW; // raw PNG bytes, decoded by LodePNG
    dsc->data_size = st->size;
    dsc->data = (const uint8_t *)st->buffer;
    st->buffer = NULL; // ownership moves to dsc
    st->dsc = dsc;

    lv_label_set_text(st->label, ""); // clear "downloading"
    lv_image_set_src(st->img, dsc);
}

// Core 1: screen destroyed. This runs only when LVGL deletes the screen, i.e.
// after the Back transition finished drawing it, so the image data is no
// longer in use. (Freeing it in the Back click handler, as before, left the
// fade-out animation rendering freed memory.)
static void gallery_cleanup_cb(lv_event_t * e) {
    gallery_state_t * st = (gallery_state_t *)lv_event_get_user_data(e);
    if (st->dsc) {
        lv_image_cache_drop(st->dsc);
        free((void *)st->dsc->data);
        free(st->dsc);
        st->dsc = NULL;
    }
    if (st->finished) {
        if (st->buffer) free(st->buffer); // only set if never moved into dsc
        free(st);
    } else {
        st->orphaned = true; // the DL task frees st (and its buffer) when it ends
    }
}

void app_build_gallery(lv_obj_t * app_screen, lv_obj_t * content, lv_obj_t * title_label) {
    lv_label_set_text(title_label, TXT_APP_GALLERY_NAME);
    lv_label_set_text(content, TXT_GAL_DOWNLOADING);

    gallery_state_t * st = (gallery_state_t *)calloc(1, sizeof(gallery_state_t));
    if (!st) return;
    st->label = content;
    st->img = lv_image_create(app_screen);
    lv_obj_center(st->img);

    // Tie st's lifetime to the screen (fires on any exit path).
    lv_obj_add_event_cb(app_screen, gallery_cleanup_cb, LV_EVENT_DELETE, st);

    // Background HTTP download on Core 0 so LVGL transitions don't freeze.
    xTaskCreatePinnedToCore(gallery_dl_task, "GalDL", 8192, st, 1, NULL, 0);

    ui_screen_timer(app_screen, gallery_poll_cb, 100, st);
}
#endif // CARUSOS_APP_GALLERY
