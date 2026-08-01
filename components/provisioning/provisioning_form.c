#include "provisioning_form.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Percent/plus-decodes in[0..inlen) into out (NUL-terminated). Returns
// decoded length, or -1 if it would overflow out.
static int url_decode(const char *in, size_t inlen, char *out, size_t outlen) {
    size_t oi = 0;
    for (size_t ii = 0; ii < inlen; ii++) {
        char c = in[ii];
        char decoded;
        if (c == '+') {
            decoded = ' ';
        } else if (c == '%' && ii + 2 < inlen) {
            int hi = hex_val(in[ii + 1]);
            int lo = hex_val(in[ii + 2]);
            if (hi < 0 || lo < 0) {
                decoded = '%';
            } else {
                decoded = (char)((hi << 4) | lo);
                ii += 2;
            }
        } else {
            decoded = c;
        }
        if (oi + 1 >= outlen) return -1;
        out[oi++] = decoded;
    }
    out[oi] = '\0';
    return (int)oi;
}

// Finds `key=` as a whole field within body[0..len) (fields separated by
// '&'). Sets *value_len to the raw (still encoded) value length. Returns a
// pointer to the value start, or NULL if key is not present as a field.
static const char *find_field(const char *body, size_t len, const char *key,
                               size_t *value_len) {
    size_t keylen = strlen(key);
    size_t i = 0;
    while (i < len) {
        size_t field_start = i;
        size_t j = field_start;
        while (j < len && body[j] != '&') j++;
        if (j - field_start > keylen && body[field_start + keylen] == '=' &&
            strncmp(body + field_start, key, keylen) == 0) {
            const char *val = body + field_start + keylen + 1;
            *value_len = j - (field_start + keylen + 1);
            return val;
        }
        i = j + 1;
    }
    return NULL;
}

int provisioning_parse_form(const char *body, size_t len, wifi_cfg_t *out) {
    size_t vlen;
    const char *v;

    v = find_field(body, len, "ssid", &vlen);
    if (!v || vlen == 0) return -1;
    if (url_decode(v, vlen, out->ssid, sizeof out->ssid) < 0) return -1;
    if (out->ssid[0] == '\0') return -1;

    v = find_field(body, len, "password", &vlen);
    if (v && vlen > 0) {
        if (url_decode(v, vlen, out->password, sizeof out->password) < 0) return -1;
    } else {
        out->password[0] = '\0';
    }

    v = find_field(body, len, "host", &vlen);
    if (!v || vlen == 0) return -1;
    if (url_decode(v, vlen, out->server_host, sizeof out->server_host) < 0) return -1;

    v = find_field(body, len, "port", &vlen);
    if (!v || vlen == 0 || vlen >= 7) return -1;
    char portbuf[8];
    memcpy(portbuf, v, vlen);
    portbuf[vlen] = '\0';
    char *endptr;
    long port = strtol(portbuf, &endptr, 10);
    if (*endptr != '\0' || port < 1 || port > 65535) return -1;
    out->server_port = (int)port;

    return 0;
}

// ---------------------------------------------------------------------------
// Page rendering
//
// The page is assembled through a bounded appender rather than one big
// snprintf: it now interleaves static markup with a variable number of network
// rows, and a single format string can neither loop nor keep a running
// overflow check. sink_t carries "have we overflowed yet" alongside the cursor
// so every append can be written unconditionally and checked once at the end.
// ---------------------------------------------------------------------------

typedef struct {
    char  *buf;
    size_t cap;   // total bytes available, including the terminating NUL
    size_t len;
    bool   ok;
} sink_t;

static void sink_init(sink_t *s, char *buf, size_t cap) {
    s->buf = buf; s->cap = cap; s->len = 0; s->ok = cap > 0;
    if (s->ok) buf[0] = '\0';
}

// Raw append — the argument is markup, never user data.
static void s_put(sink_t *s, const char *str) {
    if (!s->ok) return;
    size_t n = strlen(str);
    if (s->len + n + 1 > s->cap) { s->ok = false; return; }
    memcpy(s->buf + s->len, str, n);
    s->len += n;
    s->buf[s->len] = '\0';
}

