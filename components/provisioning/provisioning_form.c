#include "provisioning_form.h"
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

// Escapes &, ", <, > for safe inclusion inside an HTML attribute value.
// Returns length written (excluding NUL), or -1 if buf too small.
static int escape_attr(const char *in, char *out, size_t outlen) {
    size_t oi = 0;
    for (const char *p = in; *p; p++) {
        const char *rep;
        switch (*p) {
            case '&': rep = "&amp;"; break;
            case '"': rep = "&quot;"; break;
            case '<': rep = "&lt;"; break;
            case '>': rep = "&gt;"; break;
            default: rep = NULL; break;
        }
        size_t rlen = rep ? strlen(rep) : 1;
        if (oi + rlen >= outlen) return -1;
        if (rep) { memcpy(out + oi, rep, rlen); oi += rlen; }
        else { out[oi++] = *p; }
    }
    out[oi] = '\0';
    return (int)oi;
}

int provisioning_render_form(char *buf, size_t buflen, const wifi_cfg_t *cfg,
                              const char *error_msg) {
    char ssid_esc[WIFI_CFG_SSID_MAX * 6 + 1];
    char host_esc[WIFI_CFG_HOST_MAX * 6 + 1];
    if (escape_attr(cfg->ssid, ssid_esc, sizeof ssid_esc) < 0) return -1;
    if (escape_attr(cfg->server_host, host_esc, sizeof host_esc) < 0) return -1;

    int n = snprintf(buf, buflen,
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
        "<title>esp32-assistant setup</title></head><body>"
        "<h1>esp32-assistant setup</h1>"
        "%s%s%s"
        "<form method=post action=/save>"
        "<label>WiFi SSID<br><input name=ssid value=\"%s\" required></label><br><br>"
        "<label>WiFi password (leave blank to keep the saved one)<br>"
        "<input name=password type=password></label><br><br>"
        "<label>Gateway host<br><input name=host value=\"%s\" required></label><br><br>"
        "<label>Gateway port<br><input name=port type=number value=\"%d\" "
        "min=1 max=65535 required></label><br><br>"
        "<button type=submit>Save &amp; restart</button>"
        "</form></body></html>",
        (error_msg && error_msg[0]) ? "<p style=\"color:red\">" : "",
        (error_msg && error_msg[0]) ? error_msg : "",
        (error_msg && error_msg[0]) ? "</p>" : "",
        ssid_esc, host_esc, cfg->server_port);
    if (n < 0 || (size_t)n >= buflen) return -1;
    return n;
}

int provisioning_render_saved(char *buf, size_t buflen) {
    int n = snprintf(buf, buflen,
        "<!doctype html><html><head><meta charset=utf-8></head>"
        "<body><h1>Saved. Restarting...</h1></body></html>");
    if (n < 0 || (size_t)n >= buflen) return -1;
    return n;
}
