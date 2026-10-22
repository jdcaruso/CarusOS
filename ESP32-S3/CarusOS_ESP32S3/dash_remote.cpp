// Dashboard remote items (Core 0): polls a small JSON list over HTTP(S) and
// keeps the last good copy for the Dashboard app, which reads it through
// backend_dash_remote_get(). If the source goes away the last list stays;
// the app shows it as stale once `ttl` passes without a good sync.
//
// Format v1 (unknown fields are ignored, so new ones don't break old boards):
//   {"v":1,"updated":<epoch>,"ttl":900,"items":[
//     {"id":"c1","t":"C1","k":"meet","lvl":1,"w":"call","at":<epoch>,"beep":600}]}
// There is no JSON library in the build, so a tiny scanner below reads just
// this shape: flat item objects with string and integer values.
#include "backend_core.h"
#include "carusos_config.h"

#if CARUSOS_DASHBOARD_REMOTE
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/semphr.h>

#define DASH_REMOTE_MAX_BYTES 4096 // a v1 list with a few items is ~500 bytes

static dash_remote_t s_remote;           // last good list, guarded by s_lock
static SemaphoreHandle_t s_lock = NULL;

// --- Minimal JSON scanner -------------------------------------------------
static const char * skip_ws(const char * p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

// String at p (on the opening quote) into out, truncated to n. Escapes keep
// the escaped char as is (labels are short codes). Returns the char after the
// closing quote, or NULL. out may be NULL to just skip it.
static const char * read_str(const char * p, char * out, size_t n) {
    if (*p != '"') return NULL;
    p++;
    size_t i = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            if (!*p) return NULL;
            c = *p++;
        }
        if (out && i + 1 < n) out[i++] = c;
    }
    if (out && n) out[i] = '\0';
    return *p == '"' ? p + 1 : NULL;
}

// Skips any value (string, object, array, number, literal). NULL if malformed.
static const char * skip_value(const char * p) {
    p = skip_ws(p);
    if (*p == '"') return read_str(p, NULL, 0);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (*p) {
            if (*p == '"') {
                p = read_str(p, NULL, 0);
                if (!p) return NULL;
                continue;
            }
            if (*p == '{' || *p == '[') depth++;
            else if ((*p == '}' || *p == ']') && --depth == 0) return p + 1;
            p++;
        }
        return NULL;
    }
    const char * s = p;
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\r' && *p != '\n' && *p != '\t') p++;
    return p == s ? NULL : p;
}

// Integer value at p, or false if it isn't a number.
static bool read_int(const char * p, int64_t & out) {
    if (*p != '-' && (*p < '0' || *p > '9')) return false;
    out = strtoll(p, NULL, 10);
    return true;
}

// Calls fn(key, value, ctx) for each member of the object at p. fn returns the
// char after the value it consumed, or NULL to have the value skipped.
typedef const char * (*member_fn)(const char * key, const char * val, void * ctx);

static const char * walk_object(const char * p, member_fn fn, void * ctx) {
    p = skip_ws(p);
    if (*p != '{') return NULL;
    p = skip_ws(p + 1);
    if (*p == '}') return p + 1;
    while (1) {
        char key[16];
        p = read_str(p, key, sizeof(key));
        if (!p) return NULL;
        p = skip_ws(p);
        if (*p != ':') return NULL;
        const char * v = skip_ws(p + 1);
        const char * next = fn(key, v, ctx);
        p = next ? next : skip_value(v);
        if (!p) return NULL;
        p = skip_ws(p);
        if (*p == ',') { p = skip_ws(p + 1); continue; }
        if (*p == '}') return p + 1;
        return NULL;
    }
}

static const char * item_member(const char * key, const char * v, void * ctx) {
    dash_remote_item_t * it = (dash_remote_item_t *)ctx;
    int64_t n;
    if (!strcmp(key, "id")) return read_str(v, it->id, sizeof(it->id));
    if (!strcmp(key, "t"))  return read_str(v, it->t, sizeof(it->t));
    if (!strcmp(key, "w"))  return read_str(v, it->w, sizeof(it->w));
    if (!strcmp(key, "k"))  return read_str(v, it->k, sizeof(it->k));
    if (!strcmp(key, "lvl")  && read_int(v, n)) it->lvl = n < 0 ? 0 : (n > 2 ? 2 : (int8_t)n);
    if (!strcmp(key, "at")   && read_int(v, n)) it->at = n;
    if (!strcmp(key, "beep") && read_int(v, n)) it->beep = (int32_t)n;
    return NULL; // numbers and unknown members: let the walker skip them
}