static void s_putf(sink_t *s, const char *fmt, ...) {
    if (!s->ok) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(s->buf + s->len, s->cap - s->len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= s->cap - s->len) { s->ok = false; return; }
    s->len += (size_t)n;
}

// Escaping append — for anything that came from the network (SSIDs from the
// scan, the saved config) or from an error path. Escapes the four characters
// that can break out of either an attribute value or element text, so a single
// helper is correct in both positions and no call site has to pick.
static void s_esc(sink_t *s, const char *str) {
    if (!s->ok || !str) return;
    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '&': s_put(s, "&amp;");  break;
            case '"': s_put(s, "&quot;"); break;
            case '<': s_put(s, "&lt;");   break;
            case '>': s_put(s, "&gt;");   break;
            default: {
                if (!s->ok) return;
                if (s->len + 2 > s->cap) { s->ok = false; return; }
                s->buf[s->len++] = *p;
                s->buf[s->len] = '\0';
                break;
            }
        }
        if (!s->ok) return;
    }
}

int provisioning_signal_bars(int rssi_dbm) {
    // Same buckets as statusbar_wifi_bars, minus its "0 = disconnected" case:
    // anything in a scan result is by definition reachable, so the weakest
    // bucket is 1 bar rather than none.
    if (rssi_dbm >= -55) return 4;
    if (rssi_dbm >= -65) return 3;
    if (rssi_dbm >= -75) return 2;
    return 1;
}

int provisioning_sort_networks(prov_network_t *nets, int n) {
    if (!nets || n <= 0) return 0;

    // Drop hidden/blank SSIDs first: they can't be identified in a list, and
    // leaving them in would waste both rows and page bytes. Typing the name
    // into the SSID field still works for those.
    int keep = 0;
    for (int i = 0; i < n; i++)
        if (nets[i].ssid[0] != '\0') nets[keep++] = nets[i];
    n = keep;

    // Insertion sort, strongest first. n is at most the scan cap (~32) and this
    // runs once per portal boot, so the O(n^2) is irrelevant next to pulling in
    // qsort.
    for (int i = 1; i < n; i++) {
        prov_network_t cur = nets[i];
        int j = i - 1;
        while (j >= 0 && nets[j].rssi < cur.rssi) { nets[j + 1] = nets[j]; j--; }
        nets[j + 1] = cur;
    }

    // De-duplicate by name. Sorted order means the first sighting of an SSID is
    // its strongest, so a linear scan that keeps the first and drops later
    // repeats gives "one row per network, at its best signal" — which is what a
    // dual-band router or a mesh with several nodes would otherwise flood the
    // list with.
    keep = 0;
    for (int i = 0; i < n; i++) {
        bool dup = false;
        for (int j = 0; j < keep; j++)
            if (strcmp(nets[j].ssid, nets[i].ssid) == 0) { dup = true; break; }
        if (!dup) nets[keep++] = nets[i];
    }

    return keep > PROV_MAX_NETWORKS ? PROV_MAX_NETWORKS : keep;
}

// Shared document head. Dark-only on purpose: the portal is a single-purpose
// page shown for a minute on a phone, and committing to one palette costs half
// the CSS of supporting both schemes.
static void render_head(sink_t *s, const char *title) {
    s_put(s,
        "<!doctype html><html lang=en><head><meta charset=utf-8>"
        "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
        "<title>");
    s_esc(s, title);
    s_put(s, "</title><style>"
        ":root{--bg:#0f1115;--card:#181b22;--line:#272b35;--fg:#e7eaf0;"
        "--mut:#8b94a7;--acc:#34d399;--err:#f87171}"
        "*{box-sizing:border-box}"
        "body{margin:0;padding:24px 16px;background:var(--bg);color:var(--fg);"
        "font:16px/1.5 -apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}"
        "main{max-width:420px;margin:0 auto}"
        "header{text-align:center;margin-bottom:22px}"
        ".eyes{display:flex;gap:9px;justify-content:center;margin-bottom:12px}"
        ".eyes i{width:18px;height:26px;border-radius:7px;background:var(--acc);"
        "box-shadow:0 0 14px rgba(52,211,153,.45)}"
        "h1{font-size:22px;margin:0;letter-spacing:-.01em}"
        ".sub{color:var(--mut);margin:4px 0 0;font-size:14px}"
        ".err{background:rgba(248,113,113,.12);border:1px solid rgba(248,113,113,.4);"
        "color:var(--err);padding:10px 12px;border-radius:10px;font-size:14px;margin:0 0 16px}"
        "section{background:var(--card);border:1px solid var(--line);border-radius:14px;"
        "padding:16px;margin-bottom:14px}"
        "h2{font-size:12px;text-transform:uppercase;letter-spacing:.07em;color:var(--mut);"
        "margin:0 0 13px;display:flex;align-items:center;gap:8px}"
        "h2 .n{width:20px;height:20px;border-radius:50%;background:var(--acc);color:#0f1115;"
        "display:grid;place-items:center;font-size:12px;font-weight:700;letter-spacing:0}"
        ".nets{list-style:none;margin:0 0 14px;padding:0;max-height:232px;overflow-y:auto;"
        "border:1px solid var(--line);border-radius:10px}"
        ".nets li+li{border-top:1px solid var(--line)}"
        ".net{width:100%;display:flex;align-items:center;justify-content:space-between;"
        "gap:10px;background:none;border:0;color:var(--fg);font:inherit;padding:11px 12px;"
        "cursor:pointer;text-align:left}"
        ".net[aria-pressed=true]{background:rgba(52,211,153,.14);box-shadow:inset 3px 0 0 var(--acc)}"
        ".name{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
        ".meta{display:flex;align-items:center;gap:8px;flex:none;color:var(--mut)}"
        ".lk{width:11px;height:11px;fill:currentColor}"
        ".bars{display:flex;align-items:flex-end;gap:2px;height:12px}"
        ".bars i{width:3px;background:currentColor;opacity:.22;border-radius:1px}"
        ".bars i:nth-child(1){height:4px}.bars i:nth-child(2){height:7px}"
        ".bars i:nth-child(3){height:10px}.bars i:nth-child(4){height:13px}"
        ".b1 i:nth-child(-n+1),.b2 i:nth-child(-n+2),.b3 i:nth-child(-n+3),"
        ".b4 i:nth-child(-n+4){opacity:1;color:var(--acc)}"
        ".empty{color:var(--mut);font-size:13px;padding:14px 12px;text-align:center;"
        "border:1px dashed var(--line);border-radius:10px;margin-bottom:14px}"
        "label{display:block;font-size:13px;color:var(--mut);margin-bottom:12px}"
        "label:last-of-type{margin-bottom:0}"
        "input{width:100%;margin-top:6px;padding:11px 12px;background:#0f1115;"
        "border:1px solid var(--line);border-radius:10px;color:var(--fg);font-size:16px}"
        "input:focus{outline:none;border-color:var(--acc)}"
        ".pw{position:relative;display:block}"
        "#tog{position:absolute;right:5px;bottom:5px;background:none;border:0;"
        "color:var(--acc);font:inherit;font-size:13px;padding:7px 8px;cursor:pointer}"
        "small{display:block;margin-top:5px;font-size:12px;color:var(--mut)}"
        ".row{display:flex;gap:10px}.row label:first-child{flex:1}.row label:last-child{width:104px}"
        ".save{width:100%;padding:14px;background:var(--acc);color:#0b0d11;border:0;"
        "border-radius:12px;font:inherit;font-size:16px;font-weight:700;cursor:pointer}"
        ".save:active{opacity:.85}"
        ".foot{text-align:center;color:var(--mut);font-size:12px;margin:14px 0 0}"
        "</style></head><body><main>"
        "<header><div class=eyes><i></i><i></i></div>");
}