typedef struct {
    dash_remote_t * out;
    bool bad;
} top_ctx_t;

static const char * top_member(const char * key, const char * v, void * ctx) {
    top_ctx_t * c = (top_ctx_t *)ctx;
    int64_t n;
    if (!strcmp(key, "ttl") && read_int(v, n)) c->out->ttl = (int32_t)n;
    if (strcmp(key, "items") || *v != '[') return NULL;

    const char * p = skip_ws(v + 1);
    if (*p == ']') return p + 1;
    while (1) {
        if (*p == '{' && c->out->count < DASH_REMOTE_MAX_ITEMS) {
            dash_remote_item_t * it = &c->out->items[c->out->count];
            memset(it, 0, sizeof(*it));
            p = walk_object(p, item_member, it);
            if (it->t[0]) c->out->count++; // an item without a label is dropped
        } else {
            p = skip_value(p);
        }
        if (!p) { c->bad = true; return NULL; }
        p = skip_ws(p);
        if (*p == ',') { p = skip_ws(p + 1); continue; }
        if (*p == ']') return p + 1;
        c->bad = true;
        return NULL;
    }
}

static bool dash_remote_parse(const char * json, dash_remote_t & out) {
    memset(&out, 0, sizeof(out));
    top_ctx_t c = { &out, false };
    return walk_object(json, top_member, &c) && !c.bad;
}

// --- Fetch ------------------------------------------------------------------
static bool dash_remote_fetch(dash_remote_t & out) {
    HTTPClient http;
    WiFiClientSecure secure;
    String url = CARUSOS_DASHBOARD_REMOTE_URL;
    if (url.startsWith("https")) {
        secure.setInsecure(); // no cert validation, same as backend_download_file (AGENTS.md)
        http.begin(secure, url);
    } else {
        http.begin(url);
    }
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int code = http.GET();
    bool ok = false;
    if (code == HTTP_CODE_OK) {
        int len = http.getSize();
        if (len > DASH_REMOTE_MAX_BYTES) {
            Serial.printf("[Dash] remote list too big: %d bytes\n", len);
        } else {
            String body = http.getString();
            if (body.length() > DASH_REMOTE_MAX_BYTES) {
                Serial.printf("[Dash] remote list too big: %u bytes\n", body.length());
            } else if (!dash_remote_parse(body.c_str(), out)) {
                Serial.println("[Dash] remote list: bad JSON, keeping the last one");
            } else {
                ok = true;
            }
        }
    } else {
        Serial.printf("[Dash] remote GET failed: %d %s\n", code, http.errorToString(code).c_str());
    }
    http.end();
    return ok;
}

static void Task_DashRemote(void * pv) {
    static dash_remote_t fresh; // static: keeps ~600 bytes off this task's stack
    uint32_t seq = 0;
    while (1) {
        if (!backend_is_wifi_connected()) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        bool ok = dash_remote_fetch(fresh);
        if (ok) {
            fresh.seq = ++seq;
            fresh.synced_ms = millis();
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_remote = fresh;
            xSemaphoreGive(s_lock);
            Serial.printf("[Dash] remote sync ok: %d items\n", fresh.count);
        }
        // After a failure, retry sooner (but never hammer the source).
        uint32_t wait_s = ok ? CARUSOS_DASHBOARD_REMOTE_POLL_S : 30;
        vTaskDelay(pdMS_TO_TICKS(wait_s * 1000));
    }
}

void dash_remote_start() {
    if (CARUSOS_DASHBOARD_REMOTE_URL[0] == '\0') return; // no URL: feature idle
    s_lock = xSemaphoreCreateMutex();
    // TLS needs a deep stack; this task only runs during a poll.
    xTaskCreatePinnedToCore(Task_DashRemote, "TaskDashRemote", 10240, NULL, 1, NULL, 0);
}

bool backend_dash_remote_get(dash_remote_t & out) {
    if (!s_lock || s_remote.seq == 0) return false;
    if (xSemaphoreTake(s_lock, 0) != pdTRUE) return false; // busy: caller keeps its copy
    out = s_remote;
    xSemaphoreGive(s_lock);
    return true;
}
#endif // CARUSOS_DASHBOARD_REMOTE