int provisioning_render_form(char *buf, size_t buflen, const wifi_cfg_t *cfg,
                              const char *error_msg,
                              const prov_network_t *nets, int n_nets) {
    sink_t s;
    sink_init(&s, buf, buflen);

    render_head(&s, "Lugo setup");
    s_put(&s, "<h1>Lugo setup</h1><p class=sub>Connect your assistant to WiFi</p></header>");

    if (error_msg && error_msg[0]) {
        s_put(&s, "<p class=err>");
        s_esc(&s, error_msg);
        s_put(&s, "</p>");
    }

    s_put(&s, "<form method=post action=/save>"
              // One <symbol>, referenced once per secure row, so the padlock
              // costs ~30 bytes per network instead of a repeated path.
              "<svg style=display:none><symbol id=lk viewBox=\"0 0 24 24\">"
              "<path d=\"M17 9V7a5 5 0 0 0-10 0v2H5v13h14V9h-2zM9 7a3 3 0 0 1 6 0v2H9V7z\"/>"
              "</symbol></svg>"
              "<section><h2><span class=n>1</span>Choose a network</h2>");

    if (nets && n_nets > 0) {
        s_put(&s, "<ul class=nets>");
        for (int i = 0; i < n_nets; i++) {
            // Pre-select whatever is already configured, so a re-visit of the
            // portal (e.g. after a wrong password) shows which network is in
            // play instead of an empty selection.
            bool sel = cfg->ssid[0] && strcmp(cfg->ssid, nets[i].ssid) == 0;
            s_put(&s, "<li><button type=button class=net aria-pressed=");
            s_put(&s, sel ? "true" : "false");
            // data-s carries the exact SSID; the click handler copies it into
            // the input via .dataset (a string assignment, never parsed as
            // markup), so a hostile AP name cannot inject anything.
            s_put(&s, " data-s=\"");
            s_esc(&s, nets[i].ssid);
            s_put(&s, "\"><span class=name>");
            s_esc(&s, nets[i].ssid);
            s_put(&s, "</span><span class=meta>");
            if (nets[i].secure) s_put(&s, "<svg class=lk><use href=#lk /></svg>");
            s_putf(&s, "<span class=\"bars b%d\"><i></i><i></i><i></i><i></i></span>",
                   provisioning_signal_bars(nets[i].rssi));
            s_put(&s, "</span></button></li>");
        }
        s_put(&s, "</ul>");
    } else {
        s_put(&s, "<p class=empty>No networks found in the last scan.<br>"
                  "Type the name below, or restart the device to scan again.</p>");
    }

    s_put(&s, "<label>Network name<input name=ssid id=ssid autocomplete=off required value=\"");
    s_esc(&s, cfg->ssid);
    s_put(&s, "\"></label>"
              "<label>Password<span class=pw>"
              "<input name=password id=pw type=password autocomplete=off>"
              "<button type=button id=tog>Show</button></span>"
              "<small>Leave blank to keep the saved password</small></label>"
              "</section>"
              "<section><h2><span class=n>2</span>Gateway</h2><div class=row>"
              "<label>Host<input name=host required value=\"");
    s_esc(&s, cfg->server_host);
    s_put(&s, "\"></label><label>Port<input name=port type=number min=1 max=65535 required value=\"");
    s_putf(&s, "%d", cfg->server_port);
    s_put(&s, "\"></label></div></section>"
              "<button class=save type=submit>Save &amp; restart</button></form>"
              "<p class=foot>The device restarts after saving</p></main>"
              "<script>"
              "var q=function(x){return document.querySelectorAll(x)},"
              "ss=document.getElementById('ssid'),pw=document.getElementById('pw'),"
              "tg=document.getElementById('tog');"
              "q('.net').forEach(function(b){b.onclick=function(){"
              "q('.net').forEach(function(o){o.setAttribute('aria-pressed','false')});"
              "b.setAttribute('aria-pressed','true');ss.value=b.dataset.s;pw.focus();};});"
              "tg.onclick=function(){var h=pw.type=='password';pw.type=h?'text':'password';"
              "tg.textContent=h?'Hide':'Show';};"
              "</script></body></html>");

    if (!s.ok) return -1;
    return (int)s.len;
}

int provisioning_render_saved(char *buf, size_t buflen) {
    sink_t s;
    sink_init(&s, buf, buflen);
    render_head(&s, "Saved");
    s_put(&s, "<h1>Saved</h1><p class=sub>Restarting and connecting...</p></header>"
              "<section><p class=foot style=margin:0>You can close this page. "
              "If the device cannot join the network it will bring this setup "
              "portal back up.</p></section></main></body></html>");
    if (!s.ok) return -1;
    return (int)s.len;
}
