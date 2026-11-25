#include <solis/apps/browser.h>
#include <solis/apps/browser_img.h>
#include <solis/apps/browser_js.h>
#include <solis/graphics.h>
#include <solis/gui.h>
#include <solis/net.h>
#include <solis/vfs.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * Solis Browser
 * -------------
 * A small renderer for HTTP, HTTPS and local HTML with three additions over
 * the original text-only version:
 *
 *   -- a DOM-lite element tree with inline colors, backgrounds, buttons,
 *      images and basic block flow;
 *   -- deferred BMP/PNG image loading with a small thumbnail cache;
 *   -- an integer-only JavaScript subset with document access and click
 *      handlers.
 *
 * It is still deliberately not a general-purpose web engine.  There is no
 * CSS layout, JPEG, iframe, cookie jar or network fetch API in script.
 */

#define BROWSER_W       1000
#define BROWSER_H       700
#define TOOLBAR_H       42
#define STATUS_H        18
#define PAGE_HEADER_H   34
#define LINE_H          16
#define CHAR_W          8
#define MAX_ADDRESS     320
#define MAX_URL         320
#define MAX_HISTORY     8
#define MAX_LOCAL_PATH  160
/* Large enough for a truncated Wikipedia render or GitHub search page, while
 * staying inside the 3 MiB SPX slot together with the image and DOM caches. */
#define RESPONSE_MAX    786432
#define START_SHORTCUTS 5
#define MAX_ANCHOR      96

#define DOM_MAX_NODES   6656
#define DOM_MAX_ATTRS   12544
#define DOM_MAX_TEXT    65024
#define DOM_MAX_STRINGS 65024

#define MAX_IMAGES      6
#define IMG_MAX_W       180
#define IMG_MAX_H       135

#define MAX_LAYOUT      2280
#define MAX_HITS        800
#define SEARCH_MAX      72
#define SEARCH_MAX_RESULTS 12
#define SEARCH_HTML_MAX 40000
#define CSS_MAX_RULES   256
#define CSS_MAX_SELECTOR 88
#define CSS_MAX_DECLS   320
#define CSS_TEXT_MAX    32768

struct browser_link {
    int start;
    int end;
    char href[MAX_URL];
};

struct browser_layout {
    int nav_y, nav_h;
    int back_x, back_w;
    int home_x, home_w;
    int open_x, open_w;
    int save_x, save_w;
    int settings_x, settings_w;
    int addr_x, addr_w;
    int go_x, go_w;
    int content_x, content_y, content_w, content_h;
    int page_y, page_h;
    int status_y;
};

struct start_layout {
    int logo_y;
    int search_x, search_y, search_w, search_h;
    int button_y, button_w, button_h;
    int button1_x, button2_x;
    int shortcut_y, shortcut_w, shortcut_h;
    int shortcut_x[START_SHORTCUTS];
};

enum dom_type { DOM_ELEMENT = 0, DOM_TEXT = 1 };

enum dom_tag {
    DOM_UNKNOWN = 0, DOM_HTML, DOM_HEAD, DOM_BODY, DOM_TITLE, DOM_META,
    DOM_LINK, DOM_SCRIPT, DOM_STYLE, DOM_DIV, DOM_P, DOM_H1, DOM_H2, DOM_H3,
    DOM_H4, DOM_H5, DOM_H6, DOM_A, DOM_IMG, DOM_BR, DOM_HR, DOM_UL,
    DOM_OL, DOM_LI, DOM_B, DOM_I, DOM_EM, DOM_STRONG, DOM_CODE, DOM_PRE,
    DOM_BLOCKQUOTE, DOM_BUTTON, DOM_SPAN, DOM_SECTION, DOM_ARTICLE,
    DOM_HEADER, DOM_FOOTER, DOM_NAV, DOM_ASIDE, DOM_TABLE, DOM_TR,
    DOM_TD, DOM_TH, DOM_FORM, DOM_INPUT, DOM_LABEL, DOM_SMALL,
    DOM_CENTER, DOM_MAIN, DOM_FIGURE, DOM_FIGCAPTION, DOM_COUNT
};

struct dom_attr {
    uint16_t name;
    uint16_t value;
};

struct dom_node {
    uint8_t type;
    uint8_t tag;
    uint8_t hidden;
    uint8_t bold;
    uint8_t align;
    uint8_t has_fg;
    uint8_t has_bg;
    uint8_t has_style;
    uint8_t display;           /* 0 auto, 1 block, 2 inline */
    uint8_t has_bold;
    uint8_t underline;
    uint8_t radius;
    uint8_t center;
    int16_t margin_top;
    int16_t margin_bottom;
    int16_t css_width;
    int16_t css_max_width;
    uint16_t padding;
    uint32_t border;
    uint16_t parent;
    uint16_t first;
    uint16_t last;
    uint16_t next;
    uint16_t text;
    uint16_t attr_first;
    uint8_t attr_count;
    uint32_t fg;
    uint32_t bg;
    int16_t image;
    int16_t override_text;
};

enum layout_kind {
    LAY_BG = 0, LAY_TEXT, LAY_RULE, LAY_IMAGE, LAY_BUTTON, LAY_BULLET,
    LAY_BAR, LAY_INPUT
};

#define LAY_F_BOLD      0x01
#define LAY_F_UNDERLINE 0x02
#define LAY_F_BG        0x04

struct layout_item {
    int16_t x, y, w, h;
    int16_t node;
    int16_t link_node;
    int16_t event_node;
    int16_t image;
    uint16_t tlen;
    const char *text;
    uint32_t fg;
    uint32_t bg;
    uint32_t border;
    uint8_t kind;
    uint8_t flags;
    uint8_t radius;
};

struct hit_rect {
    int16_t x, y, w, h;
    int16_t node;
    uint8_t kind;              /* 1 = link, 2 = script event */
};

struct layout_style {
    uint32_t fg;
    uint32_t bg;
    uint32_t border;
    uint8_t has_bg;
    uint8_t bold;
    uint8_t underline;
    uint8_t radius;
    uint8_t pre;
    uint8_t align;
    uint16_t indent;
    int16_t event_node;
    int16_t link_node;
};

struct css_rule {
    char selector[CSS_MAX_SELECTOR];
    char declarations[CSS_MAX_DECLS];
    uint8_t priority;
};

struct layout_ctx {
    int left, right;
    int x, y;
    int line_start;
    int line_x0, line_max;
    int indent;
    int align;
    int space_pending;
    int max_y;
};

struct page_image {
    int16_t node;
    uint8_t state;             /* 0 unused, 1 pending, 2 ready, 3 failed */
    int w, h;
    char url[MAX_URL];
};

static char address[MAX_ADDRESS];
static int address_len;
static int address_cursor;
static bool address_focus;
static bool address_select_all;
static int form_input_node = -1;
static char form_input[MAX_ADDRESS];
static int form_input_len;
static char pending_anchor[MAX_ANCHOR];
static bool anchor_pending;
static int site_theme;

enum { SITE_NONE = 0, SITE_WIKIPEDIA = 1, SITE_GITHUB = 2 };
enum { SEARCH_WEB = 0, SEARCH_WIKI, SEARCH_GITHUB };
enum { THEME_LIGHT = 0, THEME_DARK = 1 };
enum { DDG_OFF = 0, DDG_FALLBACK = 1, DDG_ADD = 2 };
/* Primary keyless web-search API. Wikipedia and GitHub always use their own
 * public APIs; this setting only affects ordinary web searches. */
enum { WEB_API_FREESERP = 0, WEB_API_DDG = 1, WEB_API_BING = 2 };

#define BROWSER_SETTINGS_PATH "/home/browser.cfg"
static int browser_theme = THEME_LIGHT;
static int duckduckgo_mode = DDG_FALLBACK;
static int web_search_api = WEB_API_FREESERP;
static bool browser_load_images = true;
static bool browser_js_enabled = true;

static char page_url[MAX_URL];
static char page_title[96];
static char page_bytes_status[96];
static int page_bytes;
static int scroll_y;
static int content_height;
static bool layout_dirty;
static int layout_width;
static int view_page_h = BROWSER_H - TOOLBAR_H - STATUS_H - 8 - PAGE_HEADER_H;
static bool page_loaded;
static bool onload_pending;
static int anim_phase;

static char status[160];
static bool status_error;
static char history[MAX_HISTORY][MAX_URL];
static int history_count;
static int history_pos;
static uint8_t response[RESPONSE_MAX + 1];
static int was_online;
static bool home_active;
static int script_errors;

static struct dom_node dom_nodes[DOM_MAX_NODES];
static int dom_node_used;
static struct dom_attr dom_attrs[DOM_MAX_ATTRS];
static int dom_attr_used;
static char dom_text_pool[DOM_MAX_TEXT];
static int dom_text_used;
static char dom_str_pool[DOM_MAX_STRINGS];
static int dom_str_used;
static int dom_root;
/* Parsed articles must leave room for the synthetic site header. */
static int dom_node_reserve;
static int dom_str_reserve;
static int dom_text_reserve;

static struct page_image images[MAX_IMAGES];
static uint32_t image_pixels[MAX_IMAGES][IMG_MAX_W * IMG_MAX_H];

static struct css_rule css_rules[CSS_MAX_RULES];
static int css_rule_count;

static struct layout_item layout_items[MAX_LAYOUT];
static int layout_item_count;
static struct hit_rect layout_hits[MAX_HITS];
static int layout_hit_count;
static char layout_text_scratch[4096];
static char dom_text_scratch[4096];
static char search_html[SEARCH_HTML_MAX];

struct search_result {
    char title[128];
    char url[MAX_URL];
    char domain[64];
    char snippet[240];
    char image[MAX_URL];
};
static struct search_result search_results[SEARCH_MAX_RESULTS];

static int html_entity(const char *s, int len, int *used, char out[4]);
static int dom_parse_color(const char *text, uint32_t *out);
static void dom_apply_style_prop(struct dom_node *node, const char *prop,
                                 const char *value);
static void css_apply_document(void);
static void request_anchor_from_url(const char *url);
static void apply_pending_anchor(int page_y);
static void browser_download_current(void);
static void browser_submit_form(int node);
static void html_escape(const char *src, char *out, int max);

/* ------------------------------------------------------------------ */
/* Small string helpers                                                */
/* ------------------------------------------------------------------ */

static int b_strlen(const char *s) {
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static void b_copy(char *dst, int max, const char *src) {
    int i = 0;
    if (!dst || max <= 0) return;
    if (src) {
        while (i < max - 1 && src[i]) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

static void b_append(char *dst, int max, const char *src) {
    int len = b_strlen(dst);
    if (len >= max - 1) return;
    b_copy(dst + len, max - len, src);
}

static bool b_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char b_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static bool b_starts_ci(const char *s, const char *prefix) {
    if (!s || !prefix) return false;
    while (*prefix) {
        if (b_lower(*s) != b_lower(*prefix)) return false;
        s++;
        prefix++;
    }
    return true;
}

static bool b_equal_ci(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (b_lower(*a) != b_lower(*b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}

static void b_trim_copy(char *dst, int max, const char *src) {
    if (!dst || max <= 0) return;
    if (!src) { dst[0] = '\0'; return; }
    int start = 0;
    while (src[start] && b_is_space(src[start])) start++;
    int end = b_strlen(src);
    while (end > start && b_is_space(src[end - 1])) end--;
    int n = end - start;
    if (n > max - 1) n = max - 1;
    for (int i = 0; i < n; i++) dst[i] = src[start + i];
    dst[n] = '\0';
}

static void b_append_int(char *buf, int max, int value) {
    char tmp[12];
    int n = 0;
    if (value <= 0) { b_append(buf, max, "0"); return; }
    while (value > 0 && n < 11) {
        tmp[n++] = (char)('0' + value % 10);
        value /= 10;
    }
    char out[12];
    int oi = 0;
    while (n > 0) out[oi++] = tmp[--n];
    out[oi] = '\0';
    b_append(buf, max, out);
}

static int b_find_ci(const char *hay, int start, int len, const char *needle) {
    int n = b_strlen(needle);
    if (n <= 0) return -1;
    for (int i = start; i + n <= len; i++) {
        int j = 0;
        while (j < n && b_lower(hay[i + j]) == b_lower(needle[j])) j++;
        if (j == n) return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* URLs                                                                */
/* ------------------------------------------------------------------ */

static int url_origin_len(const char *url) {
    int i = 0;
    if (!url) return 0;
    while (url[i] && url[i] != ':') i++;
    if (url[i] != ':') return 0;
    i++;
    if (url[i] != '/' || url[i + 1] != '/') return 0;
    i += 2;
    while (url[i] && url[i] != '/') i++;
    return i;
}

static void normalize_append_fragment(char *out, int max, const char *fragment) {
    if (!fragment || !fragment[0]) return;
    b_append(out, max, "#");
    b_append(out, max, fragment);
}

static bool normalize_url(const char *input, char *out, int max) {
    char tmp[MAX_URL];
    char full[MAX_URL];
    char fragment[MAX_ANCHOR];
    b_trim_copy(tmp, sizeof(tmp), input);
    fragment[0] = '\0';
    int len = b_strlen(tmp);
    if (len == 0) return false;

    for (int i = 0; i < len; i++) {
        if (tmp[i] == '#') {
            b_copy(fragment, sizeof(fragment), tmp + i + 1);
            tmp[i] = '\0';
            len = i;
            break;
        }
        if (b_is_space(tmp[i])) return false;
    }
    for (int i = 0; i < len; i++)
        if (b_is_space(tmp[i])) return false;
    if (len == 0) return false;

    if (b_starts_ci(tmp, "file://")) {
        b_copy(full, sizeof(full), tmp);
        if (b_strlen(full) <= 7) return false;
        b_copy(out, max, full);
        normalize_append_fragment(out, max, fragment);
        return true;
    }
    if (b_starts_ci(tmp, "http://") || b_starts_ci(tmp, "https://")) {
        b_copy(full, sizeof(full), tmp);
    } else if (tmp[0] == '/') {
        b_copy(full, sizeof(full), "file://");
        b_append(full, sizeof(full), tmp);
        b_copy(out, max, full);
        normalize_append_fragment(out, max, fragment);
        return true;
    } else {
        for (int i = 0; tmp[i]; i++)
            if (tmp[i] == ':' && tmp[i + 1] == '/' && tmp[i + 2] == '/')
                return false;
        if (tmp[0] == '?') return false;
        b_copy(full, sizeof(full), "http://");
        b_append(full, sizeof(full), tmp);
    }

    int scheme_end = b_starts_ci(full, "https://") ? 8 : 7;
    int i = scheme_end;
    while (full[i] && full[i] != '/') i++;
    if (i == scheme_end) return false;
    if (!full[i]) b_append(full, sizeof(full), "/");
    b_copy(out, max, full);
    normalize_append_fragment(out, max, fragment);
    return true;
}

static bool parse_file_url(const char *url, char *out, int max) {
    if (!url || !b_starts_ci(url, "file://")) return false;
    const char *p = url + 7;
    if (!*p) return false;
    int n = 0;
    if (*p != '/') {
        if (n < max - 1) out[n++] = '/';
    }
    while (*p && *p != '?' && *p != '#') {
        if (n >= max - 1) return false;
        out[n++] = *p++;
    }
    out[n] = '\0';
    return n > 0;
}

static bool parse_url(const char *url, char *host, int host_max,
                      uint16_t *port, char *path, int path_max,
                      bool *is_https) {
    const char *p = url;
    *is_https = false;
    if (b_starts_ci(p, "http://")) p += 7;
    else if (b_starts_ci(p, "https://")) { *is_https = true; p += 8; }
    else return false;

    int hi = 0;
    while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#') {
        if (hi >= host_max - 1) return false;
        host[hi++] = *p++;
    }
    host[hi] = '\0';
    if (hi == 0) return false;

    int pt = *is_https ? 443 : 80;
    if (*p == ':') {
        p++;
        int value = 0;
        bool any = false;
        while (*p >= '0' && *p <= '9') {
            value = value * 10 + (*p - '0');
            p++;
            any = true;
            if (value > 65535) return false;
        }
        if (!any || value == 0) return false;
        pt = value;
    }

    char tmp[512];
    int pi = 0;
    if (*p == '/') {
        while (*p && *p != '#' && pi < (int)sizeof(tmp) - 1)
            tmp[pi++] = *p++;
    } else if (*p == '?') {
        if (pi < (int)sizeof(tmp) - 1) tmp[pi++] = '/';
        while (*p && *p != '#' && pi < (int)sizeof(tmp) - 1)
            tmp[pi++] = *p++;
    } else {
        if (pi < (int)sizeof(tmp) - 1) tmp[pi++] = '/';
    }
    tmp[pi] = '\0';
    if (pi >= path_max - 1) pi = path_max - 1;
    tmp[pi] = '\0';
    b_copy(path, path_max, tmp);
    *port = (uint16_t)pt;
    return true;
}

static bool resolve_href(const char *href, char *out, int max) {
    char h[MAX_URL];
    char base_page[MAX_URL];
    b_trim_copy(h, sizeof(h), href);
    b_copy(base_page, sizeof(base_page), page_url);
    for (int i = 0; base_page[i]; i++)
        if (base_page[i] == '#') { base_page[i] = '\0'; break; }
    if (!h[0] || h[0] == '#') return false;

    if (b_starts_ci(h, "http://") || b_starts_ci(h, "https://") ||
        b_starts_ci(h, "file://") || b_starts_ci(h, "solis://")) {
        b_copy(out, max, h);
        return true;
    }
    if (b_starts_ci(base_page, "file://")) {
        char base[MAX_LOCAL_PATH];
        if (!parse_file_url(base_page, base, sizeof(base))) return false;
        if (h[0] == '?') return false;

        char path[MAX_LOCAL_PATH];
        int pi = 0;
        if (h[0] == '/') {
            while (h[pi] && h[pi] != '?' && h[pi] != '#' &&
                   pi < (int)sizeof(path) - 1) {
                path[pi] = h[pi];
                pi++;
            }
        } else {
            int dir = 0;
            for (int i = 0; base[i]; i++)
                if (base[i] == '/') dir = i + 1;
            for (int i = 0; i < dir && pi < (int)sizeof(path) - 1; i++)
                path[pi++] = base[i];
            for (int i = 0; h[i] && h[i] != '?' && h[i] != '#' &&
                            pi < (int)sizeof(path) - 1; i++)
                path[pi++] = h[i];
        }
        path[pi] = '\0';
        if (!path[0]) return false;
        b_copy(out, max, "file://");
        b_append(out, max, path);
        return true;
    }
    if (h[0] == '/' && h[1] == '/') {
        b_copy(out, max, b_starts_ci(base_page, "https://") ? "https:" : "http:");
        b_append(out, max, h);
        return true;
    }
    if (!base_page[0]) return false;

    int origin = url_origin_len(base_page);
    if (origin <= 0) return false;

    if (h[0] == '/') {
        char prefix[MAX_URL];
        int oi = 0;
        while (oi < origin && oi < (int)sizeof(prefix) - 1) {
            prefix[oi] = base_page[oi];
            oi++;
        }
        prefix[oi] = '\0';
        b_copy(out, max, prefix);
        b_append(out, max, h);
        return true;
    }
    if (h[0] == '?') {
        int cut = 0;
        while (base_page[cut] && base_page[cut] != '?') cut++;
        char prefix[MAX_URL];
        int oi = 0;
        while (oi < cut && oi < (int)sizeof(prefix) - 1) {
            prefix[oi] = base_page[oi];
            oi++;
        }
        prefix[oi] = '\0';
        b_copy(out, max, prefix);
        b_append(out, max, h);
        return true;
    }

    int last = origin;
    for (int i = origin; base_page[i]; i++)
        if (base_page[i] == '/') last = i;
    char prefix[MAX_URL];
    int copy_len = last;
    if (base_page[last] == '/') copy_len = last + 1;
    int oi = 0;
    while (oi < copy_len && oi < (int)sizeof(prefix) - 1) {
        prefix[oi] = base_page[oi];
        oi++;
    }
    prefix[oi] = '\0';
    b_copy(out, max, prefix);
    if (base_page[last] != '/') b_append(out, max, "/");
    b_append(out, max, h);
    return true;
}

static bool parse_ipv4(const char *s, uint8_t out[4]) {
    int part = 0;
    int value = 0;
    bool digits = false;
    for (int i = 0;; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            if (value > 255) return false;
            digits = true;
        } else if (c == '.' || c == '\0') {
            if (!digits || part >= 4) return false;
            out[part++] = (uint8_t)value;
            value = 0;
            digits = false;
            if (c == '\0') break;
        } else {
            return false;
        }
    }
    return part == 4;
}

/* ------------------------------------------------------------------ */
/* DOM-lite document                                                   */
/* ------------------------------------------------------------------ */

static const char *const dom_tag_names[DOM_COUNT] = {
    "?", "html", "head", "body", "title", "meta", "link", "script",
    "style", "div", "p", "h1", "h2", "h3", "h4", "h5", "h6", "a", "img",
    "br", "hr", "ul", "ol", "li", "b", "i", "em", "strong", "code",
    "pre", "blockquote", "button", "span", "section", "article",
    "header", "footer", "nav", "aside", "table", "tr", "td", "th",
    "form", "input", "label", "small", "center", "main", "figure",
    "figcaption"
};

static int b_name_is(const char *text, int start, int len, const char *name) {
    int n = b_strlen(name);
    if (len != n) return 0;
    for (int i = 0; i < n; i++) {
        if (b_lower(text[start + i]) != b_lower(name[i])) return 0;
    }
    return 1;
}

/* UTF-8 helpers for the app side. The kernel text layer owns drawing and
 * measurement; the parser only needs to decode sequences it must split at
 * codepoint boundaries, so a tiny local copy avoids a new syscall. */
static int b_utf8_size(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static int b_utf8_decode(const char *s, uint32_t *cp) {
    const unsigned char *p = (const unsigned char *)s;
    int n = b_utf8_size(p[0]);
    if (n == 1) { *cp = p[0]; return 1; }
    uint32_t v = p[0] & (0xFF >> (n + 1));
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
        v = (v << 6) | (p[i] & 0x3F);
    }
    *cp = v;
    return n;
}

static int b_utf8_encode(uint32_t cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static int b_cp_width(uint32_t cp) {
    if (cp >= 0x1100 && (cp <= 0x115F || cp == 0x2329 || cp == 0x232A ||
        (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) ||
        (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE30 && cp <= 0xFE6F) ||
        (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x1F300 && cp <= 0x1FAFF)))
        return 16;
    if (cp >= 0x300 && cp <= 0x36F) return 0;
    return 8;
}

static int b_text_width(const char *s) {
    return graphics_text_width(s);
}

/* Width of a slice that is not NUL-terminated at len. */
static int b_width_n(const char *s, int len) {
    int width = 0;
    int i = 0;
    while (i < len) {
        uint32_t cp;
        int used = b_utf8_decode(s + i, &cp);
        if (used <= 0) break;
        if (used > len - i) used = len - i;
        width += b_cp_width(cp);
        i += used;
    }
    return width;
}

/* Back up to the start of a UTF-8 sequence, so a byte-counted truncation
 * never leaves half a character at the end of a buffer. */
static int b_utf8_trim(const char *s, int len) {
    if (len <= 0) return 0;
    int back = 0;
    while (back < len && back < 3 &&
           ((unsigned char)s[len - 1 - back] & 0xC0) == 0x80)
        back++;
    int lead = len - 1 - back;
    if (lead < 0) return 0;
    int need = b_utf8_size((unsigned char)s[lead]);
    if (need < 1) need = 1;
    if (back + 1 < need) return lead;   /* drop a half-written sequence */
    return len;
}

/* Named and numeric entities decode to UTF-8, so accents, curly quotes,
 * dashes and symbols survive into the layout instead of collapsing to '?'.
 * out must hold 4 bytes; returns the byte count (0 when not an entity). */
struct html_entity_name { const char *name; uint32_t cp; };

static const struct html_entity_name html_entity_names[] = {
    {"amp", '&'},      {"lt", '<'},        {"gt", '>'},
    {"quot", '"'},     {"apos", '\''},      {"nbsp", 0x00A0},
    {"copy", 0x00A9},   {"reg", 0x00AE},    {"trade", 0x2122},
    {"deg", 0x00B0},    {"plusmn", 0x00B1}, {"times", 0x00D7},
    {"divide", 0x00F7}, {"middot", 0x00B7}, {"bull", 0x2022},
    {"hellip", 0x2026}, {"mdash", 0x2014},  {"ndash", 0x2013},
    {"lsquo", 0x2018},  {"rsquo", 0x2019},  {"ldquo", 0x201C},
    {"rdquo", 0x201D},   {"laquo", 0x00AB},  {"raquo", 0x00BB},
    {"euro", 0x20AC},   {"pound", 0x00A3},  {"yen", 0x00A5},
    {"cent", 0x00A2},   {"sect", 0x00A7},   {"para", 0x00B6},
    {"dagger", 0x2020}, {"permil", 0x2030}, {"frac12", 0x00BD},
    {"frac14", 0x00BC}, {"frac34", 0x00BE}, {"sup2", 0x00B2},
    {"sup3", 0x00B3},   {"micro", 0x00B5},  {"alpha", 0x03B1},
    {"beta", 0x03B2},   {"gamma", 0x03B3},  {"delta", 0x03B4},
    {"pi", 0x03C0},     {"sigma", 0x03C3},  {"omega", 0x03C9},
    {"theta", 0x03B8},  {"lambda", 0x03BB}, {"mu", 0x03BC},
    {"eacute", 0x00E9}, {"egrave", 0x00E8}, {"agrave", 0x00E0},
    {"aacute", 0x00E1}, {"ccedil", 0x00E7}, {"uuml", 0x00FC},
    {"ouml", 0x00F6},   {"auml", 0x00E4},   {"ntilde", 0x00F1},
    {"szlig", 0x00DF},  {"Auml", 0x00C4},   {"Ouml", 0x00D6},
    {"Uuml", 0x00DC},   {"Eacute", 0x00C9}, {"larr", 0x2190},
    {"rarr", 0x2192},   {"harr", 0x2194},   {"ne", 0x2260},
    {"le", 0x2264},     {"ge", 0x2265},     {"infin", 0x221E},
    {"minus", 0x2212},  {"check", 0x2713},  {"star", 0x2605},
};

/* HTML entity names are case-sensitive (`&Auml;` is not `&auml;`), while
 * b_name_is folds case for tag comparisons. */
static int b_name_is_exact(const char *s, int start, int n, const char *name) {
    int i = 0;
    for (; i < n && name[i]; i++)
        if (s[start + i] != name[i]) return 0;
    return i == n && name[i] == '\0';
}

static int html_entity(const char *s, int len, int *used, char out[4]) {
    *used = 0;
    if (len < 3 || s[0] != '&') return 0;
    int semi = -1;
    for (int i = 1; i < len && i < 16; i++) {
        if (s[i] == ';') { semi = i; break; }
        if (!((s[i] >= 'a' && s[i] <= 'z') ||
              (s[i] >= 'A' && s[i] <= 'Z') ||
              (s[i] >= '0' && s[i] <= '9') ||
              s[i] == '#' || s[i] == 'x' || s[i] == 'X'))
            return 0;
    }
    if (semi < 0) return 0;
    int n = semi - 1;
    *used = semi + 1;

    if (s[1] == '#') {
        int i = 2;
        int base = 10;
        if (i < semi && (s[i] == 'x' || s[i] == 'X')) { base = 16; i++; }
        if (i >= semi) { *used = 0; return 0; }
        uint32_t value = 0;
        for (; i < semi; i++) {
            int d;
            char c = s[i];
            if (c >= '0' && c <= '9') d = c - '0';
            else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else { *used = 0; return 0; }
            value = value * (uint32_t)base + (uint32_t)d;
            if (value > 0x10FFFF) { *used = 0; return 0; }
        }
        if (value == 160) value = ' ';
        int en = b_utf8_encode(value, out);
        if (en == 0) { out[0] = '?'; en = 1; }
        return en;
    }

    for (unsigned i = 0; i < sizeof(html_entity_names) / sizeof(html_entity_names[0]); i++) {
        if (b_name_is_exact(s, 1, n, html_entity_names[i].name)) {
            uint32_t cp = html_entity_names[i].cp;
            if (cp == 0x00A0) cp = ' ';
            return b_utf8_encode(cp, out);
        }
    }
    if (n > 0) { out[0] = ' '; return 1; }
    return 0;
}

static int dom_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int dom_parse_color(const char *text, uint32_t *out) {
    while (text && b_is_space(*text)) text++;
    if (!text || !*text) return 0;
    if (*text == '#') {
        text++;
        int len = b_strlen(text);
        if (len == 3) {
            int r = dom_hex(text[0]), g = dom_hex(text[1]), b = dom_hex(text[2]);
            if (r < 0 || g < 0 || b < 0) return 0;
            *out = 0xFF000000U | ((uint32_t)(r * 17) << 16) |
                   ((uint32_t)(g * 17) << 8) | (uint32_t)(b * 17);
            return 1;
        }
        if (len >= 6) {
            int r = (dom_hex(text[0]) << 4) | dom_hex(text[1]);
            int g = (dom_hex(text[2]) << 4) | dom_hex(text[3]);
            int b = (dom_hex(text[4]) << 4) | dom_hex(text[5]);
            if (r < 0 || g < 0 || b < 0) return 0;
            *out = 0xFF000000U | ((uint32_t)r << 16) |
                   ((uint32_t)g << 8) | (uint32_t)b;
            return 1;
        }
        return 0;
    }
    if (b_equal_ci(text, "white")) { *out = 0xFFFFFFFF; return 1; }
    if (b_equal_ci(text, "black")) { *out = 0xFF000000; return 1; }
    if (b_equal_ci(text, "red")) { *out = 0xFFEA4335; return 1; }
    if (b_equal_ci(text, "green")) { *out = 0xFF34A853; return 1; }
    if (b_equal_ci(text, "blue")) { *out = 0xFF4285F4; return 1; }
    if (b_equal_ci(text, "yellow")) { *out = 0xFFFBBC05; return 1; }
    if (b_equal_ci(text, "orange")) { *out = 0xFFF28C28; return 1; }
    if (b_equal_ci(text, "purple")) { *out = 0xFF8E44AD; return 1; }
    if (b_equal_ci(text, "teal")) { *out = 0xFF00897B; return 1; }
    if (b_equal_ci(text, "gray") || b_equal_ci(text, "grey")) {
        *out = 0xFF6B7280; return 1;
    }
    if (b_equal_ci(text, "lightgray") || b_equal_ci(text, "lightgrey")) {
        *out = 0xFFE5E7EB; return 1;
    }
    return 0;
}

static int dom_css_length(const char *value, int fallback) {
    if (!value) return fallback;
    while (*value && b_is_space(*value)) value++;
    int sign = 1;
    if (*value == '-') { sign = -1; value++; }
    int number = 0;
    int digits = 0;
    while (*value >= '0' && *value <= '9') {
        number = number * 10 + (*value - '0');
        value++;
        digits = 1;
        if (number > 1200) number = 1200;
    }
    if (!digits) return fallback;
    number *= sign;
    if (number < -64) number = -64;
    if (number > 1200) number = 1200;
    return number;
}

static uint32_t dom_css_color(const char *value) {
    uint32_t color;
    if (!value) return 0;
    while (*value && b_is_space(*value)) value++;
    if (dom_parse_color(value, &color)) return color;
    /* The final token in border/background shorthands is usually a color. */
    const char *last = value;
    for (const char *p = value; p && *p; p++)
        if (b_is_space(*p)) last = p + 1;
    return dom_parse_color(last, &color) ? color : 0;
}

static void dom_apply_style_prop(struct dom_node *node, const char *prop,
                                 const char *value) {
    uint32_t color;
    if (b_equal_ci(prop, "color")) {
        if (dom_parse_color(value, &color)) { node->has_fg = 1; node->fg = color; }
    } else if (b_equal_ci(prop, "background") ||
               b_equal_ci(prop, "background-color")) {
        if (dom_parse_color(value, &color)) { node->has_bg = 1; node->bg = color; }
    } else if (b_equal_ci(prop, "font-weight")) {
        node->has_bold = 1;
        node->bold = (b_equal_ci(value, "bold") || b_equal_ci(value, "700") ||
                      b_equal_ci(value, "800") || b_equal_ci(value, "900"));
    } else if (b_equal_ci(prop, "display")) {
        if (b_equal_ci(value, "none")) { node->display = 3; node->hidden = 1; }
        else if (b_equal_ci(value, "block") || b_equal_ci(value, "flex") ||
                 b_equal_ci(value, "inline-flex") || b_equal_ci(value, "grid"))
            { node->display = 1; node->hidden = 0; }
        else if (b_equal_ci(value, "inline") || b_equal_ci(value, "inline-block"))
            { node->display = 2; node->hidden = 0; }
    } else if (b_equal_ci(prop, "visibility")) {
        if (b_equal_ci(value, "hidden") || b_equal_ci(value, "collapse"))
            node->hidden = 1;
        else if (b_equal_ci(value, "visible")) node->hidden = 0;
    } else if (b_equal_ci(prop, "width") || b_equal_ci(prop, "max-width")) {
        /* Percentages need a containing block; the small engine only applies
         * absolute pixel widths. This still handles reader columns and cards. */
        if (b_find_ci(value, 0, b_strlen(value), "%") < 0) {
            int w = dom_css_length(value, 0);
            if (w < 0) w = 0;
            if (b_equal_ci(prop, "width")) node->css_width = (int16_t)w;
            else node->css_max_width = (int16_t)w;
        }
    } else if (b_equal_ci(prop, "text-align")) {
        if (b_equal_ci(value, "center")) node->align = 1;
        else if (b_equal_ci(value, "right")) node->align = 2;
        else if (b_equal_ci(value, "left")) node->align = 0;
    } else if (b_equal_ci(prop, "text-decoration")) {
        node->underline = b_find_ci(value, 0, b_strlen(value), "underline") >= 0;
    } else if (b_equal_ci(prop, "margin") || b_equal_ci(prop, "margin-top")) {
        int m = dom_css_length(value, 0);
        node->margin_top = (int16_t)m;
        if (b_equal_ci(prop, "margin")) {
            node->margin_bottom = (int16_t)m;
            if (b_find_ci(value, 0, b_strlen(value), "auto") >= 0)
                node->center = 1;
        }
    } else if (b_equal_ci(prop, "margin-bottom")) {
        node->margin_bottom = (int16_t)dom_css_length(value, 0);
    } else if (b_equal_ci(prop, "padding") || b_equal_ci(prop, "padding-left")) {
        int p = dom_css_length(value, 0);
        if (p < 0) p = 0;
        if (p > 96) p = 96;
        node->padding = (uint16_t)p;
    } else if (b_equal_ci(prop, "border") || b_equal_ci(prop, "border-top") ||
               b_equal_ci(prop, "border-bottom") || b_equal_ci(prop, "border-left") ||
               b_equal_ci(prop, "border-right")) {
        if (!b_find_ci(value, 0, b_strlen(value), "none"))
            node->border = dom_css_color(value);
        else
            node->border = 0;
    } else if (b_equal_ci(prop, "border-radius")) {
        int r = dom_css_length(value, 0);
        if (r < 0) r = 0;
        if (r > 24) r = 24;
        node->radius = (uint8_t)r;
    }
}

static int dom_tag_from_name(const char *name, int len) {
    for (int t = 1; t < DOM_COUNT; t++) {
        int n = b_strlen(dom_tag_names[t]);
        if (n != len) continue;
        int ok = 1;
        for (int i = 0; i < n; i++) {
            if (b_lower(name[i]) != dom_tag_names[t][i]) { ok = 0; break; }
        }
        if (ok) return t;
    }
    return DOM_UNKNOWN;
}

static int dom_str_store(const char *src, int len) {
    if (!src) len = 0;
    if (dom_str_used + len + 1 > DOM_MAX_STRINGS - dom_str_reserve) return 0;
    int off = dom_str_used;
    for (int i = 0; i < len; i++) dom_str_pool[dom_str_used + i] = src[i];
    dom_str_pool[dom_str_used + len] = '\0';
    dom_str_used += len + 1;
    return off;
}

static int dom_text_store(const char *src, int len) {
    if (!src) len = 0;
    if (dom_text_used + len + 1 > DOM_MAX_TEXT - dom_text_reserve) return 0;
    int off = dom_text_used;
    for (int i = 0; i < len; i++) dom_text_pool[dom_text_used + i] = src[i];
    dom_text_pool[dom_text_used + len] = '\0';
    dom_text_used += len + 1;
    return off;
}

static const char *dom_str(int off) {
    if (off < 0 || off >= dom_str_used) return "";
    return dom_str_pool + off;
}

static const char *dom_text(int off) {
    if (off < 0 || off >= dom_text_used) return "";
    return dom_text_pool + off;
}

static int dom_new_node(int type, int tag, int parent) {
    if (dom_node_used >= DOM_MAX_NODES - dom_node_reserve) return -1;
    int id = dom_node_used++;
    struct dom_node *n = &dom_nodes[id];
    n->type = (uint8_t)type;
    n->tag = (uint8_t)tag;
    n->hidden = 0;
    n->bold = 0;
    n->align = 0;
    n->has_fg = 0;
    n->has_bg = 0;
    n->has_style = 0;
    n->display = 0;
    n->has_bold = 0;
    n->underline = 0;
    n->radius = 0;
    n->center = 0;
    n->margin_top = -1;
    n->margin_bottom = -1;
    n->css_width = 0;
    n->css_max_width = 0;
    n->padding = 0;
    n->border = 0;
    n->parent = (uint16_t)(parent < 0 ? 0 : parent);
    n->first = 0;
    n->last = 0;
    n->next = 0;
    n->text = 0;
    n->attr_first = 0;
    n->attr_count = 0;
    n->fg = 0xFF20242B;
    n->bg = 0xFFFFFFFF;
    n->image = -1;
    n->override_text = -1;

    if (parent >= 0 && parent < dom_node_used && parent != id) {
        struct dom_node *p = &dom_nodes[parent];
        n->parent = (uint16_t)parent;
        if (p->last) dom_nodes[p->last].next = (uint16_t)id;
        else p->first = (uint16_t)id;
        p->last = (uint16_t)id;
    }
    return id;
}

static void dom_reset(void) {
    dom_node_used = 0;
    dom_attr_used = 0;
    dom_text_used = 0;
    dom_str_used = 0;
    dom_text_pool[0] = '\0';
    dom_text_used = 1;
    dom_str_pool[0] = '\0';
    dom_str_used = 1;
    dom_root = dom_new_node(DOM_ELEMENT, DOM_HTML, 0);
}

static int dom_find_body_node(void) {
    for (int i = 0; i < dom_node_used; i++) {
        if (dom_nodes[i].type == DOM_ELEMENT && dom_nodes[i].tag == DOM_BODY)
            return i;
    }
    return -1;
}

static int dom_body_node(void) {
    int body = dom_find_body_node();
    return body >= 0 ? body : dom_root;
}

static int dom_attr_index(int node, const char *name) {
    if (node < 0 || node >= dom_node_used) return -1;
    struct dom_node *n = &dom_nodes[node];
    for (int i = 0; i < n->attr_count; i++) {
        const char *an = dom_str(dom_attrs[n->attr_first + i].name);
        if (b_equal_ci(an, name)) return n->attr_first + i;
    }
    return -1;
}

static const char *dom_get_attr(int node, const char *name) {
    int idx = dom_attr_index(node, name);
    if (idx < 0) return "";
    return dom_str(dom_attrs[idx].value);
}

static int dom_attr_int(int node, const char *name) {
    const char *value = dom_get_attr(node, name);
    int i = 0;
    int n = 0;
    while (value[i] >= '0' && value[i] <= '9') {
        n = n * 10 + (value[i] - '0');
        if (n > 4096) return 4096;
        i++;
    }
    return n;
}

static const char *dom_tag_name(int node);

static void dom_apply_declarations(int node, const char *style) {
    if (node < 0 || node >= dom_node_used || !style) return;
    struct dom_node *n = &dom_nodes[node];
    int i = 0;
    while (style[i]) {
        while (style[i] == ';' || b_is_space(style[i])) i++;
        char prop[32];
        char value[96];
        int pn = 0;
        while (style[i] && style[i] != ':' && style[i] != ';' && pn < 31)
            prop[pn++] = b_lower(style[i++]);
        prop[pn] = '\0';
        while (b_is_space(style[i])) i++;
        if (style[i] != ':') {
            while (style[i] && style[i] != ';') i++;
            continue;
        }
        i++;
        while (b_is_space(style[i])) i++;
        int vn = 0;
        while (style[i] && style[i] != ';' && vn < 95)
            value[vn++] = style[i++];
        value[vn] = '\0';
        while (vn > 0 && b_is_space(value[vn - 1])) value[--vn] = '\0';
        dom_apply_style_prop(n, prop, value);
        while (style[i] && style[i] != ';') i++;
    }
}

static void dom_apply_style(int node) {
    if (node < 0 || node >= dom_node_used) return;
    struct dom_node *n = &dom_nodes[node];
    const char *style = dom_get_attr(node, "style");
    const char *bgcolor = dom_get_attr(node, "bgcolor");
    const char *color = dom_get_attr(node, "color");
    const char *align = dom_get_attr(node, "align");
    if (bgcolor[0]) {
        uint32_t c;
        if (dom_parse_color(bgcolor, &c)) { n->has_bg = 1; n->bg = c; }
    }
    if (color[0]) {
        uint32_t c;
        if (dom_parse_color(color, &c)) { n->has_fg = 1; n->fg = c; }
    }
    if (b_equal_ci(align, "center")) n->align = 1;
    else if (b_equal_ci(align, "right")) n->align = 2;
    dom_apply_declarations(node, style);
}

static void dom_attr_put(int node, const char *name, const char *value) {
    if (node < 0 || node >= dom_node_used || !name) return;
    struct dom_node *n = &dom_nodes[node];
    int idx = dom_attr_index(node, name);
    if (idx >= 0) {
        dom_attrs[idx].value = (uint16_t)dom_str_store(value, b_strlen(value));
        return;
    }
    if (n->attr_count == 0) n->attr_first = (uint16_t)dom_attr_used;
    if (dom_attr_used >= DOM_MAX_ATTRS || n->attr_count >= 16) return;
    int at = dom_attr_used++;
    dom_attrs[at].name = (uint16_t)dom_str_store(name, b_strlen(name));
    dom_attrs[at].value = (uint16_t)dom_str_store(value, b_strlen(value));
    n->attr_count++;
}

static int dom_add_text(int parent, const char *src, int len, int decode) {
    if (parent < 0 || len <= 0) return -1;
    int off = dom_text_used;
    int at = off;
    for (int i = 0; i < len; i++) {
        char c = src[i];
        if (decode && c == '&') {
            int used = 0;
            char e[4];
            int en = html_entity(src + i, len - i, &used, e);
            if (used > 0) {
                for (int k = 0; k < en; k++)
                    if (at < DOM_MAX_TEXT - 1 - dom_text_reserve)
                        dom_text_pool[at++] = e[k];
                i += used - 1;
                continue;
            }
        }
        if (at < DOM_MAX_TEXT - 1 - dom_text_reserve) dom_text_pool[at++] = c;
    }
    if (at <= off) return -1;
    dom_text_pool[at] = '\0';
    dom_text_used = at + 1;
    int node = dom_new_node(DOM_TEXT, 0, parent);
    if (node >= 0) dom_nodes[node].text = (uint16_t)off;
    return node;
}

static void dom_clear_children(int node) {
    if (node < 0 || node >= dom_node_used) return;
    dom_nodes[node].first = 0;
    dom_nodes[node].last = 0;
}

static void dom_collect_text(int node, char *out, int max, int *pos) {
    if (node < 0 || node >= dom_node_used || *pos >= max - 1) return;
    struct dom_node *n = &dom_nodes[node];
    if (n->type == DOM_TEXT) {
        const char *text = dom_text(n->text);
        while (*text && *pos < max - 1) out[(*pos)++] = *text++;
        return;
    }
    if (n->tag == DOM_SCRIPT || n->tag == DOM_STYLE || n->tag == DOM_TITLE)
        return;
    if (n->override_text >= 0) {
        const char *text = dom_text(n->override_text);
        while (*text && *pos < max - 1) out[(*pos)++] = *text++;
        return;
    }
    for (int c = n->first; c; c = dom_nodes[c].next)
        dom_collect_text(c, out, max, pos);
}

static const char *dom_node_text(int node) {
    if (node < 0 || node >= dom_node_used) return "";
    struct dom_node *n = &dom_nodes[node];
    if (n->type == DOM_TEXT) return dom_text(n->text);
    if (n->override_text >= 0) return dom_text(n->override_text);
    if (n->tag == DOM_SCRIPT || n->tag == DOM_STYLE || n->tag == DOM_TITLE) {
        if (n->first && dom_nodes[n->first].type == DOM_TEXT)
            return dom_text(dom_nodes[n->first].text);
        return "";
    }
    dom_text_scratch[0] = '\0';
    int pos = 0;
    dom_collect_text(node, dom_text_scratch, sizeof(dom_text_scratch), &pos);
    dom_text_scratch[pos] = '\0';
    return dom_text_scratch;
}

static int dom_find_id(const char *id) {
    if (!id || !id[0]) return -1;
    for (int i = 0; i < dom_node_used; i++) {
        if (dom_nodes[i].type != DOM_ELEMENT) continue;
        if (b_equal_ci(dom_get_attr(i, "id"), id)) return i;
    }
    return -1;
}

static int dom_selector_match(int node, const char *selector) {
    if (!selector || !selector[0]) return 0;
    if (selector[0] == '#') return b_equal_ci(dom_get_attr(node, "id"), selector + 1);
    if (selector[0] == '.') {
        char classes[128];
        b_copy(classes, sizeof(classes), dom_get_attr(node, "class"));
        int i = 0;
        while (classes[i]) {
            while (classes[i] && b_is_space(classes[i])) i++;
            int start = i;
            while (classes[i] && !b_is_space(classes[i])) i++;
            int n = i - start;
            int m = b_strlen(selector + 1);
            if (n == m) {
                int same = 1;
                for (int j = 0; j < n; j++)
                    if (classes[start + j] != selector[1 + j]) { same = 0; break; }
                if (same) return 1;
            }
        }
        return 0;
    }
    return b_equal_ci(dom_tag_name(node), selector);
}

static int dom_find_selector(const char *selector) {
    if (!selector || !selector[0]) return -1;
    for (int i = 0; i < dom_node_used; i++) {
        if (dom_nodes[i].type != DOM_ELEMENT) continue;
        if (dom_selector_match(i, selector)) return i;
    }
    return -1;
}

static const char *dom_tag_name(int node) {
    static char name[16];
    if (node < 0 || node >= dom_node_used) return "";
    int tag = dom_nodes[node].tag;
    if (tag < 0 || tag >= DOM_COUNT) tag = DOM_UNKNOWN;
    b_copy(name, sizeof(name), dom_tag_names[tag]);
    for (int i = 0; name[i]; i++) name[i] = b_lower(name[i]);
    return name;
}

static int dom_find_title(void) {
    for (int i = 0; i < dom_node_used; i++) {
        if (dom_nodes[i].type == DOM_ELEMENT && dom_nodes[i].tag == DOM_TITLE)
            return i;
    }
    return -1;
}

static int html_tag_end(const char *html, int start, int len) {
    int quote = 0;
    for (int i = start; i < len; i++) {
        char c = html[i];
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '>') {
            return i;
        }
    }
    return -1;
}

static int dom_is_void(int tag) {
    return tag == DOM_IMG || tag == DOM_BR || tag == DOM_HR ||
           tag == DOM_META || tag == DOM_LINK || tag == DOM_INPUT;
}

static int dom_is_block(int tag) {
    return tag == DOM_DIV || tag == DOM_P || tag == DOM_H1 || tag == DOM_H2 ||
           tag == DOM_H3 || tag == DOM_H4 || tag == DOM_H5 || tag == DOM_H6 ||
           tag == DOM_UL || tag == DOM_OL || tag == DOM_TABLE || tag == DOM_TR ||
           tag == DOM_SECTION || tag == DOM_ARTICLE || tag == DOM_HEADER ||
           tag == DOM_FOOTER || tag == DOM_NAV || tag == DOM_ASIDE ||
           tag == DOM_BLOCKQUOTE || tag == DOM_PRE || tag == DOM_FORM ||
           tag == DOM_MAIN || tag == DOM_FIGURE || tag == DOM_FIGCAPTION ||
           tag == DOM_HR;
}

static void dom_parse_attrs(int node, const char *html, int start, int end) {
    int i = start;
    while (i < end) {
        while (i < end && b_is_space(html[i])) i++;
        if (i >= end) break;
        if (html[i] == '/' || html[i] == '>') { i++; continue; }
        int ns = i;
        while (i < end && html[i] != '=' && !b_is_space(html[i]) &&
               html[i] != '>' && html[i] != '/') i++;
        int nl = i - ns;
        if (nl <= 0) { i++; continue; }
        while (i < end && b_is_space(html[i])) i++;
        char name[32];
        int nn = nl < 31 ? nl : 31;
        for (int j = 0; j < nn; j++) name[j] = b_lower(html[ns + j]);
        name[nn] = '\0';
        while (i < end && b_is_space(html[i])) i++;
        int vs = i;
        int ve = i;
        if (i < end && html[i] == '=') {
            i++;
            while (i < end && b_is_space(html[i])) i++;
            int quote = 0;
            if (i < end && (html[i] == '"' || html[i] == '\'')) quote = html[i++];
            vs = i;
            if (quote) {
                while (i < end && html[i] != quote) i++;
                ve = i;
                if (i < end) i++;
            } else {
                while (i < end && !b_is_space(html[i]) && html[i] != '>') i++;
                ve = i;
            }
        } else {
            vs = ve = i;
        }
        char value[MAX_URL];
        int vn = 0;
        for (int j = vs; j < ve && vn < (int)sizeof(value) - 1; j++) {
            if (html[j] == '&') {
                int used = 0;
                char e[4];
                int en = html_entity(html + j, ve - j, &used, e);
                if (used > 0) {
                    for (int k = 0; k < en && vn < (int)sizeof(value) - 1; k++)
                        value[vn++] = e[k];
                    j += used - 1;
                    continue;
                }
            }
            value[vn++] = html[j];
        }
        value[vn] = '\0';
        dom_attr_put(node, name, value);
    }
}

static void dom_auto_close(int *sp, int stack[32], int tag) {
    while (*sp > 0) {
        int top = dom_nodes[stack[*sp]].tag;
        if (tag == DOM_LI && top == DOM_LI) { (*sp)--; continue; }
        if (tag == DOM_P && top == DOM_P) { (*sp)--; continue; }
        if (dom_is_block(tag) && top == DOM_P) { (*sp)--; continue; }
        if ((tag == DOM_TR || tag == DOM_TD || tag == DOM_TH) &&
            (top == DOM_TR || top == DOM_TD || top == DOM_TH)) { (*sp)--; continue; }
        break;
    }
}

static void dom_parse_into(int parent, const char *html, int len) {
    int stack[32];
    int sp = 0;
    stack[0] = parent;
    int i = 0;
    while (i < len) {
        if (html[i] != '<') {
            int start = i;
            while (i < len && html[i] != '<') i++;
            if (stack[sp] >= 0) dom_add_text(stack[sp], html + start, i - start, 1);
            continue;
        }

        if (i + 3 < len && html[i + 1] == '!' && html[i + 2] == '-' &&
            html[i + 3] == '-') {
            int close = b_find_ci(html, i + 4, len, "-->");
            if (close < 0) break;
            i = close + 3;
            continue;
        }
        if (i + 1 < len && html[i + 1] == '!') {
            int close = b_find_ci(html, i + 2, len, ">");
            if (close < 0) break;
            i = close + 1;
            continue;
        }
        if (i + 1 < len && html[i + 1] == '?') {
            int close = b_find_ci(html, i + 2, len, ">");
            if (close < 0) break;
            i = close + 1;
            continue;
        }

        int end = html_tag_end(html, i + 1, len);
        if (end < 0) break;
        int j = i + 1;
        int closing = 0;
        if (j < end && html[j] == '/') { closing = 1; j++; }
        while (j < end && b_is_space(html[j])) j++;
        int name_start = j;
        while (j < end && (html[j] == '-' || html[j] == '_' ||
                            (html[j] >= 'a' && html[j] <= 'z') ||
                            (html[j] >= 'A' && html[j] <= 'Z') ||
                            (html[j] >= '0' && html[j] <= '9'))) j++;
        int name_len = j - name_start;
        if (name_len == 0) { i = end + 1; continue; }
        int tag = dom_tag_from_name(html + name_start, name_len);

        if (closing) {
            if (tag == DOM_SCRIPT || tag == DOM_STYLE) {
                i = end + 1;
                continue;
            }
            for (int k = sp; k >= 1; k--) {
                if (dom_nodes[stack[k]].tag == tag) {
                    sp = k - 1;
                    break;
                }
            }
            i = end + 1;
            continue;
        }

        /* Icons and framework payloads are huge and have no visible text.
         * Dropping SVG and template subtrees keeps the node budget for the
         * article or search results instead of inline logos. */
        if (tag == DOM_UNKNOWN && b_name_is(html, name_start, name_len, "svg")) {
            int close = b_find_ci(html, end + 1, len, "</svg");
            if (close < 0) break;
            int gt = html_tag_end(html, close + 2, len);
            i = gt >= 0 ? gt + 1 : len;
            continue;
        }
        if (tag == DOM_UNKNOWN && b_name_is(html, name_start, name_len, "template")) {
            int close = b_find_ci(html, end + 1, len, "</template");
            if (close < 0) break;
            int gt = html_tag_end(html, close + 2, len);
            i = gt >= 0 ? gt + 1 : len;
            continue;
        }
        if (tag == DOM_UNKNOWN &&
            (b_name_is(html, name_start, name_len, "source") ||
             b_name_is(html, name_start, name_len, "track") ||
             b_name_is(html, name_start, name_len, "wbr"))) {
            i = end + 1;
            continue;
        }

        dom_auto_close(&sp, stack, tag);
        int node = dom_new_node(DOM_ELEMENT, tag, stack[sp]);
        if (node < 0) break;
        dom_parse_attrs(node, html, name_start + name_len, end);
        dom_apply_style(node);

        if (tag == DOM_SCRIPT || tag == DOM_STYLE) {
            const char *needle = tag == DOM_SCRIPT ? "</script" : "</style";
            int close = b_find_ci(html, end + 1, len, needle);
            if (close < 0) {
                i = len;
            } else {
                int raw_len = close - (end + 1);
                /* Inline application data (GitHub ships result JSON in
                 * script tags) is not a visible text node. Keep small inline
                 * scripts for the demo page, but do not spend the text pool
                 * on framework bundles or JSON payloads. */
                if (raw_len > 0 && (tag == DOM_STYLE || raw_len <= 8192))
                    dom_add_text(node, html + end + 1, raw_len, 0);
                int gt = html_tag_end(html, close + 2, len);
                i = gt >= 0 ? gt + 1 : len;
            }
            continue;
        }

        if (dom_is_void(tag)) {
            i = end + 1;
            continue;
        }
        if (sp < 31) stack[++sp] = node;
        i = end + 1;
    }
}

static void dom_parse_document(const char *html, int len) {
    dom_reset();
    dom_parse_into(dom_root, html, len);
    /* A rendered Wikipedia article is an HTML fragment with no <body>.
     * Keeping those nodes under the document root lets the layout render
     * them through dom_body_node()'s root fallback instead of hiding them
     * beneath a newly created empty body. */
    css_apply_document();
}

/* ------------------------------------------------------------------ */
/* Stylesheets                                                         */
/* ------------------------------------------------------------------ */

static int css_class_has(int node, const char *name, int len) {
    const char *classes = dom_get_attr(node, "class");
    int i = 0;
    int guard = 0;
    while (classes[i] && guard++ < 512) {
        while (classes[i] && b_is_space(classes[i])) i++;
        int start = i;
        while (classes[i] && !b_is_space(classes[i])) i++;
        if (i - start != len) continue;
        int same = 1;
        for (int j = 0; j < len; j++)
            if (classes[start + j] != name[j]) { same = 0; break; }
        if (same) return 1;
    }
    return 0;
}

static int css_simple_match(int node, const char *selector, int len) {
    while (len > 0 && b_is_space(*selector)) { selector++; len--; }
    while (len > 0 && b_is_space(selector[len - 1])) len--;
    if (len <= 0 || node < 0 || node >= dom_node_used) return 0;

    int i = 0;
    if (selector[i] != '#' && selector[i] != '.' && selector[i] != '*') {
        int start = i;
        while (i < len && selector[i] != '.' && selector[i] != '#' &&
               selector[i] != ':' && selector[i] != '[') i++;
        if (i == start) return 0;
        char tag[16];
        int n = i - start;
        if (n > 15) n = 15;
        for (int j = 0; j < n; j++) tag[j] = selector[start + j];
        tag[n] = '\0';
        if (!b_equal_ci(dom_tag_name(node), tag)) return 0;
    } else if (selector[i] == '*') {
        i++;
    }
    while (i < len) {
        if (selector[i] == '#') {
            int start = ++i;
            while (i < len && selector[i] != '.' && selector[i] != '#' &&
                   selector[i] != ':' && selector[i] != '[') i++;
            if (i == start) return 0;
            char id[64];
            int n = i - start;
            if (n > 63) n = 63;
            for (int j = 0; j < n; j++) id[j] = selector[start + j];
            id[n] = '\0';
            if (!b_equal_ci(dom_get_attr(node, "id"), id)) return 0;
        } else if (selector[i] == '.') {
            int start = ++i;
            while (i < len && selector[i] != '.' && selector[i] != '#' &&
                   selector[i] != ':' && selector[i] != '[') i++;
            if (i == start) return 0;
            if (!css_class_has(node, selector + start, i - start)) return 0;
        } else {
            return 0;
        }
    }
    return 1;
}

static int css_selector_matches(int node, const char *selector) {
    if (!selector || !selector[0] || node < 0 || node >= dom_node_used) return 0;
    int starts[6];
    int lens[6];
    int tokens = 0;
    int i = 0;
    int n = b_strlen(selector);
    while (i < n) {
        while (i < n && b_is_space(selector[i])) i++;
        if (i >= n) break;
        int start = i;
        while (i < n && !b_is_space(selector[i])) i++;
        if (tokens < 6) {
            starts[tokens] = start;
            lens[tokens] = i - start;
            tokens++;
        }
    }
    if (tokens == 0) return 0;
    int current = node;
    for (int t = tokens - 1; t >= 0; t--) {
        if (t == tokens - 1) {
            if (!css_simple_match(current, selector + starts[t], lens[t]))
                return 0;
        } else {
            current = dom_nodes[current].parent;
            int found = 0;
            int depth = 0;
            while (current > 0 && current < dom_node_used && depth++ < 64) {
                if (css_simple_match(current, selector + starts[t], lens[t])) {
                    found = 1;
                    break;
                }
                current = dom_nodes[current].parent;
            }
            if (!found) return 0;
        }
    }
    return 1;
}

static void css_add_rule(const char *selector, const char *declarations) {
    if (css_rule_count >= CSS_MAX_RULES || !selector || !selector[0] ||
        !declarations || !declarations[0])
        return;
    char sel[CSS_MAX_SELECTOR];
    char dec[CSS_MAX_DECLS];
    b_trim_copy(sel, sizeof(sel), selector);
    b_trim_copy(dec, sizeof(dec), declarations);
    if (!sel[0] || !dec[0]) return;
    int priority = 0;
    int has_tag = 0;
    for (int i = 0; sel[i]; i++) {
        if (sel[i] == '#') priority += 100;
        else if (sel[i] == '.') priority += 10;
        else if (sel[i] == ':') return; /* pseudo classes are not supported */
        else if (sel[i] == '[' || sel[i] == '>' || sel[i] == '+' || sel[i] == '~')
            return;
        else if (!b_is_space(sel[i]) && !has_tag) {
            priority += 1;
            has_tag = 1;
        }
    }
    if (priority > 255) priority = 255;
    struct css_rule *r = &css_rules[css_rule_count++];
    b_copy(r->selector, sizeof(r->selector), sel);
    b_copy(r->declarations, sizeof(r->declarations), dec);
    r->priority = (uint8_t)priority;
}

static char css_source_scratch[CSS_TEXT_MAX];

static void css_parse_text(const char *text) {
    if (!text) return;
    char *scratch = css_source_scratch;
    int scratch_max = (int)sizeof(css_source_scratch);
    int n = 0;
    int i = 0;
    while (text[i] && n < scratch_max - 1) {
        if (text[i] == '/' && text[i + 1] == '*') {
            i += 2;
            while (text[i] && !(text[i] == '*' && text[i + 1] == '/')) i++;
            if (text[i]) i += 2;
            continue;
        }
        scratch[n++] = text[i++];
    }
    scratch[n] = '\0';
    i = 0;
    while (i < n) {
        while (i < n && b_is_space(scratch[i])) i++;
        if (i >= n) break;
        if (scratch[i] == '@') {
            while (i < n && scratch[i] != '{' && scratch[i] != ';') i++;
            if (i < n && scratch[i] == ';') { i++; continue; }
            int depth = 1;
            i++;
            while (i < n && depth > 0) {
                if (scratch[i] == '{') depth++;
                else if (scratch[i] == '}') depth--;
                i++;
            }
            continue;
        }
        int sel_start = i;
        while (i < n && scratch[i] != '{' && scratch[i] != '}') i++;
        if (i >= n || scratch[i] != '{') break;
        int sel_end = i;
        i++;
        int dec_start = i;
        int depth = 1;
        while (i < n && depth > 0) {
            if (scratch[i] == '{') depth++;
            else if (scratch[i] == '}') depth--;
            if (depth == 0) break;
            i++;
        }
        if (i >= n) break;
        int dec_end = i;
        i++;

        char selector[CSS_MAX_SELECTOR];
        int sn = sel_end - sel_start;
        if (sn > (int)sizeof(selector) - 1) sn = (int)sizeof(selector) - 1;
        for (int j = 0; j < sn; j++) selector[j] = scratch[sel_start + j];
        selector[sn] = '\0';

        char declarations[CSS_MAX_DECLS];
        int dn = dec_end - dec_start;
        if (dn > (int)sizeof(declarations) - 1) dn = (int)sizeof(declarations) - 1;
        for (int j = 0; j < dn; j++) declarations[j] = scratch[dec_start + j];
        declarations[dn] = '\0';

        int part = 0;
        for (int j = 0; j <= sn; j++) {
            if (j == sn || selector[j] == ',') {
                char one[CSS_MAX_SELECTOR];
                int on = j - part;
                if (on > (int)sizeof(one) - 1) on = (int)sizeof(one) - 1;
                for (int k = 0; k < on; k++) one[k] = selector[part + k];
                one[on] = '\0';
                css_add_rule(one, declarations);
                part = j + 1;
            }
        }
    }
}

static void css_apply_site_theme(void) {
    if (site_theme == SITE_WIKIPEDIA) {
        css_add_rule("body", "background:#f8f9fa;color:#202122;");
        css_add_rule(".wiki-top", "background:#f8f9fa;border-bottom:1px solid #a2a9b1;padding:8px;");
        css_add_rule(".wiki-word", "color:#202122;font-weight:bold;");
        css_add_rule(".wiki-tabs", "background:#ffffff;border-bottom:1px solid #a2a9b1;padding:6px;");
        css_add_rule(".wiki-lang", "color:#3366cc;");
        css_add_rule("a", "color:#3366cc;");
        css_add_rule("h1", "color:#000000;margin:8px 0;");
        css_add_rule("h2", "color:#000000;margin:12px 0 6px 0;");
        css_add_rule("h3", "color:#000000;margin:10px 0 4px 0;");
        css_add_rule("p", "color:#202122;margin:7px 0;");
        css_add_rule(".mw-parser-output", "background:#ffffff;max-width:980px;margin:0 auto;padding:10px;");
        css_add_rule(".infobox", "background:#f8f9fa;border:1px solid #a2a9b1;padding:6px;max-width:340px;");
        css_add_rule(".infobox", "width:320px;");
        css_add_rule("table", "border:1px solid #a2a9b1;");
        css_add_rule("th", "background:#eaecf0;color:#202122;");
        css_add_rule("td", "border:1px solid #c8ccd1;");
        css_add_rule(".thumb", "border:1px solid #c8ccd1;padding:4px;");
        css_add_rule(".thumbcaption", "color:#54595d;");
        css_add_rule(".reflist", "color:#54595d;");
        css_add_rule(".reference", "color:#3366cc;");
        css_add_rule(".navbox", "display:none;");
        css_add_rule(".vertical-navbox", "display:none;");
        css_add_rule(".metadata", "display:none;");
        css_add_rule(".ambox", "display:none;");
        css_add_rule(".sistersitebox", "display:none;");
        css_add_rule(".shortdescription", "display:none;");
        css_add_rule(".mw-editsection", "display:none;");
        css_add_rule(".mw-jump-link", "display:none;");
        css_add_rule(".vector-header", "display:none;");
        css_add_rule(".mw-navigation", "display:none;");
    } else if (site_theme == SITE_GITHUB) {
        css_add_rule("body", "background:#ffffff;color:#1f2328;");
        css_add_rule("a", "color:#0969da;");
        css_add_rule("h1", "color:#1f2328;margin:8px 0;");
        css_add_rule("h2", "color:#1f2328;margin:12px 0 6px 0;");
        css_add_rule("h3", "color:#1f2328;margin:10px 0 4px 0;");
        css_add_rule(".Header", "background:#24292f;padding:10px;");
        css_add_rule(".Header a", "color:#ffffff;");
        css_add_rule(".Header", "color:#ffffff;");
        css_add_rule(".UnderlineNav", "background:#ffffff;border:1px solid #d0d7de;padding:6px;");
        css_add_rule(".Box", "background:#ffffff;border:1px solid #d0d7de;border-radius:6px;padding:8px;");
        css_add_rule(".repo-list", "background:#ffffff;border:1px solid #d0d7de;padding:8px;");
        css_add_rule(".search-results-page", "background:#ffffff;padding:6px;");
        css_add_rule(".search-title", "color:#0969da;");
        css_add_rule(".color-fg-muted", "color:#656d76;");
        css_add_rule(".color-fg-default", "color:#1f2328;");
        css_add_rule(".repo-head", "background:#f6f8fa;border:1px solid #d0d7de;padding:10px;");
        css_add_rule(".repo-name", "color:#0969da;font-size:24px;font-weight:bold;");
        css_add_rule(".repo-desc", "color:#1f2328;margin:6px 0;");
        css_add_rule(".repo-stat", "color:#656d76;");
        css_add_rule(".repo-box", "background:#ffffff;border:1px solid #d0d7de;padding:10px;");
        css_add_rule("footer", "color:#656d76;");
    }
}

static void css_apply_subtree(int node) {
    if (node < 0 || node >= dom_node_used) return;
    if (dom_nodes[node].type == DOM_ELEMENT) {
        for (int priority = 0; priority <= 255; priority++) {
            for (int r = 0; r < css_rule_count; r++) {
                if (css_rules[r].priority != priority) continue;
                if (css_selector_matches(node, css_rules[r].selector))
                    dom_apply_declarations(node, css_rules[r].declarations);
            }
        }
    }
    for (int c = dom_nodes[node].first; c; c = dom_nodes[c].next)
        css_apply_subtree(c);
}

static void css_apply_document(void) {
    css_rule_count = 0;
    /* GitHub keeps its colors in inline styles on the synthesized repository
     * page. Skipping selector matching here also keeps a malformed class
     * attribute from locking the single-threaded UI. */
    if (site_theme == SITE_GITHUB) return;
    css_apply_site_theme();
    /* Large pages such as rendered Wikipedia articles can carry hundreds of
     * template style rules. Matching every rule against thousands of nodes is
     * prohibitively slow here, so big documents use the site theme only. */
    bool heavy_document = site_theme == SITE_WIKIPEDIA || dom_node_used > 1500;
    if (!heavy_document) {
        for (int i = 0; i < dom_node_used; i++) {
            if (dom_nodes[i].type == DOM_ELEMENT && dom_nodes[i].tag == DOM_STYLE)
                css_parse_text(dom_node_text(i));
        }
    }
    for (int priority = 0; priority <= 255; priority++) {
        for (int r = 0; r < css_rule_count; r++) {
            if (css_rules[r].priority != priority) continue;
            for (int node = 0; node < dom_node_used; node++) {
                if (dom_nodes[node].type != DOM_ELEMENT) continue;
                if (css_selector_matches(node, css_rules[r].selector))
                    dom_apply_declarations(node, css_rules[r].declarations);
            }
        }
    }
    /* Inline styles have the final say, as they do in a normal browser. */
    for (int node = 0; node < dom_node_used; node++) {
        if (dom_nodes[node].type == DOM_ELEMENT) dom_apply_style(node);
    }
}

/* A small mutable attribute helper used by the script DOM. */
static void dom_set_attribute(int node, const char *name, const char *value) {
    if (!name) return;
    dom_attr_put(node, name, value ? value : "");
    if (b_equal_ci(name, "style") || b_equal_ci(name, "bgcolor") ||
        b_equal_ci(name, "color") || b_equal_ci(name, "align"))
        dom_apply_style(node);
    layout_dirty = 1;
}

/* ------------------------------------------------------------------ */
/* Status, history and address                                         */
/* ------------------------------------------------------------------ */

static void set_status(const char *text, bool error) {
    b_copy(status, sizeof(status), text);
    status_error = error;
}

static void history_push(const char *url) {
    if (history_pos >= 0 && history_pos < history_count &&
        b_equal_ci(history[history_pos], url))
        return;
    if (history_pos + 1 < history_count) history_count = history_pos + 1;
    if (history_count < MAX_HISTORY) {
        b_copy(history[history_count], MAX_URL, url);
        history_pos = history_count;
        history_count++;
    } else {
        for (int i = 1; i < MAX_HISTORY; i++)
            b_copy(history[i - 1], MAX_URL, history[i]);
        b_copy(history[MAX_HISTORY - 1], MAX_URL, url);
        history_pos = MAX_HISTORY - 1;
    }
}

static void set_address(const char *url) {
    b_copy(address, sizeof(address), url);
    address_len = b_strlen(address);
    address_cursor = address_len;
    address_select_all = false;
}

/* ------------------------------------------------------------------ */
/* Resource fetching                                                   */
/* ------------------------------------------------------------------ */

static int fetch_url(const char *url, uint8_t *out, int max) {
    if (!url || max <= 0) return -1;
    if (b_starts_ci(url, "data:")) return -1;
    if (b_starts_ci(url, "file://")) {
        char path[MAX_LOCAL_PATH];
        if (!parse_file_url(url, path, sizeof(path))) return -1;
        int fd = vfs_open(path);
        if (fd < 0) return -1;
        int size = vfs_get_size(fd);
        if (size < 0) return -1;
        if (size > max) size = max;
        int bytes = size > 0 ? vfs_read(fd, out, (uint32_t)size) : 0;
        return bytes;
    }
    if (!net_available()) return -1;

    char host[64];
    char path[512];
    uint16_t port = 80;
    bool is_https = false;
    if (!parse_url(url, host, sizeof(host), &port, path, sizeof(path), &is_https))
        return -1;

    /* The Ethernet stack occasionally loses a DNS reply or SYN.  Retry one
     * fresh connection before telling the user the page failed; this is the
     * same transparent retry a desktop browser performs. */
    for (int attempt = 0; attempt < 2; attempt++) {
        uint8_t ip[4];
        if (!parse_ipv4(host, ip)) {
            if (net_dns_resolve(host, ip) < 0)
                continue;
        }
        int bytes = is_https
                        ? net_https_get(ip, port, host, path, out, (uint32_t)max)
                        : net_http_get(ip, port, host, path, out, (uint32_t)max);
        if (bytes > 0) return bytes;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Images                                                              */
/* ------------------------------------------------------------------ */

static void image_reset(void) {
    for (int i = 0; i < MAX_IMAGES; i++) {
        images[i].node = -1;
        images[i].state = 0;
        images[i].w = 0;
        images[i].h = 0;
        images[i].url[0] = '\0';
    }
    for (int i = 0; i < dom_node_used; i++) dom_nodes[i].image = -1;
}

static int image_pending_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_IMAGES; i++)
        if (images[i].state == 1) count++;
    return count;
}

static int image_used_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_IMAGES; i++)
        if (images[i].state) count++;
    return count;
}

/* Keep remote thumbnails small enough for the fixed decoders. Wikimedia
 * serves arbitrary-width thumbnails from the same path, and GitHub avatars
 * accept an s= size parameter, so both can be capped without another
 * request. */
static void image_small_url(const char *url, char *out, int max) {
    b_copy(out, max, url);
    if (!url || !url[0]) return;
    int len = b_strlen(url);
    if (b_find_ci(url, 0, len, "wikimedia.org") >= 0) {
        for (int i = 0; url[i]; i++) {
            if (url[i] != '/') continue;
            int j = i + 1;
            int value = 0;
            while (url[j] >= '0' && url[j] <= '9') {
                value = value * 10 + (url[j] - '0');
                j++;
            }
            if (j == i + 1 || j + 2 >= len) continue;
            if (url[j] != 'p' || url[j + 1] != 'x' || url[j + 2] != '-') continue;
            if (value <= 250) continue;
            int oi = 0;
            for (int k = 0; k <= i && oi < max - 1; k++) out[oi++] = url[k];
            const char digit[] = "250";
            for (int k = 0; k < 3 && oi < max - 1; k++) out[oi++] = digit[k];
            for (int k = j; url[k] && oi < max - 1; k++) out[oi++] = url[k];
            out[oi] = '\0';
            return;
        }
    }
    if (b_find_ci(url, 0, len, "avatars.githubusercontent.com") >= 0 &&
        b_find_ci(url, 0, len, "s=") < 0) {
        b_append(out, max, b_find_ci(url, 0, len, "?") >= 0 ? "&s=96" : "?s=96");
    }
}

static void image_drop(int slot) {
    if (slot < 0 || slot >= MAX_IMAGES) return;
    int node = images[slot].node;
    if (node >= 0 && node < dom_node_used) dom_nodes[node].image = -2;
    images[slot].node = -1;
    images[slot].state = 0;
    images[slot].w = 0;
    images[slot].h = 0;
    images[slot].url[0] = '\0';
}

static int image_assign(int node, const char *url) {
    if (node < 0 || !url || !url[0]) return -1;
    int slot = -1;
    for (int i = 0; i < MAX_IMAGES; i++) {
        if (images[i].node == node) { slot = i; break; }
    }
    if (slot < 0) {
        for (int i = 0; i < MAX_IMAGES; i++) {
            if (images[i].state == 0) { slot = i; break; }
        }
    }
    if (slot < 0) return -1;
    if (!b_equal_ci(images[slot].url, url)) {
        b_copy(images[slot].url, sizeof(images[slot].url), url);
        images[slot].state = 1;
    }
    images[slot].node = (int16_t)node;
    dom_nodes[node].image = (int16_t)slot;
    return slot;
}

static void image_scan_node(int node);

static void image_refresh_node(int node) {
    if (node < 0 || node >= dom_node_used) return;
    if (dom_nodes[node].tag != DOM_IMG) return;
    const char *src = dom_get_attr(node, "src");
    if (!src[0]) src = dom_get_attr(node, "data-src");
    if (!src[0]) { dom_nodes[node].image = -1; return; }
    char url[MAX_URL];
    if (!resolve_href(src, url, sizeof(url))) return;
    char small[MAX_URL];
    image_small_url(url, small, sizeof(small));
    image_assign(node, small);
    layout_dirty = 1;
}

static void image_scan_node(int node) {
    if (node < 0 || node >= dom_node_used) return;
    if (!browser_load_images) {
        if (dom_nodes[node].type == DOM_ELEMENT && dom_nodes[node].tag == DOM_IMG)
            dom_nodes[node].image = -1;
        return;
    }
    if (dom_nodes[node].type == DOM_ELEMENT && dom_nodes[node].tag == DOM_IMG) {
        if (dom_nodes[node].image != -2)
            image_refresh_node(node);
        return;
    }
    for (int c = dom_nodes[node].first; c; c = dom_nodes[c].next)
        image_scan_node(c);
}

static int image_load_next(void) {
    for (int i = 0; i < MAX_IMAGES; i++) {
        if (images[i].state != 1) continue;
        char msg[96];
        b_copy(msg, sizeof(msg), "Loading image ");
        b_append_int(msg, sizeof(msg), image_used_count() - image_pending_count() + 1);
        b_append(msg, sizeof(msg), "...");
        set_status(msg, false);

        int bytes = fetch_url(images[i].url, response, RESPONSE_MAX);
        if (bytes <= 0) {
            image_drop(i);
        } else {
            int w = 0, h = 0;
            if (browser_image_decode(response, bytes, image_pixels[i],
                                     IMG_MAX_W, IMG_MAX_H, &w, &h) == 0) {
                images[i].w = w;
                images[i].h = h;
                images[i].state = 2;
            } else {
                /* Release the slot and assign the next waiting picture. */
                image_drop(i);
                image_scan_node(dom_root);
            }
        }
        layout_dirty = 1;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* DOM callback surface used by the script engine                      */
/* ------------------------------------------------------------------ */

static void dom_js_set_text(int node, const char *text) {
    if (node < 0 || node >= dom_node_used) return;
    dom_nodes[node].override_text = (int16_t)dom_text_store(text, b_strlen(text));
    layout_dirty = 1;
}

static void dom_js_set_html(int node, const char *html) {
    if (node < 0 || node >= dom_node_used || !html) return;
    dom_nodes[node].override_text = -1;
    dom_clear_children(node);
    dom_parse_into(node, html, b_strlen(html));
    if (page_loaded) image_scan_node(node);
    layout_dirty = 1;
}

static void dom_js_append_html(const char *html) {
    if (!html) return;
    int body = dom_body_node();
    if (body < 0) return;
    dom_parse_into(body, html, b_strlen(html));
    if (page_loaded) image_scan_node(body);
    layout_dirty = 1;
}

static void dom_js_set_style(int node, const char *property, const char *value) {
    if (node < 0 || node >= dom_node_used) return;
    char prop[32];
    int n = 0;
    while (property && property[n] && n < 31) {
        prop[n] = b_lower(property[n]);
        n++;
    }
    prop[n] = '\0';
    if (b_equal_ci(prop, "backgroundcolor")) b_copy(prop, sizeof(prop), "background-color");
    dom_apply_style_prop(&dom_nodes[node], prop, value ? value : "");
    layout_dirty = 1;
}

static void dom_js_set_attr(int node, const char *name, const char *value) {
    if (node < 0 || node >= dom_node_used || !name) return;
    const char *key = name;
    if (b_equal_ci(name, "className")) key = "class";
    dom_set_attribute(node, key, value ? value : "");
    if (b_equal_ci(key, "src") && page_loaded) image_refresh_node(node);
}

static void dom_js_status(const char *text) {
    if (text && text[0]) set_status(text, false);
}

static int dom_js_query_all(const char *selector, int *out, int max) {
    if (!selector || !selector[0] || !out || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < dom_node_used && count < max; i++) {
        if (dom_nodes[i].type != DOM_ELEMENT) continue;
        if (css_selector_matches(i, selector)) out[count++] = i;
    }
    return count;
}

static int dom_js_create_element(const char *tag) {
    int id = dom_tag_from_name(tag ? tag : "div", b_strlen(tag ? tag : "div"));
    if (id <= 0 || id >= DOM_COUNT) id = DOM_UNKNOWN;
    return dom_new_node(DOM_ELEMENT, id, -1);
}

static void dom_js_unlink(int node) {
    if (node <= 0 || node >= dom_node_used) return;
    int parent = dom_nodes[node].parent;
    if (parent <= 0 || parent >= dom_node_used) return;
    int prev = 0;
    for (int c = dom_nodes[parent].first; c; c = dom_nodes[c].next) {
        if (c == node) break;
        prev = c;
    }
    if (prev) dom_nodes[prev].next = dom_nodes[node].next;
    else dom_nodes[parent].first = dom_nodes[node].next;
    if (dom_nodes[parent].last == node) dom_nodes[parent].last = (uint16_t)prev;
    dom_nodes[node].next = 0;
    dom_nodes[node].parent = 0;
}

static int dom_js_append_child(int parent, int child) {
    if (parent <= 0 || parent >= dom_node_used || child <= 0 ||
        child >= dom_node_used || parent == child) return 0;
    for (int p = parent; p > 0 && p < dom_node_used; p = dom_nodes[p].parent)
        if (p == child) return 0;
    dom_js_unlink(child);
    dom_nodes[child].parent = (uint16_t)parent;
    if (dom_nodes[parent].last) {
        dom_nodes[dom_nodes[parent].last].next = (uint16_t)child;
    } else {
        dom_nodes[parent].first = (uint16_t)child;
    }
    dom_nodes[parent].last = (uint16_t)child;
    dom_nodes[child].next = 0;
    layout_dirty = 1;
    return 1;
}

static int dom_js_remove_child(int parent, int child) {
    if (parent <= 0 || parent >= dom_node_used || child <= 0 ||
        child >= dom_node_used) return 0;
    if (dom_nodes[child].parent != parent) return 0;
    dom_js_unlink(child);
    layout_dirty = 1;
    return 1;
}

static int dom_js_parent_node(int node) {
    if (node <= 0 || node >= dom_node_used) return -1;
    int parent = dom_nodes[node].parent;
    return parent > 0 ? parent : -1;
}

static int dom_js_first_child(int node, int element_only) {
    if (node < 0 || node >= dom_node_used) return -1;
    for (int c = dom_nodes[node].first; c; c = dom_nodes[c].next) {
        if (!element_only || dom_nodes[c].type == DOM_ELEMENT) return c;
    }
    return -1;
}

static int dom_js_next_sibling(int node, int element_only) {
    if (node <= 0 || node >= dom_node_used) return -1;
    for (int c = dom_nodes[node].next; c; c = dom_nodes[c].next) {
        if (!element_only || dom_nodes[c].type == DOM_ELEMENT) return c;
    }
    return -1;
}

static const struct browser_js_dom dom_script_ops = {
    dom_find_id,
    dom_find_selector,
    dom_js_query_all,
    dom_body_node,
    dom_tag_name,
    dom_node_text,
    dom_get_attr,
    dom_js_set_text,
    dom_js_set_html,
    dom_js_append_html,
    dom_js_set_style,
    dom_js_set_attr,
    dom_js_create_element,
    dom_js_append_child,
    dom_js_remove_child,
    dom_js_parent_node,
    dom_js_first_child,
    dom_js_next_sibling,
    dom_js_status
};

/* ------------------------------------------------------------------ */
/* Page parsing and navigation                                         */
/* ------------------------------------------------------------------ */

static void page_reset_view(void) {
    scroll_y = 0;
    content_height = 0;
    anchor_pending = false;
    pending_anchor[0] = '\0';
    layout_dirty = 1;
    layout_width = 0;
    page_loaded = false;
    onload_pending = false;
    image_reset();
}

static void set_page_title_from_dom(void) {
    page_title[0] = '\0';
    int title = dom_find_title();
    if (title >= 0) {
        b_copy(page_title, sizeof(page_title), dom_node_text(title));
        b_trim_copy(page_title, sizeof(page_title), page_title);
    }
    if (!page_title[0]) {
        if (b_starts_ci(page_url, "file://")) {
            b_copy(page_title, sizeof(page_title), "Local page");
        } else {
            const char *p = b_starts_ci(page_url, "https://") ? page_url + 8
                                                              : page_url + 7;
            int n = 0;
            while (p[n] && p[n] != '/' && n < (int)sizeof(page_title) - 1) {
                page_title[n] = p[n];
                n++;
            }
            page_title[n] = '\0';
            if (site_theme == SITE_WIKIPEDIA) {
                int wiki = b_find_ci(page_url, 0, b_strlen(page_url), "/wiki/");
                if (wiki >= 0) {
                    const char *src = page_url + wiki + 6;
                    int j = 0;
                    while (src[j] && src[j] != '?' && src[j] != '#' &&
                           j < (int)sizeof(page_title) - 1) {
                        page_title[j] = src[j] == '_' ? ' ' : src[j];
                        j++;
                    }
                    page_title[j] = '\0';
                }
            }
        }
    }
}

static void dom_execute_scripts(int node, int *count) {
    if (node < 0 || node >= dom_node_used || *count >= 8) return;
    struct dom_node *n = &dom_nodes[node];
    if (n->type == DOM_ELEMENT && n->tag == DOM_SCRIPT) {
        (*count)++;
        const char *src = dom_get_attr(node, "src");
        const char *source = 0;
        if (src[0]) {
            char url[MAX_URL];
            if (resolve_href(src, url, sizeof(url))) {
                int bytes = fetch_url(url, response, RESPONSE_MAX);
                if (bytes > 0) {
                    response[bytes] = '\0';
                    source = (const char *)response;
                }
            }
        } else {
            source = dom_node_text(node);
        }
        if (source && source[0]) {
            char error[160];
            error[0] = '\0';
            if (browser_js_run(source, &dom_script_ops, error, sizeof(error)) < 0) {
                script_errors++;
                if (!status_error) {
                    char msg[160];
                    b_copy(msg, sizeof(msg), "Script: ");
                    b_append(msg, sizeof(msg), error);
                    set_status(msg, true);
                }
            }
        }
        return;
    }
    for (int c = n->first; c; c = dom_nodes[c].next)
        dom_execute_scripts(c, count);
}

static void dom_register_handlers(int node) {
    if (node < 0 || node >= dom_node_used) return;
    if (dom_nodes[node].type == DOM_ELEMENT) {
        const char *onclick = dom_get_attr(node, "onclick");
        if (onclick[0] && browser_js_set_inline_handler(node, onclick) < 0)
            script_errors++;
    }
    for (int c = dom_nodes[node].first; c; c = dom_nodes[c].next)
        dom_register_handlers(c);
}

static int github_home_url(const char *url) {
    const char prefix[] = "https://github.com";
    if (!url || !b_starts_ci(url, prefix)) return 0;
    const char *p = url + (int)sizeof(prefix) - 1;
    return !*p || (*p == '/' && !p[1]);
}

/* Rendered Wikipedia articles come from action=render, which deliberately
 * omits the site header. Build a small native header (with a real search
 * form) and put it in front of the article. */
static int site_chrome_install(void) {
    if (site_theme == SITE_NONE) return -1;
    if (site_theme == SITE_GITHUB && github_home_url(page_url)) return -1;
    int body = dom_body_node();
    if (body < 0) return -1;
    char html[8192];
    html[0] = '\0';
    if (site_theme == SITE_WIKIPEDIA) {
        b_append(html, sizeof(html),
                 "<div class=\"wiki-top\">"
                 "<img class=\"wiki-logo\" src=\"https://thumb.wikimedia.org/"
                 "wikipedia/en/thumb/8/80/Wikipedia-logo-v2.svg/60px-"
                 "Wikipedia-logo-v2.svg.png\" width=\"48\" height=\"44\" "
                 "alt=\"Wikipedia\">"
                 "<div class=\"wiki-word\">WIKIPEDIA<br>The Free Encyclopedia</div>"
                 "<form action=\"solis://wikipedia\" method=\"get\">"
                 "<input type=\"search\" name=\"q\" placeholder=\"Search Wikipedia\">"
                 "<button type=\"submit\">Search</button></form>"
                 "</div>"
                 "<div class=\"wiki-tabs\"><b>Article</b> &nbsp; Talk &nbsp; "
                 "Read &nbsp; View source &nbsp; View history &nbsp; Tools &nbsp; "
                 "<span class=\"wiki-lang\">Languages</span></div>");
    } else if (site_theme == SITE_GITHUB) {
        b_append(html, sizeof(html),
                 "<div style=\"background:#0d1117;color:#e6edf3;padding:8px;\">"
                 "<div style=\"font-size:22px;font-weight:bold;\">GitHub</div>"
                 "<form action=\"solis://github\" method=\"get\">"
                 "<input type=\"search\" name=\"q\" placeholder=\"Search GitHub\">"
                 "<button type=\"submit\">Search</button></form>"
                 "<div style=\"color:#8b949e;\">Dashboard &nbsp; Home &nbsp; "
                 "Repositories &nbsp; Issues &nbsp; Pull requests</div></div>");
    }
    if (!html[0]) return -1;
    int chrome = dom_new_node(DOM_ELEMENT, DOM_DIV, body);
    if (chrome < 0) return -1;
    dom_parse_into(chrome, html, b_strlen(html));
    int first = dom_nodes[body].first;
    if (first == chrome) return chrome;
    int prev = -1;
    for (int n = first; n; n = dom_nodes[n].next) {
        if (n == chrome) break;
        prev = n;
    }
    if (prev >= 0) dom_nodes[prev].next = dom_nodes[chrome].next;
    dom_nodes[chrome].next = first;
    dom_nodes[body].first = chrome;
    return chrome;
}

static void show_error(const char *title, const char *detail) {
    browser_js_reset();
    page_reset_view();
    site_theme = SITE_NONE;
    form_input_node = -1;
    dom_reset();
    int body = dom_new_node(DOM_ELEMENT, DOM_BODY, dom_root);
    if (body >= 0) {
        int h = dom_new_node(DOM_ELEMENT, DOM_H1, body);
        if (h >= 0) dom_add_text(h, title, b_strlen(title), 1);
        int p = dom_new_node(DOM_ELEMENT, DOM_P, body);
        if (p >= 0) dom_add_text(p, detail, b_strlen(detail), 1);
    }
    b_copy(page_title, sizeof(page_title), title);
    scroll_y = 0;
    content_height = 0;
    layout_dirty = 1;
    page_loaded = false;
    home_active = false;
    image_reset();
}

static void load_document(const char *html, int bytes, const char *url,
                          bool push_history) {
    page_reset_view();
    home_active = false;
    form_input_node = -1;
    form_input[0] = '\0';
    form_input_len = 0;
    b_copy(page_url, sizeof(page_url), url);
    site_theme = SITE_NONE;
    if (b_find_ci(page_url, 0, b_strlen(page_url), "wikipedia.org") >= 0)
        site_theme = SITE_WIKIPEDIA;
    else if (b_find_ci(page_url, 0, b_strlen(page_url), "github.com") >= 0)
        site_theme = SITE_GITHUB;
    set_address(url);
    address_focus = false;
    address_select_all = false;

    browser_js_reset();
    bool wants_chrome = site_theme == SITE_WIKIPEDIA ||
                        (site_theme == SITE_GITHUB && !github_home_url(page_url));
    dom_node_reserve = wants_chrome ? 96 : 0;
    dom_str_reserve = wants_chrome ? 2048 : 0;
    dom_text_reserve = wants_chrome ? 2048 : 0;
    dom_parse_document(html, bytes);
    dom_node_reserve = 0;
    dom_str_reserve = 0;
    dom_text_reserve = 0;
    int chrome = site_chrome_install();
    if (chrome >= 0 && site_theme == SITE_WIKIPEDIA)
        css_apply_subtree(chrome);
    set_page_title_from_dom();
    request_anchor_from_url(url);

    page_loaded = true;
    int scripts = 0;
    script_errors = 0;
    if (browser_js_enabled) {
        dom_execute_scripts(dom_root, &scripts);
        dom_register_handlers(dom_root);
    }
    image_scan_node(dom_root);
    layout_dirty = 1;

    page_bytes = bytes;
    b_copy(page_bytes_status, sizeof(page_bytes_status), "Loaded ");
    b_append_int(page_bytes_status, sizeof(page_bytes_status), bytes);
    b_append(page_bytes_status, sizeof(page_bytes_status), " bytes");

    if (image_pending_count() > 0) {
        onload_pending = true;
        char msg[96];
        b_copy(msg, sizeof(msg), "Loading ");
        b_append_int(msg, sizeof(msg), image_pending_count());
        b_append(msg, sizeof(msg), " image(s)...");
        set_status(msg, false);
    } else {
        onload_pending = false;
        set_status(b_starts_ci(url, "file://") ? "Local page ready."
                                                   : "Page ready.",
                   status_error);
        if (browser_js_fire_load() < 0) script_errors++;
        image_scan_node(dom_root);
    }
    if (script_errors > 0 && !status_error) {
        char msg[128];
        b_copy(msg, sizeof(msg), "Page loaded, script warning(s)");
        set_status(msg, true);
    }
    if (push_history) history_push(url);
}

static void show_home(void) {
    browser_js_reset();
    site_theme = SITE_NONE;
    form_input_node = -1;
    dom_reset();
    page_reset_view();
    address[0] = '\0';
    address_len = 0;
    address_cursor = 0;
    address_focus = false;
    address_select_all = false;
    home_active = true;
    page_url[0] = '\0';
    page_title[0] = '\0';
    set_status("Web and local HTML. Type an address or open a file.", false);
}

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */

static struct layout_item *layout_next(uint8_t kind) {
    if (layout_item_count >= MAX_LAYOUT) return 0;
    struct layout_item *it = &layout_items[layout_item_count++];
    it->x = 0; it->y = 0; it->w = 0; it->h = 0;
    it->node = -1; it->link_node = -1; it->event_node = -1;
    it->image = -1; it->tlen = 0; it->text = 0;
    it->fg = 0xFF20242B; it->bg = 0xFFFFFFFF; it->border = 0;
    it->kind = kind; it->flags = 0; it->radius = 0;
    return it;
}

static int layout_copy_label(const char *text, int max) {
    int used = b_strlen(layout_text_scratch);
    if (used >= (int)sizeof(layout_text_scratch) - 1) return 0;
    int n = b_strlen(text);
    if (n > max) n = max;
    if (n > (int)sizeof(layout_text_scratch) - used - 1)
        n = (int)sizeof(layout_text_scratch) - used - 1;
    for (int i = 0; i < n; i++) layout_text_scratch[used + i] = text[i];
    layout_text_scratch[used + n] = '\0';
    return used;
}

static void layout_align_line(struct layout_ctx *ctx) {
    if (ctx->line_start >= layout_item_count) return;
    if (ctx->align == 0) return;
    int width = ctx->line_max - ctx->line_x0;
    int avail = ctx->right - ctx->left;
    if (width <= 0 || width >= avail) return;
    int target = ctx->left;
    if (ctx->align == 1) target = ctx->left + (avail - width) / 2;
    else target = ctx->right - width;
    int delta = target - ctx->line_x0;
    if (delta == 0) return;
    for (int i = ctx->line_start; i < layout_item_count; i++)
        layout_items[i].x += (int16_t)delta;
}

static void layout_flush(struct layout_ctx *ctx) {
    layout_align_line(ctx);
    if (ctx->line_start < layout_item_count) ctx->y += LINE_H;
    if (ctx->y > ctx->max_y) ctx->max_y = ctx->y;
    ctx->x = ctx->left + ctx->indent;
    ctx->line_start = layout_item_count;
    ctx->line_x0 = ctx->x;
    ctx->line_max = ctx->x;
    ctx->space_pending = 0;
}

static void layout_maybe_space(struct layout_ctx *ctx) {
    if (!ctx->space_pending) return;
    if (ctx->line_start < layout_item_count) {
        ctx->x += CHAR_W;
        if (ctx->x > ctx->line_max) ctx->line_max = ctx->x;
    }
    ctx->space_pending = 0;
}

static void layout_start_block(struct layout_ctx *ctx, int margin) {
    layout_flush(ctx);
    ctx->y += margin;
    ctx->x = ctx->left + ctx->indent;
    ctx->line_x0 = ctx->x;
    ctx->line_max = ctx->x;
}

static void layout_end_block(struct layout_ctx *ctx, int margin) {
    layout_flush(ctx);
    ctx->y += margin;
    if (ctx->y > ctx->max_y) ctx->max_y = ctx->y;
}

static void layout_add_bg(struct layout_ctx *ctx, int y, int x, int w, int h,
                          uint32_t color, uint32_t border, int radius) {
    (void)ctx;
    if (h <= 0 || w <= 0) return;
    struct layout_item *it = layout_next(LAY_BG);
    if (!it) return;
    it->x = (int16_t)x; it->y = (int16_t)y;
    it->w = (int16_t)w; it->h = (int16_t)h;
    it->bg = color;
    it->border = border;
    it->radius = (uint8_t)(radius < 0 ? 0 : (radius > 24 ? 24 : radius));
}

static void layout_add_text(struct layout_ctx *ctx, const struct layout_style *st,
                            int node, const char *text, int len) {
    if (len <= 0) return;
    if (ctx->line_start < layout_item_count &&
        ctx->x + b_width_n(text, len) > ctx->right)
        layout_flush(ctx);

    int i = 0;
    while (i < len && layout_item_count < MAX_LAYOUT) {
        int avail = ctx->right - ctx->x;
        if (avail < CHAR_W) {
            layout_flush(ctx);
            avail = ctx->right - ctx->x;
            if (avail < CHAR_W) break;
        }
        /* Accumulate whole codepoints until the next one would cross the
         * margin, so a multi-byte character is never split between items. */
        int start = i;
        int width = 0;
        while (i < len) {
            uint32_t cp;
            int used = b_utf8_decode(text + i, &cp);
            if (used <= 0) break;
            if (used > len - i) used = len - i;
            int adv = b_cp_width(cp);
            if (i > start && width + adv > avail) break;
            width += adv;
            i += used;
        }
        if (i == start) break;

        struct layout_item *it = layout_next(LAY_TEXT);
        if (!it) break;
        it->x = (int16_t)ctx->x;
        it->y = (int16_t)ctx->y;
        it->w = (int16_t)width;
        it->h = LINE_H;
        it->node = (int16_t)node;
        it->link_node = st->link_node;
        it->event_node = st->event_node;
        it->tlen = (uint16_t)(i - start);
        it->text = text + start;
        it->fg = st->fg;
        it->bg = st->bg;
        if (st->bold) it->flags |= LAY_F_BOLD;
        if (st->link_node >= 0 || st->underline) it->flags |= LAY_F_UNDERLINE;
        if (st->has_bg) it->flags |= LAY_F_BG;
        ctx->x += width;
        if (ctx->x > ctx->line_max) ctx->line_max = ctx->x;
        if (i < len) layout_flush(ctx);
    }
}

static void layout_add_text_node(struct layout_ctx *ctx,
                                 const struct layout_style *st, int node) {
    const char *text = dom_text(dom_nodes[node].text);
    int len = b_strlen(text);
    int i = 0;
    while (i < len) {
        if (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' ||
            text[i] == '\n') {
            ctx->space_pending = 1;
            i++;
            continue;
        }
        int start = i;
        while (i < len && text[i] != ' ' && text[i] != '\t' &&
               text[i] != '\r' && text[i] != '\n') i++;
        layout_maybe_space(ctx);
        layout_add_text(ctx, st, node, text + start, i - start);
    }
}

static void layout_add_pre(struct layout_ctx *ctx, const struct layout_style *st,
                           int node) {
    const char *text = dom_text(dom_nodes[node].text);
    int len = b_strlen(text);
    int start = 0;
    for (int i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            if (i > start) layout_add_text(ctx, st, node, text + start, i - start);
            layout_flush(ctx);
            start = i + 1;
        }
    }
}

static void layout_add_image(struct layout_ctx *ctx, const struct layout_style *st,
                             int node) {
    layout_flush(ctx);
    struct dom_node *n = &dom_nodes[node];
    int slot = n->image;
    int state = slot >= 0 && slot < MAX_IMAGES ? images[slot].state : 0;
    int iw = slot >= 0 && slot < MAX_IMAGES ? images[slot].w : 0;
    int ih = slot >= 0 && slot < MAX_IMAGES ? images[slot].h : 0;
    int avail = ctx->right - ctx->left;
    int req_w = dom_attr_int(node, "width");
    int req_h = dom_attr_int(node, "height");
    int w, h;
    if (state == 2 && iw > 0 && ih > 0) {
        w = req_w > 0 ? req_w : iw;
        if (req_h > 0) h = req_h;
        else h = ih * w / iw;
        if (w > avail) { h = h * avail / w; w = avail; }
        if (w < 24) w = 24;
        if (h < 16) h = 16;
    } else {
        w = req_w > 0 ? req_w : (avail < 220 ? avail : 220);
        h = req_h > 0 ? req_h : (state == 1 ? 86 : 62);
        if (w > avail) { h = h * avail / w; w = avail; }
        if (w < 80) w = 80;
        if (h < 16) h = 16;
    }
    int x = ctx->left + ctx->indent;
    if (st->align == 1) x = ctx->left + (avail - w) / 2;
    else if (st->align == 2) x = ctx->right - w;
    struct layout_item *it = layout_next(LAY_IMAGE);
    if (!it) return;
    it->x = (int16_t)x; it->y = (int16_t)ctx->y;
    it->w = (int16_t)w; it->h = (int16_t)h;
    it->node = (int16_t)node;
    it->image = (int16_t)slot;
    it->link_node = st->link_node;
    it->event_node = browser_js_has_handler(node) ? (int16_t)node : st->event_node;
    it->text = dom_get_attr(node, "alt");
    it->tlen = (uint16_t)b_strlen(it->text);
    ctx->y += h + 8;
    if (ctx->y > ctx->max_y) ctx->max_y = ctx->y;
    ctx->x = ctx->left + ctx->indent;
    ctx->line_start = layout_item_count;
    ctx->line_x0 = ctx->x;
    ctx->line_max = ctx->x;
}

static void layout_add_rule(struct layout_ctx *ctx) {
    layout_flush(ctx);
    struct layout_item *it = layout_next(LAY_RULE);
    if (it) {
        it->x = (int16_t)(ctx->left + ctx->indent);
        it->y = (int16_t)(ctx->y + 4);
        it->w = (int16_t)(ctx->right - ctx->left - ctx->indent);
        it->h = 1;
        it->bg = 0xFFD5DDE8;
    }
    ctx->y += 12;
}

static void layout_add_button(struct layout_ctx *ctx, const struct layout_style *st,
                              int node) {
    layout_flush(ctx);
    const char *label = dom_node_text(node);
    int off = layout_copy_label(label, 48);
    int len = b_strlen(layout_text_scratch + off);
    int w = b_text_width(layout_text_scratch + off) + 30;
    int h = 28;
    int avail = ctx->right - ctx->left;
    if (w > avail) w = avail;
    if (w < 88) w = 88;
    int x = ctx->left + ctx->indent;
    if (st->align == 1) x = ctx->left + (avail - w) / 2;
    else if (st->align == 2) x = ctx->right - w;
    struct layout_item *it = layout_next(LAY_BUTTON);
    if (!it) return;
    it->x = (int16_t)x; it->y = (int16_t)ctx->y;
    it->w = (int16_t)w; it->h = (int16_t)h;
    it->node = (int16_t)node;
    it->event_node = (int16_t)node;
    it->link_node = st->link_node;
    it->text = layout_text_scratch + off;
    it->tlen = (uint16_t)len;
    it->fg = 0xFFFFFFFF;
    ctx->y += h + 8;
    if (ctx->y > ctx->max_y) ctx->max_y = ctx->y;
    ctx->x = ctx->left + ctx->indent;
    ctx->line_start = layout_item_count;
    ctx->line_x0 = ctx->x;
    ctx->line_max = ctx->x;
}

static void layout_add_input(struct layout_ctx *ctx, int node) {
    layout_flush(ctx);
    const char *value = dom_get_attr(node, "value");
    if (!value[0]) value = dom_get_attr(node, "placeholder");
    int w = 210;
    int avail = ctx->right - ctx->left;
    if (w > avail) w = avail;
    if (w < 80) w = 80;
    struct layout_item *it = layout_next(LAY_INPUT);
    if (!it) return;
    it->x = (int16_t)(ctx->left + ctx->indent);
    it->y = (int16_t)ctx->y;
    it->w = (int16_t)w; it->h = 26;
    it->node = (int16_t)node;
    it->text = value;
    it->tlen = (uint16_t)b_strlen(value);
    it->fg = value[0] ? 0xFF3C4043 : 0xFF9AA0A6;
    ctx->y += 34;
    if (ctx->y > ctx->max_y) ctx->max_y = ctx->y;
    ctx->x = ctx->left + ctx->indent;
    ctx->line_start = layout_item_count;
    ctx->line_x0 = ctx->x;
    ctx->line_max = ctx->x;
}

static struct layout_style layout_style_for(int node,
                                            const struct layout_style *parent) {
    struct layout_style st = *parent;
    struct dom_node *n = &dom_nodes[node];
    st.event_node = parent->event_node;
    st.link_node = parent->link_node;
    st.pre = 0;
    if (n->has_fg) st.fg = n->fg;
    if (n->has_bg) { st.bg = n->bg; st.has_bg = 1; }
    if (n->has_bold) st.bold = n->bold;
    else if (n->bold) st.bold = 1;
    if (n->underline) st.underline = 1;
    if (n->border) st.border = n->border;
    if (n->radius) st.radius = n->radius;
    if (n->align) st.align = n->align;
    if (browser_js_has_handler(node)) st.event_node = (int16_t)node;

    switch (n->tag) {
        case DOM_BODY:
            st.align = n->align;
            break;
        case DOM_H1:
            if (!n->has_fg) st.fg = 0xFF173B6C;
            st.bold = 1; st.indent = parent->indent;
            break;
        case DOM_H2:
            if (!n->has_fg) st.fg = 0xFF1F4E79;
            st.bold = 1; st.indent = parent->indent;
            break;
        case DOM_H3:
        case DOM_H4:
            if (!n->has_fg) st.fg = 0xFF2F5D8A;
            st.bold = 1;
            break;
        case DOM_A: {
            const char *href = dom_get_attr(node, "href");
            if (href[0]) {
                st.link_node = (int16_t)node;
                if (!n->has_fg) st.fg = 0xFF1558B0;
            }
            break;
        }
        case DOM_B:
        case DOM_STRONG:
            st.bold = 1;
            break;
        case DOM_CODE:
            if (!n->has_fg) st.fg = 0xFF8A3B12;
            if (!n->has_bg) { st.bg = 0xFFF1F3F5; st.has_bg = 1; }
            break;
        case DOM_PRE:
            if (!n->has_fg) st.fg = 0xFF233044;
            if (!n->has_bg) { st.bg = 0xFFF5F7FA; st.has_bg = 1; }
            st.pre = 1;
            break;
        case DOM_BLOCKQUOTE:
            if (!n->has_fg) st.fg = 0xFF40536B;
            if (!n->has_bg) { st.bg = 0xFFEDF5FF; st.has_bg = 1; }
            st.indent = parent->indent + 16;
            break;
        case DOM_UL:
        case DOM_OL:
            st.indent = parent->indent + 16;
            break;
        case DOM_CENTER:
            st.align = 1;
            break;
        default:
            break;
    }
    return st;
}

static void layout_node(int node, struct layout_style parent,
                        struct layout_ctx *ctx, int depth) {
    if (node < 0 || node >= dom_node_used || depth > 24) return;
    struct dom_node *n = &dom_nodes[node];
    if (n->type == DOM_TEXT) {
        if (!parent.pre) layout_add_text_node(ctx, &parent, node);
        else layout_add_pre(ctx, &parent, node);
        return;
    }
    if (n->hidden || n->tag == DOM_HEAD || n->tag == DOM_META ||
        n->tag == DOM_TITLE || n->tag == DOM_SCRIPT || n->tag == DOM_STYLE)
        return;
    if (n->override_text >= 0) {
        struct layout_style st = layout_style_for(node, &parent);
        int block = dom_is_block(n->tag) || n->tag == DOM_BODY;
        int old_indent = ctx->indent;
        if (block) {
            ctx->indent = st.indent;
            layout_start_block(ctx, 6);
        }
        layout_add_text(ctx, &st, node, dom_text(n->override_text),
                        b_strlen(dom_text(n->override_text)));
        if (block) {
            layout_end_block(ctx, 6);
            ctx->indent = old_indent;
        }
        return;
    }
    if (n->tag == DOM_BR) { layout_flush(ctx); return; }
    if (n->tag == DOM_IMG) { layout_add_image(ctx, &parent, node); return; }
    if (n->tag == DOM_HR) { layout_add_rule(ctx); return; }

    struct layout_style st = layout_style_for(node, &parent);
    if (n->tag == DOM_BUTTON) { layout_add_button(ctx, &st, node); return; }
    if (n->tag == DOM_INPUT) { layout_add_input(ctx, node); return; }

    if (n->tag == DOM_LI) {
        layout_flush(ctx);
        struct layout_item *bullet = layout_next(LAY_BULLET);
        if (bullet) {
            bullet->x = (int16_t)(ctx->left + ctx->indent);
            bullet->y = (int16_t)(ctx->y + 6);
            bullet->w = 4; bullet->h = 4;
            bullet->bg = st.fg;
        }
        ctx->indent += 12;
        ctx->x = ctx->left + ctx->indent;
        ctx->line_x0 = ctx->x;
        ctx->line_max = ctx->x;
        for (int c = n->first; c; c = dom_nodes[c].next)
            layout_node(c, st, ctx, depth + 1);
        layout_flush(ctx);
        ctx->indent -= 12;
        ctx->y += 2;
        return;
    }

    int block = n->display == 1 ||
                (n->display == 0 &&
                 (dom_is_block(n->tag) || n->tag == DOM_BODY ||
                  n->tag == DOM_HTML || n->tag == DOM_SECTION ||
                  n->tag == DOM_ARTICLE || n->tag == DOM_HEADER ||
                  n->tag == DOM_FOOTER || n->tag == DOM_NAV ||
                  n->tag == DOM_ASIDE || n->tag == DOM_MAIN ||
                  n->tag == DOM_FORM));
    if (n->display == 2) block = 0;
    if (!block) {
        struct layout_ctx saved = *ctx;
        ctx->align = st.align;
        for (int c = n->first; c; c = dom_nodes[c].next)
            layout_node(c, st, ctx, depth + 1);
        ctx->align = saved.align;
        return;
    }

    int old_indent = ctx->indent;
    int old_align = ctx->align;
    int old_left = ctx->left;
    int old_right = ctx->right;
    int box_width = n->css_width > 0 ? n->css_width : ctx->right - ctx->left;
    if (n->css_max_width > 0 && box_width > n->css_max_width)
        box_width = n->css_max_width;
    int available_width = ctx->right - ctx->left;
    if (box_width < 120) box_width = 120;
    if (box_width > available_width) box_width = available_width;
    if (box_width < available_width) {
        int extra = available_width - box_width;
        if (n->center) ctx->left += extra / 2;
        ctx->right = ctx->left + box_width;
    }
    int default_margin = (n->tag == DOM_H1 || n->tag == DOM_H2) ? 10 :
                         (n->tag == DOM_H3 || n->tag == DOM_H4 || n->tag == DOM_H5 ||
                          n->tag == DOM_H6) ? 8 :
                         (n->tag == DOM_P) ? 7 :
                         (n->tag == DOM_BODY || n->tag == DOM_HTML) ? 0 : 6;
    int margin_top = n->margin_top >= 0 ? n->margin_top : default_margin;
    int margin_bottom = n->margin_bottom >= 0 ? n->margin_bottom : default_margin;
    if (margin_top < 0) margin_top = 0;
    if (margin_bottom < 0) margin_bottom = 0;
    ctx->indent = st.indent + n->padding;
    ctx->align = st.align;
    int start_y = ctx->y;
    layout_start_block(ctx, margin_top);
    if ((n->tag == DOM_H1 || n->tag == DOM_H2) && site_theme == SITE_NONE) {
        struct layout_item *bar = layout_next(LAY_BAR);
        if (bar) {
            bar->x = (int16_t)(ctx->left + ctx->indent);
            bar->y = (int16_t)ctx->y;
            bar->w = 3; bar->h = 16;
            bar->bg = 0xFF4285F4;
        }
        ctx->indent += 12;
        ctx->x = ctx->left + ctx->indent;
        ctx->line_x0 = ctx->x;
        ctx->line_max = ctx->x;
    }
    for (int c = n->first; c; c = dom_nodes[c].next)
        layout_node(c, st, ctx, depth + 1);
    layout_end_block(ctx, margin_bottom);
    if (st.has_bg && n->tag != DOM_BODY)
        layout_add_bg(ctx, start_y, ctx->left + st.indent,
                      ctx->right - ctx->left - st.indent,
                      ctx->y - start_y - margin_bottom, st.bg, st.border,
                      st.radius);
    ctx->indent = old_indent;
    ctx->align = old_align;
    ctx->left = old_left;
    ctx->right = old_right;
    ctx->x = ctx->left + ctx->indent;
    ctx->line_x0 = ctx->x;
    ctx->line_max = ctx->x;
}

static void layout_build_hits(void) {
    layout_hit_count = 0;
    for (int i = 0; i < layout_item_count; i++) {
        struct layout_item *it = &layout_items[i];
        if (it->kind == LAY_BG || it->kind == LAY_RULE || it->kind == LAY_BAR ||
            it->kind == LAY_BULLET)
            continue;
        if (it->link_node >= 0 && layout_hit_count < MAX_HITS) {
            struct hit_rect *h = &layout_hits[layout_hit_count++];
            h->x = it->x; h->y = it->y; h->w = it->w; h->h = it->h;
            h->node = it->link_node; h->kind = 1;
        }
        if (it->event_node >= 0 && layout_hit_count < MAX_HITS) {
            struct hit_rect *h = &layout_hits[layout_hit_count++];
            h->x = it->x; h->y = it->y; h->w = it->w; h->h = it->h;
            h->node = it->event_node; h->kind = 2;
        }
        if (it->kind == LAY_INPUT && layout_hit_count < MAX_HITS) {
            struct hit_rect *h = &layout_hits[layout_hit_count++];
            h->x = it->x; h->y = it->y; h->w = it->w; h->h = it->h;
            h->node = it->node; h->kind = 3;
        }
    }
}

static void layout_build(int content_x, int page_y, int content_w) {
    layout_item_count = 0;
    layout_hit_count = 0;
    layout_text_scratch[0] = '\0';

    struct layout_ctx ctx;
    ctx.left = content_x + 14;
    ctx.right = content_x + content_w - 18;
    if (ctx.right <= ctx.left + 40) ctx.right = ctx.left + 40;
    ctx.x = ctx.left;
    ctx.y = page_y + 9;
    ctx.line_start = 0;
    ctx.line_x0 = ctx.x;
    ctx.line_max = ctx.x;
    ctx.indent = 0;
    ctx.align = 0;
    ctx.space_pending = 0;
    ctx.max_y = ctx.y;

    struct layout_style base;
    base.fg = 0xFF20242B;
    base.bg = 0xFFFFFFFF;
    base.has_bg = 0;
    base.bold = 0;
    base.underline = 0;
    base.radius = 0;
    base.border = 0;
    base.pre = 0;
    base.align = 0;
    base.indent = 0;
    base.event_node = -1;
    base.link_node = -1;

    int body = dom_body_node();
    if (body >= 0) layout_node(body, base, &ctx, 0);
    else layout_node(dom_root, base, &ctx, 0);
    layout_flush(&ctx);
    content_height = ctx.max_y - page_y + 10;
    if (content_height < 30) content_height = 30;
    layout_build_hits();
    layout_width = content_w;
    layout_dirty = false;
}

/* ------------------------------------------------------------------ */
/* Painting                                                            */
/* ------------------------------------------------------------------ */

static void browser_layout(int x, int y, int w, int h,
                           struct browser_layout *L) {
    L->nav_y = y + 7;
    L->nav_h = 28;
    L->back_x = x + 10;
    L->back_w = 32;
    L->home_x = L->back_x + L->back_w + 5;
    L->home_w = 58;
    L->open_x = L->home_x + L->home_w + 5;
    L->open_w = 54;
    L->save_w = 62;
    L->settings_w = 70;
    L->go_w = 38;
    L->go_x = x + w - L->go_w - 10;
    L->settings_x = L->go_x - L->settings_w - 6;
    L->save_x = L->settings_x - L->save_w - 6;
    L->addr_x = L->open_x + L->open_w + 8;
    L->addr_w = L->save_x - 6 - L->addr_x;
    if (L->addr_w < 40) L->addr_w = 40;

    L->content_x = x + 12;
    L->content_y = y + TOOLBAR_H + 4;
    L->status_y = y + h - STATUS_H;
    L->content_w = w - 24;
    if (L->content_w < 40) L->content_w = 40;
    L->content_h = L->status_y - L->content_y - 4;
    if (L->content_h < 30) L->content_h = 30;
    L->page_y = L->content_y + PAGE_HEADER_H;
    L->page_h = L->content_h - PAGE_HEADER_H;
    if (L->page_h < 20) L->page_h = 20;
    view_page_h = L->page_h;
}

static void gfx_fill_round(int x, int y, int w, int h, int r,
                           uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r == 0) { graphics_fill_rect(x, y, w, h, color); return; }
    graphics_fill_rect(x + r, y, w - 2 * r, h, color);
    for (int i = 0; i < r; i++) {
        int yy = r - 1 - i;
        int cut = 0;
        while (cut < r && (r - cut) * (r - cut) + yy * yy > r * r) cut++;
        graphics_fill_rect(x + cut, y + i, w - 2 * cut, 1, color);
        graphics_fill_rect(x + cut, y + h - 1 - i, w - 2 * cut, 1, color);
    }
}

static void gfx_draw_round(int x, int y, int w, int h, int r,
                           uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r == 0) { graphics_draw_rect(x, y, w, h, color); return; }
    graphics_fill_rect(x + r, y, w - 2 * r, 1, color);
    graphics_fill_rect(x + r, y + h - 1, w - 2 * r, 1, color);
    graphics_fill_rect(x, y + r, 1, h - 2 * r, color);
    graphics_fill_rect(x + w - 1, y + r, 1, h - 2 * r, color);
    for (int i = 0; i < r; i++) {
        int yy = r - 1 - i;
        int cut = 0;
        while (cut < r && (r - cut) * (r - cut) + yy * yy > r * r) cut++;
        graphics_fill_rect(x + cut, y + i, 1, 1, color);
        graphics_fill_rect(x + w - 1 - cut, y + i, 1, 1, color);
        graphics_fill_rect(x + cut, y + h - 1 - i, 1, 1, color);
        graphics_fill_rect(x + w - 1 - cut, y + h - 1 - i, 1, 1, color);
    }
}

static void draw_button(int x, int y, int w, int h, const char *label,
                        int accent) {
    gfx_fill_round(x, y, w, h, 5,
                               accent ? 0xFF2E6FD8 : 0xFF2B3B55);
    gfx_draw_round(x, y, w, h, 5,
                               accent ? 0xFF70A8FF : 0xFF435C7C);
    int text_w = b_text_width(label);
    int tx = x + (w - text_w) / 2;
    if (tx < x + 2) tx = x + 2;
    graphics_draw_string(tx, y + (h - 16) / 2, label,
                         accent ? 0xFFFFFFFF : 0xFFE8F1FF);
}

static void draw_centered(int cx, int y, const char *text, uint32_t color) {
    int x = cx - b_text_width(text) / 2;
    graphics_draw_string(x, y, text, color);
}

static void compute_start_layout(const struct browser_layout *L,
                                 struct start_layout *S) {
    int cx = L->content_x + L->content_w / 2;
    S->logo_y = L->content_y + 28;
    int search_w = L->content_w - 96;
    if (search_w > 430) search_w = 430;
    if (search_w > L->content_w - 24) search_w = L->content_w - 24;
    if (search_w < 180) search_w = 180;
    S->search_w = search_w;
    S->search_h = 36;
    S->search_x = cx - search_w / 2;
    S->search_y = S->logo_y + 48;
    S->button_w = 150;
    S->button_h = 30;
    S->button_y = S->search_y + 50;
    S->button1_x = cx - 6 - S->button_w;
    S->button2_x = cx + 6;
    int tile_w = (L->content_w - 36) / START_SHORTCUTS;
    if (tile_w > 168) tile_w = 168;
    if (tile_w < 96) tile_w = 96;
    int total_w = tile_w * START_SHORTCUTS + 30;
    int tile_x = cx - total_w / 2;
    S->shortcut_w = tile_w;
    S->shortcut_h = 62;
    S->shortcut_y = S->button_y + S->button_h + 42;
    for (int i = 0; i < START_SHORTCUTS; i++)
        S->shortcut_x[i] = tile_x + i * (tile_w + 10);
}

static void draw_start_page(const struct browser_layout *L) {
    struct start_layout S;
    compute_start_layout(L, &S);
    int cx = L->content_x + L->content_w / 2;
    bool dark = browser_theme == THEME_DARK;

    graphics_fill_gradient_v(L->content_x, L->content_y, L->content_w,
                             L->content_h, dark ? 0xFF111827 : 0xFFFCFDFF,
                             dark ? 0xFF0B1220 : 0xFFF4F8FD);

    graphics_fill_circle(L->content_x + 58, L->content_y + 54, 26,
                         dark ? 0xFF1E293B : 0xFFE7F0FF);
    graphics_fill_circle(L->content_x + L->content_w - 54,
                         L->content_y + 96, 36,
                         dark ? 0xFF2B1F24 : 0xFFFCEBE9);
    graphics_fill_circle(L->content_x + L->content_w - 82,
                         L->content_y + 38, 15,
                         dark ? 0xFF2A2618 : 0xFFFFF6D8);
    graphics_fill_circle(L->content_x + 100, L->content_y + L->content_h - 42,
                         11, dark ? 0xFF16291F : 0xFFE8F6EC);

    const char *word = "Solis";
    uint32_t colors[5] = {0xFF4285F4, 0xFFEA4335, 0xFFFBBC05,
                          0xFF4285F4, 0xFF34A853};
    int spacing = 20;
    int word_w = (b_strlen(word) - 1) * spacing + CHAR_W;
    int lx = cx - word_w / 2;
    gfx_fill_round(lx - 20, S.logo_y - 10, word_w + 40, 54, 10,
                               dark ? 0xFF1F2937 : 0xFFF6F9FF);
    for (int i = 0; word[i]; i++) {
        char letter[2] = {word[i], '\0'};
        graphics_draw_string(lx + i * spacing, S.logo_y, letter, colors[i]);
    }
    draw_centered(cx, S.logo_y + 21, "Browser  -  a small window on the web",
                  dark ? 0xFF9CA3AF : 0xFF6B7280);

    gfx_fill_round(S.search_x + 2, S.search_y + 3, S.search_w - 4,
                               S.search_h, 17, dark ? 0xFF0F172A : 0xFFE9EEF7);
    gfx_fill_round(S.search_x, S.search_y, S.search_w, S.search_h,
                               17, dark ? 0xFF1F2937 : 0xFFFFFFFF);
    gfx_draw_round(S.search_x, S.search_y, S.search_w, S.search_h,
                               17, dark ? 0xFF374151 : 0xFFDADCE0);
    graphics_fill_circle(S.search_x + 20, S.search_y + 18, 5,
                         dark ? 0xFF9CA3AF : 0xFF9AA0A6);
    graphics_fill_circle(S.search_x + 20, S.search_y + 18, 2,
                         dark ? 0xFF1F2937 : 0xFFFFFFFF);
    graphics_fill_rect(S.search_x + 24, S.search_y + 22, 7, 2,
                       dark ? 0xFF9CA3AF : 0xFF9AA0A6);
    graphics_draw_string(S.search_x + 40, S.search_y + 10,
                         "Search the web or type an address",
                         dark ? 0xFF9CA3AF : 0xFF9AA0A6);

    gfx_fill_round(S.button1_x, S.button_y, S.button_w, S.button_h,
                               4, dark ? 0xFF1F2937 : 0xFFFFFFFF);
    gfx_draw_round(S.button1_x, S.button_y, S.button_w, S.button_h,
                               4, dark ? 0xFF374151 : 0xFFDADCE0);
    draw_centered(S.button1_x + S.button_w / 2, S.button_y + 7,
                  "Solis Search", dark ? 0xFFE5E7EB : 0xFF3C4043);
    gfx_fill_round(S.button2_x, S.button_y, S.button_w, S.button_h,
                               4, 0xFF4285F4);
    draw_centered(S.button2_x + S.button_w / 2, S.button_y + 7,
                  "Open local page", 0xFFFFFFFF);

    const char *labels[START_SHORTCUTS] = {
        "Wikipedia", "GitHub", "Example", "Surprise me", "Welcome"
    };
    const char *subs[START_SHORTCUTS] = {
        "article", "repo search", "example.com", "random article", "local HTML"
    };
    uint32_t icons[START_SHORTCUTS] = {
        0xFF34A853, 0xFF24292F, 0xFF4285F4, 0xFFFBBC05, 0xFFEA4335
    };
    for (int i = 0; i < START_SHORTCUTS; i++) {
        int x = S.shortcut_x[i];
        int y = S.shortcut_y;
        gfx_fill_round(x + 2, y + 3, S.shortcut_w, S.shortcut_h,
                                   7, dark ? 0xFF0B1220 : 0xFFE7ECF4);
        gfx_fill_round(x, y, S.shortcut_w, S.shortcut_h, 7,
                                   dark ? 0xFF1F2937 : 0xFFFFFFFF);
        gfx_draw_round(x, y, S.shortcut_w, S.shortcut_h, 7,
                                   dark ? 0xFF374151 : 0xFFE2E7EF);
        gfx_fill_round(x + 12, y + 12, 24, 24, 6, icons[i]);
        char letter[2] = {labels[i][0], '\0'};
        graphics_draw_string(x + 20, y + 16, letter, 0xFFFFFFFF);
        graphics_draw_string(x + 44, y + 12, labels[i],
                             dark ? 0xFFF3F4F6 : 0xFF202124);
        graphics_draw_string(x + 44, y + 30, subs[i],
                             dark ? 0xFF9CA3AF : 0xFF6B7280);
    }

    graphics_fill_rect(L->content_x + 24, L->content_y + L->content_h - 24,
                       L->content_w - 48, 1, dark ? 0xFF374151 : 0xFFE6EBF2);
    draw_centered(cx, L->content_y + L->content_h - 18,
                  "Solis Browser  -  local HTML, images and a little JavaScript",
                  dark ? 0xFF6B7280 : 0xFF8A94A6);
}

static void draw_address(const struct browser_layout *L) {
    gfx_fill_round(L->addr_x, L->nav_y, L->addr_w, L->nav_h, 7,
                               address_select_all ? 0xFF2C4F80 : 0xFF101A2B);
    gfx_draw_round(L->addr_x, L->nav_y, L->addr_w, L->nav_h, 7,
                               address_focus ? 0xFF79B0FF : 0xFF344D73);

    uint32_t badge = 0xFF7C8BA1;
    const char *badge_text = "WEB";
    if (b_starts_ci(page_url, "file://")) {
        badge = 0xFF34A853; badge_text = "LOCAL";
    } else if (b_starts_ci(page_url, "https://")) {
        badge = 0xFF4285F4; badge_text = "LOCK";
    } else if (b_starts_ci(page_url, "http://")) {
        badge = 0xFFF29900; badge_text = "HTTP";
    }
    graphics_fill_circle(L->addr_x + 14, L->nav_y + 14, 7, badge);
    if (b_starts_ci(page_url, "https://")) {
        graphics_fill_rect(L->addr_x + 11, L->nav_y + 12, 6, 6,
                           address_select_all ? 0xFFFFFFFF : 0xFF101A2B);
        graphics_fill_rect(L->addr_x + 12, L->nav_y + 9, 4, 4, badge);
    } else {
        char letter[2] = {badge_text[0], '\0'};
        graphics_draw_string(L->addr_x + 11, L->nav_y + 6, letter, 0xFFFFFFFF);
    }

    int text_x = L->addr_x + 28;
    int visible = (L->addr_w - 34) / CHAR_W;
    if (visible < 1) visible = 1;
    if (visible > 191) visible = 191;
    if (address_len == 0) {
        graphics_draw_string(text_x, L->nav_y + 6,
                             home_active ? "Search or type an address"
                                         : "Type an address",
                             0xFF7D8DA7);
        if (address_focus) graphics_fill_rect(text_x, L->nav_y + 6, 1, 16,
                                              0xFFEAF2FF);
        return;
    }

    int start = 0;
    if (!address_select_all && address_cursor >= visible)
        start = address_cursor - visible + 1;
    if (start > address_len) start = address_len;
    char tmp[192];
    int n = 0;
    while (start + n < address_len && n < visible && n < 191) {
        tmp[n] = address[start + n];
        n++;
    }
    n = b_utf8_trim(tmp, n);
    tmp[n] = '\0';
    graphics_draw_string(text_x, L->nav_y + 6, tmp,
                         address_select_all ? 0xFFFFFFFF : 0xFFE6EEFC);
    if (address_focus && !address_select_all) {
        int cx = text_x + (address_cursor - start) * CHAR_W;
        graphics_fill_rect(cx, L->nav_y + 6, 1, 16, 0xFFEAF2FF);
    }
}

static void draw_page_header(const struct browser_layout *L) {
    graphics_fill_gradient_v(L->content_x, L->content_y, L->content_w,
                             PAGE_HEADER_H, 0xFFFFFFFF, 0xFFF4F8FE);
    graphics_fill_rect(L->content_x, L->content_y + PAGE_HEADER_H - 1,
                       L->content_w, 1, 0xFFE2E8F1);

    if (!home_active && !page_loaded && !status_error) {
        graphics_draw_string(L->content_x + 14, L->content_y + 10,
                             "Page", 0xFF25324B);
        return;
    }
    const char *title = home_active ? "New tab" :
                        (page_title[0] ? page_title : "Untitled page");
    int max_chars = (L->content_w - 120) / CHAR_W;
    if (max_chars < 4) max_chars = 4;
    char tmp[96];
    int n = 0;
    while (title[n] && n < max_chars && n < 95) { tmp[n] = title[n]; n++; }
    n = b_utf8_trim(tmp, n);
    tmp[n] = '\0';

    uint32_t badge = 0xFF4285F4;
    if (b_starts_ci(page_url, "file://")) badge = 0xFF34A853;
    else if (b_starts_ci(page_url, "http://")) badge = 0xFFF29900;
    gfx_fill_round(L->content_x + 10, L->content_y + 7, 20, 20,
                               5, badge);
    char badge_letter = 'S';
    if (b_starts_ci(page_url, "file://")) badge_letter = 'L';
    else if (b_starts_ci(page_url, "http://")) badge_letter = 'H';
    else if (b_starts_ci(page_url, "https://")) badge_letter = 'W';
    char letter[2] = {badge_letter, '\0'};
    graphics_draw_string(L->content_x + 16, L->content_y + 11, letter,
                         0xFFFFFFFF);
    graphics_draw_string(L->content_x + 38, L->content_y + 6, tmp, 0xFF1F2D44);

    const char *right = "";
    if (home_active) right = "start page";
    else if (b_starts_ci(page_url, "file://")) right = "local file";
    else if (b_starts_ci(page_url, "https://")) right = "secure connection";
    else if (b_starts_ci(page_url, "http://")) right = "unencrypted";
    int rw = b_text_width(right);
    if (rw > 0 && L->content_w > rw + 40)
        graphics_draw_string(L->content_x + L->content_w - rw - 10,
                             L->content_y + 7, right, 0xFF8793A6);

    if (image_pending_count() > 0 && !home_active) {
        int pct = image_used_count() > 0
                      ? (image_used_count() - image_pending_count()) * 100 /
                            image_used_count()
                      : 0;
        graphics_fill_rect(L->content_x, L->content_y + PAGE_HEADER_H - 3,
                           L->content_w, 2, 0xFFE6ECF5);
        int pw = L->content_w * pct / 100;
        if (pw < 6) pw = 6;
        graphics_fill_rect(L->content_x, L->content_y + PAGE_HEADER_H - 3,
                           (uint32_t)pw, 2, 0xFF4285F4);
    } else if (!home_active) {
        graphics_fill_rect(L->content_x, L->content_y + PAGE_HEADER_H - 3,
                           L->content_w, 1, 0xFFE6ECF5);
    }
}

static void draw_layout_item(const struct layout_item *it) {
    if (it->kind == LAY_BG || it->kind == LAY_RULE || it->kind == LAY_BAR ||
        it->kind == LAY_BULLET)
        return;
    if (it->kind == LAY_TEXT) {
        if (it->flags & LAY_F_BG)
            graphics_fill_rect(it->x, it->y, it->w, it->h, it->bg);
        char tmp[192];
        int n = it->tlen;
        if (n > 191) n = 191;
        for (int i = 0; i < n; i++) tmp[i] = it->text[i];
        n = b_utf8_trim(tmp, n);
        tmp[n] = '\0';
        uint32_t color = it->fg;
        graphics_draw_string(it->x, it->y, tmp, color);
        if (it->flags & LAY_F_BOLD)
            graphics_draw_string(it->x + 1, it->y, tmp, color);
        if (it->flags & LAY_F_UNDERLINE)
            graphics_fill_rect(it->x, it->y + 15, it->w, 1, color);
    } else if (it->kind == LAY_IMAGE) {
        gfx_fill_round(it->x + 1, it->y + 2, it->w, it->h, 5,
                                   0xFFE4EAF3);
        if (it->image >= 0 && images[it->image].state == 2) {
            graphics_blit_rgb_scaled(it->x, it->y, it->w, it->h,
                                     image_pixels[it->image],
                                     images[it->image].w,
                                     images[it->image].h);
            gfx_draw_round(it->x, it->y, it->w, it->h, 5,
                                       0xFFD6DEE9);
        } else {
            gfx_fill_round(it->x, it->y, it->w, it->h, 5,
                                       it->image >= 0 &&
                                               images[it->image].state == 1
                                           ? 0xFFF0F6FF
                                           : 0xFFF3F4F6);
            gfx_draw_round(it->x, it->y, it->w, it->h, 5,
                                       0xFFD6DEE9);
            if (it->image >= 0 && images[it->image].state == 1) {
                int dots = (anim_phase / 3) & 3;
                int bx = it->x + it->w / 2 - 18;
                for (int d = 0; d < 3; d++) {
                    uint32_t c = d == dots ? 0xFF4285F4 : 0xFFC9D8EE;
                    graphics_fill_circle(bx + d * 12, it->y + it->h / 2 - 6, 3, c);
                }
                graphics_draw_string(it->x + 10, it->y + it->h / 2 + 10,
                                     "Loading image...", 0xFF6B7C96);
            } else {
                graphics_draw_string(it->x + 10, it->y + 12,
                                     it->tlen ? it->text : "Image",
                                     0xFF7A8496);
                graphics_draw_string(it->x + 10, it->y + 26,
                                     "BMP, PNG, JPEG, GIF, ICO", 0xFFA0A8B5);
                graphics_draw_string(it->x + 10, it->y + 38,
                                     "TGA, PNM, QOI, XBM", 0xFFA0A8B5);
            }
        }
    } else if (it->kind == LAY_BUTTON) {
        gfx_fill_round(it->x, it->y, it->w, it->h, 6,
                                   0xFF2E6FD8);
        gfx_draw_round(it->x, it->y, it->w, it->h, 6,
                                   0xFF1B55AE);
        char tmp[64];
        int n = it->tlen < 63 ? it->tlen : 63;
        for (int i = 0; i < n; i++) tmp[i] = it->text[i];
        n = b_utf8_trim(tmp, n);
        tmp[n] = '\0';
        int tx = it->x + (it->w - b_text_width(tmp)) / 2;
        if (tx < it->x + 4) tx = it->x + 4;
        graphics_draw_string(tx, it->y + 6, tmp, 0xFFFFFFFF);
    } else if (it->kind == LAY_INPUT) {
        int focused = form_input_node == it->node;
        gfx_fill_round(it->x, it->y, it->w, it->h, 4, 0xFFFFFFFF);
        gfx_draw_round(it->x, it->y, it->w, it->h, 4,
                                   focused ? 0xFF4285F4 : 0xFFCAD3DF);
        char tmp[64];
        int n = it->tlen < 63 ? it->tlen : 63;
        for (int i = 0; i < n; i++) tmp[i] = it->text[i];
        tmp[n] = '\0';
        graphics_draw_string(it->x + 7, it->y + 5, tmp, it->fg);
    }
}

static void draw_page_content(const struct browser_layout *L) {
    int page_y = L->page_y;
    int page_h = L->page_h;
    int content_w = L->content_w - 2;
    if (content_w < 40) content_w = 40;

    if (layout_dirty || layout_width != content_w)
        layout_build(L->content_x + 1, page_y, content_w);
    apply_pending_anchor(page_y);
    int max_scroll = content_height - page_h;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_y > max_scroll) scroll_y = max_scroll;
    if (scroll_y < 0) scroll_y = 0;

    uint32_t paper = 0xFFFFFFFF;
    int body = dom_find_body_node();
    if (body >= 0 && dom_nodes[body].has_bg) paper = dom_nodes[body].bg;
    graphics_fill_rect(L->content_x, page_y, L->content_w, page_h, paper);

    for (int i = 0; i < layout_item_count; i++) {
        if (layout_items[i].kind != LAY_BG) continue;
        struct layout_item *it = &layout_items[i];
        int sy = it->y - scroll_y;
        if (sy < page_y || sy + it->h > page_y + page_h) continue;
        if (it->radius)
            gfx_fill_round(it->x, sy, it->w, it->h, it->radius, it->bg);
        else
            graphics_fill_rect(it->x, sy, it->w, it->h, it->bg);
        if (it->border) {
            if (it->radius)
                gfx_draw_round(it->x, sy, it->w, it->h, it->radius, it->border);
            else
                graphics_draw_rect(it->x, sy, it->w, it->h, it->border);
        }
    }
    for (int i = 0; i < layout_item_count; i++) {
        struct layout_item *it = &layout_items[i];
        if (it->kind == LAY_BG) continue;
        int sy = it->y - scroll_y;
        if (sy < page_y || sy + it->h > page_y + page_h) continue;
        struct layout_item shifted = *it;
        shifted.y = (int16_t)sy;
        if (shifted.kind == LAY_RULE || shifted.kind == LAY_BAR) {
            graphics_fill_rect(shifted.x, shifted.y, shifted.w, shifted.h,
                               shifted.bg);
            continue;
        }
        if (shifted.kind == LAY_BULLET) {
            graphics_fill_circle(shifted.x + 2, shifted.y, 2, shifted.bg);
            continue;
        }
        draw_layout_item(&shifted);
    }

    if (layout_item_count == 0 && !home_active)
        graphics_draw_string(L->content_x + 16, page_y + 14, "Empty page.",
                             0xFF7B8494);

    if (content_height > page_h) {
        int track_x = L->content_x + L->content_w - 7;
        int thumb_h = page_h * page_h / content_height;
        if (thumb_h < 20) thumb_h = 20;
        if (thumb_h > page_h) thumb_h = page_h;
        int span = page_h - thumb_h;
        int thumb_y = page_y;
        if (max_scroll > 0) thumb_y += span * scroll_y / max_scroll;
        graphics_fill_rect(track_x, page_y, 3, page_h, 0xFFEEF2F7);
        gfx_fill_round(track_x, thumb_y, 3, thumb_h, 1,
                                   0xFF9AA8BC);
    }
    graphics_fill_rect(L->content_x, page_y, 1, page_h, 0xFFE8EDF4);
    graphics_fill_rect(L->content_x + L->content_w - 1, page_y, 1, page_h,
                       0xFFE8EDF4);
}

void browser_draw(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    struct browser_layout L;
    browser_layout(x, y, w, h, &L);

    graphics_fill_rect(x, y, w, h, 0xFFF7F9FC);
    graphics_fill_gradient_v(x, y, w, TOOLBAR_H, 0xFF22334E, 0xFF172238);
    graphics_fill_rect(x, y + TOOLBAR_H - 1, w, 1, 0xFF344A69);

    draw_button(L.back_x, L.nav_y, L.back_w, L.nav_h, "<", 0);
    draw_button(L.home_x, L.nav_y, L.home_w, L.nav_h, "Home", 0);
    draw_button(L.open_x, L.nav_y, L.open_w, L.nav_h, "Open", 0);
    draw_button(L.save_x, L.nav_y, L.save_w, L.nav_h, "Save", 0);
    draw_button(L.settings_x, L.nav_y, L.settings_w, L.nav_h, "Settings", 0);
    draw_address(&L);
    draw_button(L.go_x, L.nav_y, L.go_w, L.nav_h, "Go", 1);

    if (home_active) {
        draw_start_page(&L);
    } else {
        draw_page_header(&L);
        draw_page_content(&L);
    }

    graphics_fill_gradient_v(x, L.status_y, w, STATUS_H, 0xFF1A263B,
                             0xFF131D2E);
    const char *text = status[0] ? status : "Ready";
    uint32_t color = status_error ? 0xFFF08A80 : 0xFFB8C7DD;
    if (!net_available()) {
        text = "Network disconnected";
        color = 0xFFF08A80;
    }
    char tmp[192];
    int max_chars = (w - 160) / CHAR_W;
    if (max_chars > 191) max_chars = 191;
    if (max_chars < 1) max_chars = 1;
    int n = 0;
    while (text[n] && n < max_chars) { tmp[n] = text[n]; n++; }
    n = b_utf8_trim(tmp, n);
    tmp[n] = '\0';
    graphics_draw_string(x + 10, L.status_y + 1, tmp, color);

    char right[64];
    if (page_loaded) {
        b_copy(right, sizeof(right), page_bytes_status);
    } else {
        b_copy(right, sizeof(right), net_available() ? "online" : "offline");
    }
    int rw = b_text_width(right);
    if (rw < w - 30)
        graphics_draw_string(x + w - rw - 10, L.status_y + 1, right,
                             0xFF8FA3BF);
}

/* ------------------------------------------------------------------ */
/* Navigation                                                          */
/* ------------------------------------------------------------------ */

static void download_name_from_url(const char *url, char *out, int max,
                                    const char *suffix) {
    out[0] = '\0';
    if (!url) return;
    int end = b_strlen(url);
    while (end > 0 && (url[end - 1] == '/' || url[end - 1] == '\n' ||
                       url[end - 1] == '\r')) end--;
    int query = end;
    for (int i = 0; i < end; i++)
        if (url[i] == '?' || url[i] == '#') { query = i; break; }
    int start = 0;
    for (int i = 0; i < query; i++)
        if (url[i] == '/') start = i + 1;
    int oi = 0;
    for (int i = start; i < query && oi < max - 1; i++) {
        char c = url[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')
            out[oi++] = c;
        else if (c == ' ')
            out[oi++] = '_';
        if (oi >= 48) break;
    }
    out[oi] = '\0';
    if (!out[0]) b_copy(out, max, "download.bin");
    if (suffix && suffix[0]) {
        int has_ext = 0;
        for (int i = 0; out[i]; i++) if (out[i] == '.') has_ext = 1;
        if (!has_ext) b_append(out, max, suffix);
    }
}

static int looks_like_download(const char *url) {
    if (!url) return 0;
    const char *names[] = {
        ".zip", ".pdf", ".gz", ".tgz", ".tar", ".xz", ".7z", ".rar",
        ".exe", ".msi", ".iso", ".img", ".mp3", ".wav", ".ogg", ".mp4",
        ".mkv", ".jpg", ".jpeg", ".png", ".gif", ".webp", ".txt", ".csv"
    };
    int len = b_strlen(url);
    for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
        int n = b_strlen(names[i]);
        if (len >= n && b_equal_ci(url + len - n, names[i])) return 1;
    }
    return 0;
}

static int download_url(const char *url, const char *suggested,
                        const char *suffix) {
    if (!url || !url[0]) return -1;
    int bytes = fetch_url(url, response, RESPONSE_MAX);
    if (bytes <= 0) return -1;
    if (vfs_open("/home/downloads") < 0) vfs_mkdir("/home/downloads");

    char name[64];
    if (suggested && suggested[0]) b_copy(name, sizeof(name), suggested);
    else download_name_from_url(url, name, sizeof(name), suffix);
    for (int i = 0; name[i]; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            name[i] = '_';
    }
    if (!name[0]) b_copy(name, sizeof(name), "download.bin");

    char path[96];
    char candidate[64];
    int fd = -1;
    for (int attempt = 0; attempt < 100; attempt++) {
        b_copy(candidate, sizeof(candidate), name);
        if (attempt > 0) {
            int dot = -1;
            for (int i = 0; candidate[i]; i++) if (candidate[i] == '.') dot = i;
            char suffix_num[12];
            suffix_num[0] = '-';
            suffix_num[1] = '\0';
            b_append_int(suffix_num, sizeof(suffix_num), attempt);
            if (dot > 0 && dot < (int)sizeof(candidate) - 8) {
                char ext[16];
                b_copy(ext, sizeof(ext), candidate + dot);
                candidate[dot] = '\0';
                b_append(candidate, sizeof(candidate), suffix_num);
                b_append(candidate, sizeof(candidate), ext);
            } else {
                b_append(candidate, sizeof(candidate), suffix_num);
            }
        }
        b_copy(path, sizeof(path), "/home/downloads/");
        b_append(path, sizeof(path), candidate);
        int existing = vfs_open(path);
        if (existing < 0) {
            fd = vfs_create(path);
            if (fd >= 0) break;
        }
    }
    if (fd < 0) return -1;
    int written = vfs_write(fd, response, (uint32_t)bytes);
    if (written < 0 || written != bytes) return -1;

    char msg[180];
    b_copy(msg, sizeof(msg), "Downloaded to ");
    b_append(msg, sizeof(msg), path);
    b_append(msg, sizeof(msg), " (");
    b_append_int(msg, sizeof(msg), bytes);
    b_append(msg, sizeof(msg), " bytes)");
    set_status(msg, false);
    return 0;
}

static void browser_download_current(void) {
    if (home_active || !page_url[0]) {
        set_status("Open a page before saving it.", true);
        return;
    }
    if (b_starts_ci(page_url, "solis://")) {
        set_status("This built-in page cannot be downloaded.", true);
        return;
    }
    char name[64];
    char suggested[64];
    download_name_from_url(page_url, name, sizeof(name), ".html");
    int dot = 0;
    for (int i = 0; name[i]; i++) if (name[i] == '.') dot = 1;
    b_copy(suggested, sizeof(suggested), name);
    if (!dot) b_append(suggested, sizeof(suggested), ".html");
    if (download_url(page_url, suggested, ".html") < 0)
        set_status("Download failed (network, disk full, or read-only).", true);
}

static bool load_local_file(const char *path, const char *url, bool push_history) {
    int fd = vfs_open(path);
    if (fd < 0) {
        set_status("Local file not found.", true);
        show_error("File not found", path);
        return false;
    }
    int size = vfs_get_size(fd);
    if (size < 0) {
        set_status("Could not read local file.", true);
        show_error("Could not read file", path);
        return false;
    }
    if (size > RESPONSE_MAX) size = RESPONSE_MAX;
    int bytes = size > 0 ? vfs_read(fd, response, (uint32_t)size) : 0;
    if (bytes < 0) bytes = 0;
    response[bytes] = '\0';
    load_document((const char *)response, bytes, url, push_history);
    return true;
}

/* ------------------------------------------------------------------ */
/* Search results                                                      */
/* ------------------------------------------------------------------ */

static void url_encode_text(const char *src, char *out, int max) {
    static const char hex[] = "0123456789ABCDEF";
    int oi = 0;
    for (int i = 0; src && src[i] && oi < max - 1; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            out[oi++] = (char)c;
        } else if (c == ' ') {
            out[oi++] = '+';
        } else if (oi + 3 <= max - 1) {
            out[oi++] = '%';
            out[oi++] = hex[c >> 4];
            out[oi++] = hex[c & 15];
        }
    }
    out[oi] = '\0';
}

static void url_decode_text(const char *src, char *out, int max) {
    int oi = 0;
    for (int i = 0; src && src[i] && oi < max - 1; i++) {
        if (src[i] == '+' ) {
            out[oi++] = ' ';
        } else if (src[i] == '%' && src[i + 1] && src[i + 2]) {
            int hi = dom_hex(src[i + 1]);
            int lo = dom_hex(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out[oi++] = (char)((hi << 4) | lo);
                i += 2;
            } else {
                out[oi++] = src[i];
            }
        } else {
            out[oi++] = src[i];
        }
    }
    out[oi] = '\0';
}

static void request_anchor_from_url(const char *url) {
    anchor_pending = false;
    pending_anchor[0] = '\0';
    if (!url) return;
    int cut = -1;
    for (int i = 0; url[i]; i++) {
        if (url[i] == '#') { cut = i; break; }
    }
    if (cut < 0 || !url[cut + 1]) return;
    url_decode_text(url + cut + 1, pending_anchor, sizeof(pending_anchor));
    if (pending_anchor[0]) anchor_pending = true;
}

static int search_provider_from_url(const char *url, char *query, int max) {
    const char *prefixes[] = {
        "solis://search?q=", "solis://wikipedia?q=", "solis://github?q="
    };
    for (int p = SEARCH_WEB; p <= SEARCH_GITHUB; p++) {
        if (b_starts_ci(url, prefixes[p])) {
            url_decode_text(url + b_strlen(prefixes[p]), query, max);
            return query[0] ? p : -1;
        }
    }
    return -1;
}

static int url_query_param(const char *url, const char *key,
                           char *out, int max) {
    int n = b_strlen(key);
    for (int i = 0; url[i]; i++) {
        if (url[i] != '?' && url[i] != '&') continue;
        int start = i + 1;
        int same = 1;
        for (int j = 0; j < n; j++) {
            if (!url[start + j] || url[start + j] != key[j]) { same = 0; break; }
        }
        if (!same || url[start + n] != '=') continue;
        int value = start + n + 1;
        int end = value;
        while (url[end] && url[end] != '&' && url[end] != '#') end++;
        char raw[SEARCH_MAX];
        int ri = 0;
        for (int j = value; j < end && ri < (int)sizeof(raw) - 1; j++)
            raw[ri++] = url[j];
        raw[ri] = '\0';
        url_decode_text(raw, out, max);
        return out[0] != '\0';
    }
    return 0;
}

static int wikipedia_render_url(const char *url, char *out, int max) {
    const char *scheme = 0;
    if (b_starts_ci(url, "https://")) scheme = url + 8;
    else if (b_starts_ci(url, "http://")) scheme = url + 7;
    if (!scheme) return 0;
    const char *slash = scheme;
    while (*slash && *slash != '/') slash++;
    if (!*slash || !b_starts_ci(slash, "/wiki/")) return 0;
    const char *title = slash + 6;
    const char *end = title;
    while (*end && *end != '?' && *end != '#') end++;
    if (end == title) return 0;
    for (const char *p = title; p < end; p++)
        if (*p == ':') return 0;
    char raw_title[512];
    int n = (int)(end - title);
    if (n > (int)sizeof(raw_title) - 1) n = (int)sizeof(raw_title) - 1;
    for (int i = 0; i < n; i++) raw_title[i] = title[i];
    raw_title[n] = '\0';
    char encoded[768];
    url_encode_text(raw_title, encoded, sizeof(encoded));

    b_copy(out, max, "https://");
    char host[128];
    int host_len = (int)(slash - scheme);
    if (host_len > (int)sizeof(host) - 1) host_len = (int)sizeof(host) - 1;
    for (int i = 0; i < host_len; i++) host[i] = scheme[i];
    host[host_len] = '\0';
    b_append(out, max, host);
    b_append(out, max, "/w/index.php?title=");
    b_append(out, max, encoded);
    b_append(out, max, "&action=render");
    return 1;
}

static bool looks_like_search(const char *input) {
    char tmp[SEARCH_MAX];
    b_trim_copy(tmp, sizeof(tmp), input);
    if (!tmp[0]) return false;
    if (b_starts_ci(tmp, "http://") || b_starts_ci(tmp, "https://") ||
        b_starts_ci(tmp, "file://") || b_starts_ci(tmp, "solis://"))
        return false;
    if (tmp[0] == '/' || tmp[0] == '.' || tmp[0] == '?')
        return false;
    for (int i = 0; tmp[i]; i++) {
        if (b_is_space(tmp[i])) return true;
        if (tmp[i] == '.' || tmp[i] == ':' || tmp[i] == '/')
            return false;
    }
    return true;
}

static int json_object_end(const char *start, const char *end) {
    int depth = 0;
    int quoted = 0;
    int escaped = 0;
    const char *p = start;
    for (; p < end; p++) {
        char c = *p;
        if (quoted) {
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
            else if (c == '"') quoted = 0;
            continue;
        }
        if (c == '"') quoted = 1;
        else if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0) return (int)(p - start);
        }
    }
    return -1;
}

static const char *json_field(const char *obj, const char *end, const char *key) {
    int n = b_strlen(key);
    for (const char *p = obj; p + n + 2 < end; p++) {
        if (*p != '"') continue;
        int same = 1;
        for (int i = 0; i < n; i++)
            if (p[1 + i] != key[i]) { same = 0; break; }
        if (!same || p[1 + n] != '"') continue;
        const char *q = p + n + 2;
        while (q < end && b_is_space(*q)) q++;
        if (q < end && *q == ':') return q + 1;
    }
    return 0;
}

static int json_string(const char *obj, const char *end, const char *key,
                       char *out, int max) {
    const char *p = json_field(obj, end, key);
    if (!p || p >= end) return 0;
    while (p < end && b_is_space(*p)) p++;
    if (p >= end || *p != '"') return 0;
    p++;
    int oi = 0;
    while (p < end && *p != '"') {
        char c = *p++;
        if (c == '\\' && p < end) {
            c = *p++;
            if (c == 'n' || c == 'r' || c == 't') c = ' ';
            else if (c == 'u' && p + 3 < end) {
                int value = 0;
                int ok = 1;
                for (int i = 0; i < 4; i++) {
                    int d = dom_hex(*p++);
                    if (d < 0) { ok = 0; break; }
                    value = (value << 4) | d;
                }
                char enc[4];
                int en = 0;
                if (ok) en = b_utf8_encode((uint32_t)value, enc);
                if (en == 0) { enc[0] = '?'; en = 1; }
                for (int i = 0; i < en && oi < max - 1; i++)
                    out[oi++] = enc[i];
                continue;
            }
            if (c == '\\' || c == '"' || c == '/') { /* keep escaped char */ }
            else if (c == 'b' || c == 'f') c = ' ';
        }
        if ((unsigned char)c >= 0x80) c = ' ';
        if (oi < max - 1) out[oi++] = c;
    }
    out[oi] = '\0';
    return oi > 0;
}

static int json_int(const char *obj, const char *end, const char *key) {
    const char *p = json_field(obj, end, key);
    if (!p || p >= end) return 0;
    while (p < end && b_is_space(*p)) p++;
    int value = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        p++;
    }
    return value;
}

static void search_strip_html(const char *src, char *out, int max) {
    int oi = 0;
    int skip = 0;
    for (int i = 0; src && src[i] && oi < max - 1;) {
        if (src[i] == '<') { skip = 1; i++; continue; }
        if (skip) {
            if (src[i] == '>') skip = 0;
            i++;
            continue;
        }
        if (src[i] == '&') {
            int used = 0;
            char e[4];
            int en = html_entity(src + i, b_strlen(src + i), &used, e);
            if (used > 0) {
                for (int k = 0; k < en && oi < max - 1; k++)
                    out[oi++] = e[k];
                i += used;
                continue;
            }
        }
        out[oi++] = src[i++];
    }
    out[oi] = '\0';
}

static int github_parse_json(const char *json, int len) {
    int count = 0;
    const char *end = json + len;
    const char *p = json_field(json, end, "items");
    if (!p) return 0;
    while (p < end && b_is_space(*p)) p++;
    if (p >= end || *p != '[') return 0;
    p++;
    while (p < end && count < SEARCH_MAX_RESULTS) {
        /* Results may be an array or an object keyed by id, so skip to the
         * next object instead of assuming a bare array. */
        while (p < end && *p != '{' && *p != ']') p++;
        if (p >= end || *p != '{') break;
        int rel = json_object_end(p, end);
        if (rel < 0) break;
        const char *obj_end = p + rel;
        struct search_result *r = &search_results[count];
        r->title[0] = r->url[0] = r->domain[0] = r->snippet[0] = r->image[0] = '\0';
        json_string(p, obj_end, "full_name", r->title, sizeof(r->title));
        json_string(p, obj_end, "html_url", r->url, sizeof(r->url));
        json_string(p, obj_end, "description", r->snippet, sizeof(r->snippet));
        json_string(p, obj_end, "avatar_url", r->image, sizeof(r->image));
        b_copy(r->domain, sizeof(r->domain), "github.com");
        if (r->snippet[0]) b_append(r->snippet, sizeof(r->snippet), "  |  ");
        b_append(r->snippet, sizeof(r->snippet), "Stars: ");
        b_append_int(r->snippet, sizeof(r->snippet),
                     json_int(p, obj_end, "stargazers_count"));
        char language[64];
        language[0] = '\0';
        json_string(p, obj_end, "language", language, sizeof(language));
        if (language[0]) {
            b_append(r->snippet, sizeof(r->snippet), "  |  Language: ");
            b_append(r->snippet, sizeof(r->snippet), language);
        }
        if (r->url[0]) count++;
        p = obj_end + 1;
    }
    return count;
}

static int github_home_page(const char *url, bool push_history) {
    if (!github_home_url(url)) return 0;
    search_html[0] = '\0';
    b_append(search_html, SEARCH_HTML_MAX,
             "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
             "<title>GitHub</title></head>"
             "<body style=\"background:#0d1117;color:#e6edf3;margin:0;\">"
             "<div style=\"background:#161b22;border-bottom:1px solid #30363d;padding:8px;\">"
             "<div style=\"font-size:22px;font-weight:bold;\">GitHub</div>"
             "<form action=\"solis://github\" method=\"get\">"
             "<input type=\"search\" name=\"q\" placeholder=\"Search GitHub\">"
             "<button type=\"submit\">Search</button></form></div>"
             "<div style=\"padding:10px;\">"
             "<h1>Good evening</h1>"
             "<div style=\"background:#161b22;border:1px solid #30363d;padding:10px;\">"
             "<b>Ask anything or type # to add context</b>"
             "<p style=\"color:#8b949e;\">Search repositories, issues, and people.</p>"
             "</div>"
             "<h2>Top repositories</h2>"
             "<div style=\"background:#161b22;border:1px solid #30363d;padding:10px;\">"
             "<p><a href=\"https://github.com/torvalds/linux\">torvalds/linux</a> "
             "<span style=\"color:#8b949e;\">Linux kernel source tree</span></p>"
             "<p><a href=\"https://github.com/microsoft/vscode\">microsoft/vscode</a> "
             "<span style=\"color:#8b949e;\">Visual Studio Code</span></p>"
             "<p><a href=\"https://github.com/facebook/react\">facebook/react</a> "
             "<span style=\"color:#8b949e;\">The library for web UIs</span></p>"
             "<p><a href=\"https://github.com/python/cpython\">python/cpython</a> "
             "<span style=\"color:#8b949e;\">The Python programming language</span></p>"
             "<p><a href=\"https://github.com/rust-lang/rust\">rust-lang/rust</a> "
             "<span style=\"color:#8b949e;\">Empowering everyone to build reliable software</span></p>"
             "</div>"
             "<div style=\"background:#161b22;border:1px solid #30363d;padding:10px;margin-top:10px;\">"
             "<h2>Latest from the changelog</h2>"
             "<p style=\"color:#8b949e;\">GitHub Copilot updates, enterprise releases, and platform news.</p>"
             "</div></div></body></html>");
    load_document(search_html, b_strlen(search_html), url, push_history);
    set_status("GitHub home ready.", false);
    return 1;
}

static int github_repo_slug(const char *url, char *owner, int owner_max,
                            char *repo, int repo_max) {
    const char prefix[] = "https://github.com/";
    if (!b_starts_ci(url, prefix)) return 0;
    const char *p = url + (int)sizeof(prefix) - 1;
    int oi = 0;
    while (*p && *p != '/' && *p != '?' && *p != '#' && oi < owner_max - 1)
        owner[oi++] = *p++;
    owner[oi] = '\0';
    if (oi == 0 || *p != '/') return 0;
    p++;
    int ri = 0;
    while (*p && *p != '/' && *p != '?' && *p != '#' && ri < repo_max - 1)
        repo[ri++] = *p++;
    repo[ri] = '\0';
    if (ri == 0) return 0;
    if (*p == '/') {
        p++;
        if (*p && *p != '#' && *p != '?') return 0;
    }
    const char *reserved[] = { "search", "topics", "login", "settings",
                               "marketplace", "explore", "notifications",
                               "new", "orgs", "users", "pulls", "issues",
                               "sponsors", "collections", "events" };
    for (int i = 0; i < (int)(sizeof(reserved) / sizeof(reserved[0])); i++)
        if (b_equal_ci(owner, reserved[i])) return 0;
    return 1;
}

/* GitHub repository pages are far too script- and asset-heavy for this small
 * engine, so render the public repository metadata through the API instead.
 * The synthetic page still uses the GitHub theme and keeps the original URL. */
static int github_repo_page(const char *url, bool push_history) {
    char owner[96], repo[160];
    if (!github_repo_slug(url, owner, sizeof(owner), repo, sizeof(repo))) return 0;
    char api[MAX_URL];
    b_copy(api, sizeof(api), "https://api.github.com/repos/");
    b_append(api, sizeof(api), owner);
    b_append(api, sizeof(api), "/");
    b_append(api, sizeof(api), repo);
    set_status("Loading repository...", false);
    int bytes = fetch_url(api, response, RESPONSE_MAX);
    if (bytes <= 0) {
        set_status("Could not load repository.", true);
        show_error("GitHub repository unavailable",
                   "The GitHub API did not answer. Check the connection and try again.");
        return 1;
    }
    response[bytes] = '\0';
    const char *end = (const char *)response + bytes;
    char full_name[192], description[1024], language[64], branch[64];
    char license[64], html_url[512], homepage[384], avatar[512];
    full_name[0] = description[0] = language[0] = branch[0] = license[0] = '\0';
    html_url[0] = homepage[0] = avatar[0] = '\0';
    json_string((const char *)response, end, "full_name", full_name, sizeof(full_name));
    json_string((const char *)response, end, "description", description, sizeof(description));
    json_string((const char *)response, end, "language", language, sizeof(language));
    json_string((const char *)response, end, "default_branch", branch, sizeof(branch));
    json_string((const char *)response, end, "spdx_id", license, sizeof(license));
    json_string((const char *)response, end, "html_url", html_url, sizeof(html_url));
    json_string((const char *)response, end, "homepage", homepage, sizeof(homepage));
    json_string((const char *)response, end, "avatar_url", avatar, sizeof(avatar));
    int stars = json_int((const char *)response, end, "stargazers_count");
    int forks = json_int((const char *)response, end, "forks_count");
    int issues = json_int((const char *)response, end, "open_issues_count");
    if (!full_name[0]) {
        b_copy(full_name, sizeof(full_name), owner);
        b_append(full_name, sizeof(full_name), "/");
        b_append(full_name, sizeof(full_name), repo);
    }
    char esc_name[512], esc_desc[3072], esc_lang[256], esc_license[256];
    char esc_branch[256], esc_url[1024], esc_home[1024], esc_avatar[1024];
    html_escape(full_name, esc_name, sizeof(esc_name));
    html_escape(description, esc_desc, sizeof(esc_desc));
    html_escape(language, esc_lang, sizeof(esc_lang));
    html_escape(license, esc_license, sizeof(esc_license));
    html_escape(branch, esc_branch, sizeof(esc_branch));
    html_escape(html_url[0] ? html_url : url, esc_url, sizeof(esc_url));
    html_escape(homepage, esc_home, sizeof(esc_home));
    html_escape(avatar, esc_avatar, sizeof(esc_avatar));

    /* The repository page also shows the beginning of README.md. Fetch it
     * after all API fields have been copied because it reuses the response
     * buffer. */
    char readme_raw[8001];
    readme_raw[0] = '\0';
    if (branch[0]) {
        const char *readme_names[] = { "README.md", "README", "README.rst" };
        for (int attempt = 0; attempt < 3 && !readme_raw[0]; attempt++) {
            char readme_url[MAX_URL];
            b_copy(readme_url, sizeof(readme_url), "https://raw.githubusercontent.com/");
            b_append(readme_url, sizeof(readme_url), owner);
            b_append(readme_url, sizeof(readme_url), "/");
            b_append(readme_url, sizeof(readme_url), repo);
            b_append(readme_url, sizeof(readme_url), "/");
            b_append(readme_url, sizeof(readme_url), branch);
            b_append(readme_url, sizeof(readme_url), "/");
            b_append(readme_url, sizeof(readme_url), readme_names[attempt]);
            int readme_bytes = fetch_url(readme_url, response, RESPONSE_MAX);
            if (readme_bytes <= 0) continue;
            response[readme_bytes] = '\0';
            if (b_find_ci((const char *)response, 0, readme_bytes,
                          "404: Not Found") >= 0) continue;
            int copy = readme_bytes < 8000 ? readme_bytes : 8000;
            for (int i = 0; i < copy; i++)
                readme_raw[i] = (char)response[i];
            readme_raw[copy] = '\0';
        }
    }
    char readme_esc[12000];
    html_escape(readme_raw, readme_esc, sizeof(readme_esc));

    search_html[0] = '\0';
    b_append(search_html, SEARCH_HTML_MAX,
             "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
             "<title>");
    b_append(search_html, SEARCH_HTML_MAX, esc_name);
    b_append(search_html, SEARCH_HTML_MAX,
             "</title></head><body style=\"background:#0d1117;color:#e6edf3;margin:0;\">"
             "<div style=\"background:#161b22;border:1px solid #30363d;padding:10px;\">");
    if (esc_avatar[0]) {
        b_append(search_html, SEARCH_HTML_MAX, "<img src=\"");
        b_append(search_html, SEARCH_HTML_MAX, esc_avatar);
        b_append(search_html, SEARCH_HTML_MAX,
                 "\" width=\"56\" height=\"56\" alt=\"owner avatar\">");
    }
    b_append(search_html, SEARCH_HTML_MAX,
             "<div style=\"color:#58a6ff;font-size:24px;font-weight:bold;\">");
    b_append(search_html, SEARCH_HTML_MAX, esc_name);
    b_append(search_html, SEARCH_HTML_MAX, "</div><div style=\"color:#c9d1d9;margin:6px 0;\">");
    b_append(search_html, SEARCH_HTML_MAX, esc_desc[0] ? esc_desc : "No description.");
    b_append(search_html, SEARCH_HTML_MAX, "</div><div>");
    char number[64];
    number[0] = '\0'; b_append_int(number, sizeof(number), stars);
    b_append(search_html, SEARCH_HTML_MAX, "<div style=\"color:#8b949e;\">Stars: ");
    b_append(search_html, SEARCH_HTML_MAX, number);
    b_append(search_html, SEARCH_HTML_MAX, "</div>");
    number[0] = '\0'; b_append_int(number, sizeof(number), forks);
    b_append(search_html, SEARCH_HTML_MAX, "<div style=\"color:#8b949e;\">Forks: ");
    b_append(search_html, SEARCH_HTML_MAX, number);
    b_append(search_html, SEARCH_HTML_MAX, "</div>");
    number[0] = '\0'; b_append_int(number, sizeof(number), issues);
    b_append(search_html, SEARCH_HTML_MAX, "<div style=\"color:#8b949e;\">Issues: ");
    b_append(search_html, SEARCH_HTML_MAX, number);
    b_append(search_html, SEARCH_HTML_MAX, "</div>");
    if (esc_lang[0]) {
        b_append(search_html, SEARCH_HTML_MAX, "<div style=\"color:#8b949e;\">Language: ");
        b_append(search_html, SEARCH_HTML_MAX, esc_lang);
        b_append(search_html, SEARCH_HTML_MAX, "</div>");
    }
    b_append(search_html, SEARCH_HTML_MAX, "</div>");
    b_append(search_html, SEARCH_HTML_MAX,
             "<div style=\"color:#8b949e;padding:6px 0;\">Code &nbsp; Issues &nbsp; "
             "Pull requests &nbsp; Actions &nbsp; Projects &nbsp; Wiki &nbsp; Security</div>");
    b_append(search_html, SEARCH_HTML_MAX, "<div style=\"background:#161b22;border:1px solid #30363d;padding:10px;margin-top:10px;\"><h2>About</h2><p>");
    b_append(search_html, SEARCH_HTML_MAX, esc_desc[0] ? esc_desc : "No description.");
    b_append(search_html, SEARCH_HTML_MAX, "</p>");
    if (esc_branch[0]) {
        b_append(search_html, SEARCH_HTML_MAX, "<p><b>Default branch:</b> ");
        b_append(search_html, SEARCH_HTML_MAX, esc_branch);
        b_append(search_html, SEARCH_HTML_MAX, "</p>");
    }
    if (esc_license[0]) {
        b_append(search_html, SEARCH_HTML_MAX, "<p><b>License:</b> ");
        b_append(search_html, SEARCH_HTML_MAX, esc_license);
        b_append(search_html, SEARCH_HTML_MAX, "</p>");
    }
    if (esc_home[0]) {
        b_append(search_html, SEARCH_HTML_MAX, "<p><b>Homepage:</b> <a href=\"");
        b_append(search_html, SEARCH_HTML_MAX, esc_home);
        b_append(search_html, SEARCH_HTML_MAX, "\">");
        b_append(search_html, SEARCH_HTML_MAX, esc_home);
        b_append(search_html, SEARCH_HTML_MAX, "</a></p>");
    }
    if (readme_esc[0]) {
        b_append(search_html, SEARCH_HTML_MAX,
                 "<div style=\"background:#161b22;border:1px solid #30363d;padding:10px;margin-top:10px;\">"
                 "<h2>README</h2><pre style=\"color:#1f2328;background:#f6f8fa;padding:8px;\">");
        b_append(search_html, SEARCH_HTML_MAX, readme_esc);
        b_append(search_html, SEARCH_HTML_MAX, "</pre></div>");
    }
    b_append(search_html, SEARCH_HTML_MAX, "<p><a href=\"");
    b_append(search_html, SEARCH_HTML_MAX, esc_url);
    b_append(search_html, SEARCH_HTML_MAX, "\">Open on GitHub</a></p></div></body></html>");
    load_document(search_html, b_strlen(search_html), url, push_history);
    set_status("Repository loaded.", false);
    return 1;
}

/* Generator search returns query.pages objects. It carries a page thumbnail
 * and the intro extract in one request, so results can show a picture and
 * useful text without eight follow-up API calls. */
static int wiki_parse_json(const char *json, int len) {
    int count = 0;
    const char *end = json + len;
    const char *p = json_field(json, end, "pages");
    if (!p) return 0;
    while (p < end && b_is_space(*p)) p++;
    if (p >= end || *p != '{') return 0;
    p++;
    while (p < end && count < SEARCH_MAX_RESULTS) {
        /* Results may be an array or an object keyed by id, so skip to the
         * next object instead of assuming a bare array. */
        while (p < end && *p != '{' && *p != ']') p++;
        if (p >= end || *p != '{') break;
        int rel = json_object_end(p, end);
        if (rel < 0) break;
        const char *obj_end = p + rel;
        struct search_result *r = &search_results[count];
        r->title[0] = r->url[0] = r->domain[0] = r->snippet[0] = r->image[0] = '\0';
        json_string(p, obj_end, "title", r->title, sizeof(r->title));
        char raw_snippet[1200];
        raw_snippet[0] = '\0';
        json_string(p, obj_end, "extract", raw_snippet, sizeof(raw_snippet));
        search_strip_html(raw_snippet, r->snippet, sizeof(r->snippet));
        const char *thumb = json_field(p, obj_end, "thumbnail");
        if (thumb) {
            while (thumb < obj_end && b_is_space(*thumb)) thumb++;
            if (thumb < obj_end && *thumb == '{')
                json_string(thumb, obj_end, "source", r->image, sizeof(r->image));
        }
        if (!r->snippet[0]) b_copy(r->snippet, sizeof(r->snippet), "Wikipedia article");
        b_copy(r->domain, sizeof(r->domain), "en.wikipedia.org");
        if (r->title[0]) {
            char encoded[512];
            char title_path[512];
            b_copy(title_path, sizeof(title_path), r->title);
            for (int i = 0; title_path[i]; i++) if (title_path[i] == ' ') title_path[i] = '_';
            url_encode_text(title_path, encoded, sizeof(encoded));
            b_copy(r->url, sizeof(r->url), "https://en.wikipedia.org/wiki/");
            b_append(r->url, sizeof(r->url), encoded);
        }
        if (r->url[0]) count++;
        p = obj_end + 1;
    }
    return count;
}

static void xml_text_value(const char *start, const char *end,
                           char *out, int max) {
    int oi = 0;
    const char *p = start;
    while (p + 8 < end && p[0] == '<' && p[1] == '!' && p[2] == '[' &&
           p[3] == 'C' && p[4] == 'D' && p[5] == 'A' && p[6] == 'T' &&
           p[7] == 'A' && p[8] == '[') p += 9;
    for (; p < end && oi < max - 1; p++) {
        if (p + 2 < end && p[0] == ']' && p[1] == ']' && p[2] == '>') break;
        if (*p == '<') {
            while (p < end && *p != '>') p++;
            continue;
        }
        if (*p == '&') {
            const char *semi = p;
            while (semi < end && semi - p < 10 && *semi != ';') semi++;
            if (semi < end && *semi == ';') {
                int n = (int)(semi - p - 1);
                const char *name = p + 1;
                char ch = 0;
                if (n == 3 && name[0] == 'a' && name[1] == 'm' && name[2] == 'p') ch = '&';
                else if (n == 2 && name[0] == 'l' && name[1] == 't') ch = '<';
                else if (n == 2 && name[0] == 'g' && name[1] == 't') ch = '>';
                else if (n == 4 && name[0] == 'q' && name[1] == 'u' &&
                         name[2] == 'o' && name[3] == 't') ch = '"';
                else if (n == 4 && name[0] == 'a' && name[1] == 'p' &&
                         name[2] == 'o' && name[3] == 's') ch = '\'';
                if (ch) { out[oi++] = ch; p = semi; continue; }
            }
        }
        out[oi++] = *p;
    }
    while (oi > 0 && (out[oi - 1] == '\n' || out[oi - 1] == '\r' ||
                       out[oi - 1] == ' ' || out[oi - 1] == '\t')) oi--;
    out[oi] = '\0';
}

/* Bing's RSS endpoint is a small keyless fallback when the primary web
 * index does not answer. Each item has a plain title, URL and snippet. */
static int web_parse_rss(const char *xml, int len) {
    int count = 0;
    int pos = 0;
    while (count < SEARCH_MAX_RESULTS) {
        int item = b_find_ci(xml, pos, len, "<item>");
        if (item < 0) break;
        int item_end = b_find_ci(xml, item, len, "</item>");
        if (item_end < 0) break;
        struct search_result *r = &search_results[count];
        r->title[0] = r->url[0] = r->domain[0] = r->snippet[0] = r->image[0] = '\0';
        int start, close, base = item;
        start = b_find_ci(xml, base, item_end, "<title>");
        if (start >= 0) {
            start += 7;
            close = b_find_ci(xml, start, item_end, "</title>");
            if (close >= 0) xml_text_value(xml + start, xml + close,
                                           r->title, sizeof(r->title));
        }
        start = b_find_ci(xml, base, item_end, "<link>");
        if (start >= 0) {
            start += 6;
            close = b_find_ci(xml, start, item_end, "</link>");
            if (close >= 0) xml_text_value(xml + start, xml + close,
                                           r->url, sizeof(r->url));
        }
        start = b_find_ci(xml, base, item_end, "<description>");
        if (start >= 0) {
            start += 13;
            close = b_find_ci(xml, start, item_end, "</description>");
            if (close >= 0) xml_text_value(xml + start, xml + close,
                                           r->snippet, sizeof(r->snippet));
        }
        const char *u = r->url;
        if (u[0] && u[1] && u[2] && u[3]) {
            const char *host = u;
            if (host[0] == 'h' && host[1] == 't' && host[2] == 't' && host[3] == 'p') {
                int i = 0;
                while (host[i] && host[i] != ':' && i < 12) i++;
                if (host[i] == ':' && host[i + 1] == '/' && host[i + 2] == '/')
                    host += i + 3;
            }
            int j = 0;
            while (host[j] && host[j] != '/' && host[j] != '?' &&
                   host[j] != '#' && j < (int)sizeof(r->domain) - 1) {
                r->domain[j] = host[j];
                j++;
            }
            r->domain[j] = '\0';
        }
        if (r->url[0]) count++;
        pos = item_end + 7;
    }
    return count;
}

static void search_domain_from_url(const char *url, char *out, int max) {
    out[0] = '\0';
    if (!url || !url[0]) return;
    const char *host = url;
    if (host[0] == 'h' && host[1] == 't' && host[2] == 't' && host[3] == 'p') {
        int i = 0;
        while (host[i] && host[i] != ':' && i < 12) i++;
        if (host[i] == ':' && host[i + 1] == '/' && host[i + 2] == '/')
            host += i + 3;
    }
    int j = 0;
    while (host[j] && host[j] != '/' && host[j] != '?' && host[j] != '#' &&
           j < max - 1) {
        out[j] = host[j];
        j++;
    }
    out[j] = '\0';
}

static int ddg_add_array(const char *json, int len, const char *field,
                         int start, int limit) {
    const char *end = json + len;
    const char *p = json_field(json, end, field);
    if (!p) return start;
    while (p < end && *p != '[') p++;
    if (p >= end) return start;
    p++;
    int count = start;
    while (p < end && count < limit) {
        while (p < end && *p != '{' && *p != ']') p++;
        if (p >= end || *p != '{') break;
        int rel = json_object_end(p, end);
        if (rel < 0) break;
        const char *obj_end = p + rel;
        char text[240];
        char url[MAX_URL];
        text[0] = url[0] = '\0';
        json_string(p, obj_end, "FirstURL", url, sizeof(url));
        json_string(p, obj_end, "Text", text, sizeof(text));
        if (url[0] && text[0]) {
            struct search_result *r = &search_results[count];
            b_copy(r->title, sizeof(r->title), text);
            b_copy(r->url, sizeof(r->url), url);
            b_copy(r->snippet, sizeof(r->snippet), "DuckDuckGo related result");
            r->image[0] = '\0';
            search_domain_from_url(url, r->domain, sizeof(r->domain));
            count++;
        }
        p = obj_end + 1;
    }
    return count;
}

static int ddg_parse_json(const char *json, int len, int start, int limit) {
    int count = start;
    const char *end = json + len;
    char heading[128];
    char abstract[240];
    char url[MAX_URL];
    heading[0] = abstract[0] = url[0] = '\0';
    json_string(json, end, "Heading", heading, sizeof(heading));
    json_string(json, end, "AbstractText", abstract, sizeof(abstract));
    json_string(json, end, "AbstractURL", url, sizeof(url));
    if (count < limit && abstract[0] && url[0]) {
        struct search_result *r = &search_results[count];
        b_copy(r->title, sizeof(r->title), heading[0] ? heading : "DuckDuckGo");
        b_copy(r->url, sizeof(r->url), url);
        b_copy(r->snippet, sizeof(r->snippet), abstract);
        r->image[0] = '\0';
        search_domain_from_url(url, r->domain, sizeof(r->domain));
        count++;
    }
    count = ddg_add_array(json, len, "Results", count, limit);
    count = ddg_add_array(json, len, "RelatedTopics", count, limit);
    return count;
}

static int duckduckgo_search(const char *query, int start, int limit) {
    if (limit <= start) return start;
    char encoded[SEARCH_MAX * 3 + 1];
    url_encode_text(query, encoded, sizeof(encoded));
    char api[512];
    b_copy(api, sizeof(api),
           "https://api.duckduckgo.com/?format=json&no_html=1&skip_disambig=1&q=");
    b_append(api, sizeof(api), encoded);
    int bytes = fetch_url(api, response, RESPONSE_MAX);
    if (bytes <= 0) return start;
    return ddg_parse_json((const char *)response, bytes, start, limit);
}

static int search_parse_json(const char *json, int len) {
    int count = 0;
    const char *end = json + len;
    const char *p = json_field(json, end, "results");
    if (!p) return 0;
    while (p < end && b_is_space(*p)) p++;
    if (p >= end || *p != '[') return 0;
    p++;
    while (p < end && count < SEARCH_MAX_RESULTS) {
        /* Results may be an array or an object keyed by id, so skip to the
         * next object instead of assuming a bare array. */
        while (p < end && *p != '{' && *p != ']') p++;
        if (p >= end || *p != '{') break;
        int rel = json_object_end(p, end);
        if (rel < 0) break;
        const char *obj_end = p + rel;
        struct search_result *r = &search_results[count];
        r->title[0] = r->url[0] = r->domain[0] = r->snippet[0] = r->image[0] = '\0';
        json_string(p, obj_end, "title", r->title, sizeof(r->title));
        json_string(p, obj_end, "url", r->url, sizeof(r->url));
        json_string(p, obj_end, "domain", r->domain, sizeof(r->domain));
        json_string(p, obj_end, "snippet", r->snippet, sizeof(r->snippet));
        if (r->url[0]) count++;
        p = obj_end + 1;
    }
    return count;
}

static void html_escape(const char *src, char *out, int max) {
    int oi = 0;
    for (int i = 0; src && src[i] && oi < max - 1; i++) {
        char c = src[i];
        const char *rep = 0;
        if (c == '&') rep = "&amp;";
        else if (c == '<') rep = "&lt;";
        else if (c == '>') rep = "&gt;";
        else if (c == '"') rep = "&quot;";
        else if (c == '\'') rep = "&#39;";
        if (rep) {
            for (int j = 0; rep[j] && oi < max - 1; j++) out[oi++] = rep[j];
        } else {
            out[oi++] = c;
        }
    }
    out[oi] = '\0';
}

static void search_append_control(const char *query, int provider,
                                  int count, int total, bool failed) {
    char escaped[SEARCH_MAX * 6];
    char query_url[SEARCH_MAX * 4];
    html_escape(query, escaped, sizeof(escaped));
    url_encode_text(query, query_url, sizeof(query_url));
    search_html[0] = '\0';
    b_append(search_html, SEARCH_HTML_MAX,
             "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
             "<title>Solis Search</title><style>"
             "body { background: #ffffff; color: #202124; margin: 0; }"
             ".page { max-width: 760px; margin: auto; padding: 0 10px 20px 10px; }"
             ".top { background: #f1f5fb; border-bottom: 1px solid #d9e3f0; padding: 8px; }"
             ".brand { color: #1a73e8; font-weight: bold; }"
             ".muted { color: #6b7280; }"
             ".tab { color: #1558b0; margin-right: 12px; }"
             ".active { color: #111827; font-weight: bold; }"
             "h1 { color: #173b6c; margin: 12px 0; }"
             ".meta { color: #6b7280; margin-bottom: 10px; }"
             ".result { background: #fbfdff; border: 1px solid #dbe6f5; margin: 8px 0; padding: 8px; border-radius: 4px; }"
             ".title { color: #1558b0; font-weight: bold; }"
             ".url { color: #2e7d32; }"
             ".snippet { color: #4b5563; }"
             ".wiki-search { background:#f8f9fa; border:1px solid #a2a9b1; padding:8px; margin:10px 0; }"
             ".gh-search { background:#0d1117; color:#e6edf3; padding:8px; margin:10px 0; }"
             ".gh-search .brand { color:#e6edf3; }"
             ".result-img { width:48px; height:48px; }"
             ".footer { color: #6b7280; margin-top: 18px; }");
    if (browser_theme == THEME_DARK) {
        b_append(search_html, SEARCH_HTML_MAX,
                 "body{background:#0d1117;color:#e6edf3;}"
                 ".page{color:#e6edf3;}.top{background:#161b22;border-color:#30363d;}"
                 ".brand{color:#58a6ff;}.muted,.meta,.footer{color:#9ca3af;}"
                 "h1{color:#e6edf3;}.tab{color:#79c0ff;}.active{color:#f0f6fc;}"
                 ".result{background:#161b22;border-color:#30363d;}"
                 ".title{color:#58a6ff;}.url{color:#7ee787;}.snippet{color:#c9d1d9;}"
                 ".wiki-search{background:#161b22;border-color:#30363d;color:#e6edf3;}"
                 ".gh-search{background:#0d1117;border-color:#30363d;}"
                 "a{color:#58a6ff;}");
    }
    b_append(search_html, SEARCH_HTML_MAX,
             "</style></head><body><div class=\"page\">"
             "<div class=\"top\"><span class=\"brand\">Solis</span> "
             "<span class=\"muted\">search</span> &nbsp; ");
    b_append(search_html, SEARCH_HTML_MAX, "<a class=\"tab");
    if (provider == SEARCH_WEB) b_append(search_html, SEARCH_HTML_MAX, " active");
    if (browser_theme == THEME_DARK)
        b_append(search_html, SEARCH_HTML_MAX, "\" style=\"color:#79c0ff");
    b_append(search_html, SEARCH_HTML_MAX, "\" href=\"solis://search?q=");
    b_append(search_html, SEARCH_HTML_MAX, query_url);
    b_append(search_html, SEARCH_HTML_MAX, "\">Web</a> &nbsp; ");
    b_append(search_html, SEARCH_HTML_MAX, "<a class=\"tab");
    if (provider == SEARCH_WIKI) b_append(search_html, SEARCH_HTML_MAX, " active");
    if (browser_theme == THEME_DARK)
        b_append(search_html, SEARCH_HTML_MAX, "\" style=\"color:#79c0ff");
    b_append(search_html, SEARCH_HTML_MAX, "\" href=\"solis://wikipedia?q=");
    b_append(search_html, SEARCH_HTML_MAX, query_url);
    b_append(search_html, SEARCH_HTML_MAX, "\">Wikipedia</a> &nbsp; ");
    b_append(search_html, SEARCH_HTML_MAX, "<a class=\"tab");
    if (provider == SEARCH_GITHUB) b_append(search_html, SEARCH_HTML_MAX, " active");
    if (browser_theme == THEME_DARK)
        b_append(search_html, SEARCH_HTML_MAX, "\" style=\"color:#79c0ff");
    b_append(search_html, SEARCH_HTML_MAX, "\" href=\"solis://github?q=");
    b_append(search_html, SEARCH_HTML_MAX, query_url);
    b_append(search_html, SEARCH_HTML_MAX, "\">GitHub</a></div>");
    if (provider == SEARCH_WIKI) {
        b_append(search_html, SEARCH_HTML_MAX,
                 "<div class=\"wiki-search\"><b>WIKIPEDIA</b> The Free Encyclopedia"
                 "<form action=\"solis://wikipedia\" method=\"get\">"
                 "<input type=\"search\" name=\"q\" value=\"");
        b_append(search_html, SEARCH_HTML_MAX, escaped);
        b_append(search_html, SEARCH_HTML_MAX,
                 "\" placeholder=\"Search Wikipedia\">"
                 "<button type=\"submit\">Search</button></form></div>");
    } else if (provider == SEARCH_GITHUB) {
        b_append(search_html, SEARCH_HTML_MAX,
                 "<div class=\"gh-search\"><span class=\"brand\">GitHub</span>"
                 "<form action=\"solis://github\" method=\"get\">"
                 "<input type=\"search\" name=\"q\" value=\"");
        b_append(search_html, SEARCH_HTML_MAX, escaped);
        b_append(search_html, SEARCH_HTML_MAX,
                 "\" placeholder=\"Search GitHub repositories\">"
                 "<button type=\"submit\">Search</button></form></div>");
    }
    b_append(search_html, SEARCH_HTML_MAX, "<h1>");
    if (provider == SEARCH_WIKI) b_append(search_html, SEARCH_HTML_MAX, "Wikipedia: ");
    else if (provider == SEARCH_GITHUB) b_append(search_html, SEARCH_HTML_MAX, "GitHub: ");
    else b_append(search_html, SEARCH_HTML_MAX, "Results for ");
    b_append(search_html, SEARCH_HTML_MAX, escaped);
    b_append(search_html, SEARCH_HTML_MAX, "</h1>");

    char meta[160];
    meta[0] = '\0';
    if (failed) b_append(meta, sizeof(meta), "Search service unavailable.");
    else {
        b_append_int(meta, sizeof(meta), count);
        b_append(meta, sizeof(meta), count == 1 ? " result" : " results");
        if (total > count) {
            b_append(meta, sizeof(meta), " (about ");
            b_append_int(meta, sizeof(meta), total);
            b_append(meta, sizeof(meta), " total)");
        }
    }
    b_append(search_html, SEARCH_HTML_MAX, "<div class=\"meta\">");
    b_append(search_html, SEARCH_HTML_MAX, meta);
    b_append(search_html, SEARCH_HTML_MAX, "</div>");

    if (!failed) {
        for (int i = 0; i < count; i++) {
            char title[768];
            char url[512];
            char domain[384];
            char snippet[1440];
            html_escape(search_results[i].title, title, sizeof(title));
            html_escape(search_results[i].url, url, sizeof(url));
            html_escape(search_results[i].domain[0] ? search_results[i].domain
                                                     : search_results[i].url,
                        domain, sizeof(domain));
            html_escape(search_results[i].snippet, snippet, sizeof(snippet));
            char image_url[512];
            image_url[0] = '\0';
            if (search_results[i].image[0]) {
                b_copy(image_url, sizeof(image_url), search_results[i].image);
            } else if (provider == SEARCH_WEB && search_results[i].domain[0]) {
                b_copy(image_url, sizeof(image_url),
                       "https://www.google.com/s2/favicons?sz=64&domain=");
                b_append(image_url, sizeof(image_url), search_results[i].domain);
            }
            char image_esc[768];
            html_escape(image_url, image_esc, sizeof(image_esc));
            b_append(search_html, SEARCH_HTML_MAX, "<div class=\"result\">");
            if (image_esc[0]) {
                b_append(search_html, SEARCH_HTML_MAX, "<img class=\"result-img\" src=\"");
                b_append(search_html, SEARCH_HTML_MAX, image_esc);
                b_append(search_html, SEARCH_HTML_MAX,
                         "\" width=\"48\" height=\"48\" alt=\"result picture\">");
            }
            b_append(search_html, SEARCH_HTML_MAX, "<a class=\"title\" href=\"");
            b_append(search_html, SEARCH_HTML_MAX, url);
            b_append(search_html, SEARCH_HTML_MAX, "\">");
            b_append(search_html, SEARCH_HTML_MAX, title);
            b_append(search_html, SEARCH_HTML_MAX, "</a><div class=\"url\">");
            b_append(search_html, SEARCH_HTML_MAX, domain);
            b_append(search_html, SEARCH_HTML_MAX, "</div><p class=\"snippet\">");
            b_append(search_html, SEARCH_HTML_MAX, snippet);
            b_append(search_html, SEARCH_HTML_MAX, "</p></div>");
        }
    } else {
        b_append(search_html, SEARCH_HTML_MAX,
                 "<p class=\"snippet\">The search endpoint could not be reached. "
                 "Check the connection and try one of the providers below.</p>");
    }

    b_append(search_html, SEARCH_HTML_MAX, "<p class=\"footer\">Search again: "
             "<a href=\"solis://search?q=");
    b_append(search_html, SEARCH_HTML_MAX, query_url);
    b_append(search_html, SEARCH_HTML_MAX, "\">Web</a> &nbsp; "
             "<a href=\"solis://wikipedia?q=");
    b_append(search_html, SEARCH_HTML_MAX, query_url);
    b_append(search_html, SEARCH_HTML_MAX, "\">Wikipedia</a> &nbsp; "
             "<a href=\"solis://github?q=");
    b_append(search_html, SEARCH_HTML_MAX, query_url);
    b_append(search_html, SEARCH_HTML_MAX, "\">GitHub</a></p>"
             "<p class=\"footer\"><a href=\"https://html.duckduckgo.com/html/?q=");
    b_append(search_html, SEARCH_HTML_MAX, query_url);
    b_append(search_html, SEARCH_HTML_MAX, "\">Open DuckDuckGo</a></p>"
             "</div></body></html>");
}

static void browser_settings_defaults(void) {
    browser_theme = THEME_LIGHT;
    duckduckgo_mode = DDG_FALLBACK;
    web_search_api = WEB_API_FREESERP;
    browser_load_images = true;
    browser_js_enabled = true;
}

static void browser_settings_load(void) {
    browser_settings_defaults();
    int fd = vfs_open(BROWSER_SETTINGS_PATH);
    if (fd < 0) return;
    char buf[192];
    int n = vfs_read(fd, (uint8_t *)buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = '\0';
    if (b_find_ci(buf, 0, n, "theme=dark") >= 0) browser_theme = THEME_DARK;
    if (b_find_ci(buf, 0, n, "ddg=0") >= 0) duckduckgo_mode = DDG_OFF;
    else if (b_find_ci(buf, 0, n, "ddg=2") >= 0) duckduckgo_mode = DDG_ADD;
    else if (b_find_ci(buf, 0, n, "ddg=1") >= 0) duckduckgo_mode = DDG_FALLBACK;
    if (b_find_ci(buf, 0, n, "api=2") >= 0) web_search_api = WEB_API_BING;
    else if (b_find_ci(buf, 0, n, "api=1") >= 0) web_search_api = WEB_API_DDG;
    else if (b_find_ci(buf, 0, n, "api=0") >= 0) web_search_api = WEB_API_FREESERP;
    if (b_find_ci(buf, 0, n, "images=0") >= 0) browser_load_images = false;
    if (b_find_ci(buf, 0, n, "js=0") >= 0) browser_js_enabled = false;
}

static void browser_settings_save(void) {
    int fd = vfs_create(BROWSER_SETTINGS_PATH);
    if (fd < 0) fd = vfs_open(BROWSER_SETTINGS_PATH);
    if (fd < 0) return;
    char buf[192];
    b_copy(buf, sizeof(buf), browser_theme == THEME_DARK ? "theme=dark\n" : "theme=light\n");
    b_append(buf, sizeof(buf), "ddg=");
    b_append_int(buf, sizeof(buf), duckduckgo_mode);
    b_append(buf, sizeof(buf), "\napi=");
    b_append_int(buf, sizeof(buf), web_search_api);
    b_append(buf, sizeof(buf), "\nimages=");
    b_append(buf, sizeof(buf), browser_load_images ? "1\n" : "0\n");
    b_append(buf, sizeof(buf), "js=");
    b_append(buf, sizeof(buf), browser_js_enabled ? "1\n" : "0\n");
    vfs_write(fd, (const uint8_t *)buf, (uint32_t)b_strlen(buf));
}

static const char *web_api_name(int api) {
    if (api == WEB_API_BING) return "Bing RSS";
    if (api == WEB_API_DDG) return "DuckDuckGo";
    return "FreeSerp";
}

static void settings_option(const char *href, const char *title,
                            const char *description, bool selected) {
    b_append(search_html, SEARCH_HTML_MAX, "<a class=\"option");
    if (selected) b_append(search_html, SEARCH_HTML_MAX, " on");
    b_append(search_html, SEARCH_HTML_MAX, "\"");
    if (browser_theme == THEME_DARK)
        b_append(search_html, SEARCH_HTML_MAX, " style=\"color:#c9d1d9;\"");
    b_append(search_html, SEARCH_HTML_MAX, " href=\"");
    b_append(search_html, SEARCH_HTML_MAX, href);
    b_append(search_html, SEARCH_HTML_MAX, "\">");
    if (selected) b_append(search_html, SEARCH_HTML_MAX, "* ");
    b_append(search_html, SEARCH_HTML_MAX, "<b>");
    b_append(search_html, SEARCH_HTML_MAX, title);
    b_append(search_html, SEARCH_HTML_MAX, "</b><br><span>");
    b_append(search_html, SEARCH_HTML_MAX, description);
    b_append(search_html, SEARCH_HTML_MAX, "</span></a>");
}

static void settings_base_url(char *out, int max) {
    b_copy(out, max, "solis://browser-settings?theme=");
    b_append(out, max, browser_theme == THEME_DARK ? "dark" : "light");
    b_append(out, max, "&ddg=");
    b_append_int(out, max, duckduckgo_mode);
    b_append(out, max, "&api=");
    b_append_int(out, max, web_search_api);
    b_append(out, max, "&images=");
    b_append(out, max, browser_load_images ? "1" : "0");
    b_append(out, max, "&js=");
    b_append(out, max, browser_js_enabled ? "1" : "0");
}

static void settings_choice_url(char *out, int max, const char *change) {
    settings_base_url(out, max);
    b_append(out, max, "&");
    b_append(out, max, change);
}

static void settings_card_heading(bool dark, const char *heading) {
    b_append(search_html, SEARCH_HTML_MAX,
             "<div class=\"card\"><div style=\"font-weight:bold;font-size:18px;color:");
    b_append(search_html, SEARCH_HTML_MAX, dark ? "#f0f6fc" : "#173b6c");
    b_append(search_html, SEARCH_HTML_MAX, ";\">");
    b_append(search_html, SEARCH_HTML_MAX, heading);
    b_append(search_html, SEARCH_HTML_MAX, "</div>");
}

static void browser_settings_page(bool push_history) {
    bool dark = browser_theme == THEME_DARK;
    search_html[0] = '\0';
    b_append(search_html, SEARCH_HTML_MAX,
             "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
             "<title>Browser Settings</title><style>");
    if (dark) {
        b_append(search_html, SEARCH_HTML_MAX,
                 "body{background:#0d1117;color:#e6edf3;margin:0;font-family:sans-serif;}"
                 ".wrap{max-width:680px;margin:auto;padding:18px;}"
                 "body h1{color:#f0f6fc;margin:0 0 6px;}body h2{color:#f0f6fc;font-size:17px;margin:0 0 8px;}"
                 "body p{color:#c9d1d9;line-height:1.5;}.card{background:#161b22;border:1px solid #30363d;"
                 "border-radius:8px;padding:12px;margin:12px 0;}.option{display:block;background:#21262d;"
                 "border:1px solid #30363d;border-radius:6px;padding:8px;margin:6px 0;color:#c9d1d9;"
                 "text-decoration:none;}.option.on{border-color:#58a6ff;background:#1f3a5f;}"
                 ".option b{color:#f0f6fc;}.option span{color:#9ca3af;font-size:13px;}"
                 ".meta{color:#8b949e;font-size:13px;}a{color:#79c0ff;}.badge{color:#4fd1b0;}"
                 "</style></head><body><div class=\"wrap\">");
    } else {
        b_append(search_html, SEARCH_HTML_MAX,
                 "body{background:#f6f8fa;color:#202124;margin:0;font-family:sans-serif;}"
                 ".wrap{max-width:680px;margin:auto;padding:18px;}"
                 "h1{color:#173b6c;margin:0 0 6px;}h2{color:#173b6c;font-size:17px;margin:0 0 8px;}"
                 "p{color:#5f6368;line-height:1.5;}.card{background:#ffffff;border:1px solid #dbe6f5;"
                 "border-radius:8px;padding:12px;margin:12px 0;}.option{display:block;background:#fbfdff;"
                 "border:1px solid #dbe6f5;border-radius:6px;padding:8px;margin:6px 0;color:#202124;"
                 "text-decoration:none;}.option.on{border-color:#1a73e8;background:#e8f0fe;}"
                 ".option b{color:#173b6c;}.option span{color:#5f6368;font-size:13px;}"
                 ".meta{color:#6b7280;font-size:13px;}a{color:#1558b0;}.badge{color:#087f5b;}"
                 "</style></head><body><div class=\"wrap\">");
    }
    b_append(search_html, SEARCH_HTML_MAX,
             "<div style=\"font-weight:bold;font-size:24px;color:");
    b_append(search_html, SEARCH_HTML_MAX, dark ? "#f0f6fc" : "#173b6c");
    b_append(search_html, SEARCH_HTML_MAX,
             ";\">Browser Settings</div>"
             "<p class=\"meta\">Saved in /home/browser.cfg. "
             "Website colors are left unchanged by the Browser theme.</p>");

    char href[256];
    settings_card_heading(dark, "Appearance");
    settings_choice_url(href, sizeof(href), "theme=light");
    settings_option(href, "Light mode", "Bright Solis pages and search results", !dark);
    settings_choice_url(href, sizeof(href), "theme=dark");
    settings_option(href, "Dark mode", "Dark Solis pages, search results, and start page", dark);
    b_append(search_html, SEARCH_HTML_MAX, "</div>");

    settings_card_heading(dark, "Web search API");
    b_append(search_html, SEARCH_HTML_MAX,
             "<p class=\"meta\">Choose the primary keyless API for ordinary web "
             "search. Wikipedia and GitHub always keep their own APIs.</p>");
    settings_choice_url(href, sizeof(href), "api=2");
    settings_option(href, "Bing RSS",
                    "Fast, keyless RSS results; recommended when a provider is unavailable",
                    web_search_api == WEB_API_BING);
    settings_choice_url(href, sizeof(href), "api=1");
    settings_option(href, "DuckDuckGo",
                    "Keyless Instant Answer results with related topics",
                    web_search_api == WEB_API_DDG);
    settings_choice_url(href, sizeof(href), "api=0");
    settings_option(href, "FreeSerp",
                    "Solis's original keyless web index; useful as a second opinion",
                    web_search_api == WEB_API_FREESERP);
    b_append(search_html, SEARCH_HTML_MAX,
             "<p class=\"meta\">Try the selected API: "
             "<a href=\"solis://search?q=Solis%20OS\">Solis OS</a> &nbsp; "
             "<a href=\"solis://search?q=operating%20system%20history\">OS history</a>"
             "</p></div>");

    settings_card_heading(dark, "DuckDuckGo supplementation");
    settings_choice_url(href, sizeof(href), "ddg=0");
    settings_option(href, "No DuckDuckGo", "Never use DuckDuckGo results",
                    duckduckgo_mode == DDG_OFF);
    settings_choice_url(href, sizeof(href), "ddg=1");
    settings_option(href, "Only as a fallback",
                    "Use DuckDuckGo when the chosen primary API returns nothing",
                    duckduckgo_mode == DDG_FALLBACK);
    settings_choice_url(href, sizeof(href), "ddg=2");
    settings_option(href, "Add results automatically",
                    "Merge DuckDuckGo results into every web search",
                    duckduckgo_mode == DDG_ADD);
    b_append(search_html, SEARCH_HTML_MAX, "</div>");

    settings_card_heading(dark, "Page content");
    settings_choice_url(href, sizeof(href), "images=1");
    settings_option(href, "Load pictures",
                    "Download thumbnails, avatars, icons, and photos in BMP, PNG, JPEG, GIF, ICO, TGA, PNM, QOI, or XBM format",
                    browser_load_images);
    settings_choice_url(href, sizeof(href), "images=0");
    settings_option(href, "Text only",
                    "Skip picture downloads for the fastest pages and lowest memory use",
                    !browser_load_images);
    settings_choice_url(href, sizeof(href), "js=1");
    settings_option(href, "JavaScript on",
                    "Run the bounded Solis script engine on compatible pages",
                    browser_js_enabled);
    settings_choice_url(href, sizeof(href), "js=0");
    settings_option(href, "JavaScript off",
                    "Do not execute page scripts or click handlers",
                    !browser_js_enabled);
    b_append(search_html, SEARCH_HTML_MAX,
             "<p class=\"meta\">See the decoders in action: "
             "<a href=\"file:///picture-formats.html\">classic formats</a> and "
             "<a href=\"file:///picture-formats-2.html\">more formats</a>.</p>"
             "<p class=\"meta\">Source limits: PNG 384x256; TGA/PNM 8192x8192; "
             "BMP 8192x8192; GIF 4096x4096; ICO 1024x1024; QOI/XBM 4096x4096; "
             "JPEG 16384x16384.</p>"
             "</div>");

    settings_card_heading(dark, "Network and data");
    b_append(search_html, SEARCH_HTML_MAX, "<p class=\"meta\">Network: <span class=\"badge\">");
    b_append(search_html, SEARCH_HTML_MAX, net_available() ? "connected" : "disconnected");
    b_append(search_html, SEARCH_HTML_MAX, "</span> &nbsp; Primary API: <b>");
    b_append(search_html, SEARCH_HTML_MAX, web_api_name(web_search_api));
    b_append(search_html, SEARCH_HTML_MAX,
             "</b> &nbsp; Pictures: <b>");
    b_append(search_html, SEARCH_HTML_MAX, browser_load_images ? "on" : "off");
    b_append(search_html, SEARCH_HTML_MAX, "</b></p>"
             "<p><a href=\"solis://browser-settings?reset=1\">Reset all Browser settings</a>"
             " &nbsp; <a href=\"solis://browser-settings?clearhistory=1\">Clear history</a></p>");
    b_append(search_html, SEARCH_HTML_MAX, "</div>");
    b_append(search_html, SEARCH_HTML_MAX,
             "<p><a href=\"solis://home\">Back to the start page</a></p>"
             "</div></body></html>");
    load_document(search_html, b_strlen(search_html),
                  "solis://browser-settings", push_history);
    set_status("Browser settings saved.", false);
}

static void browser_settings_open(const char *url, bool push_history) {
    bool changed = false;
    char value[24];
    if (url_query_param(url, "reset", value, sizeof(value))) {
        browser_settings_defaults();
        changed = true;
    }
    if (url_query_param(url, "clearhistory", value, sizeof(value))) {
        history_count = 0;
        history_pos = -1;
    }
    if (url_query_param(url, "theme", value, sizeof(value))) {
        int theme = b_equal_ci(value, "dark") ? THEME_DARK : THEME_LIGHT;
        if (browser_theme != theme) changed = true;
        browser_theme = theme;
    }
    if (url_query_param(url, "ddg", value, sizeof(value))) {
        int mode = DDG_FALLBACK;
        if (value[0] == '0') mode = DDG_OFF;
        else if (value[0] == '2') mode = DDG_ADD;
        if (duckduckgo_mode != mode) changed = true;
        duckduckgo_mode = mode;
    }
    if (url_query_param(url, "api", value, sizeof(value))) {
        int api = WEB_API_FREESERP;
        if (value[0] == '1') api = WEB_API_DDG;
        else if (value[0] == '2') api = WEB_API_BING;
        if (web_search_api != api) changed = true;
        web_search_api = api;
    }
    if (url_query_param(url, "images", value, sizeof(value))) {
        bool on = value[0] != '0';
        if (browser_load_images != on) changed = true;
        browser_load_images = on;
    }
    if (url_query_param(url, "js", value, sizeof(value))) {
        bool on = value[0] != '0';
        if (browser_js_enabled != on) changed = true;
        browser_js_enabled = on;
    }
    if (changed) browser_settings_save();
    browser_settings_page(push_history);
}

static int web_search_bing(const char *encoded, int start, int limit) {
    (void)start;
    (void)limit;
    char rss[1024];
    b_copy(rss, sizeof(rss), "https://www.bing.com/search?format=rss&q=");
    b_append(rss, sizeof(rss), encoded);
    int bytes = fetch_url(rss, response, RESPONSE_MAX);
    if (bytes <= 0) return 0;
    return web_parse_rss((const char *)response, bytes);
}

/* Last-resort keyless index. A selected provider can be temporarily down or
 * serve a certificate chain the built-in anchor set does not cover; in that
 * case the user should still get web results instead of an empty page. */
static int web_search_freeserp(const char *encoded, int start, int limit) {
    (void)start;
    (void)limit;
    char api[1024];
    b_copy(api, sizeof(api), "https://freeserp.ai/api.php?index=web&q=");
    b_append(api, sizeof(api), encoded);
    b_append(api, sizeof(api), "&size=8");
    int bytes = fetch_url(api, response, RESPONSE_MAX);
    if (bytes <= 0) return 0;
    response[bytes] = '\0';
    return search_parse_json((const char *)response, bytes);
}

static void search_load_provider(const char *raw_query, int provider,
                                  bool push_history) {
    char query[SEARCH_MAX];
    b_trim_copy(query, sizeof(query), raw_query);
    if (!query[0]) { show_home(); return; }

    char encoded[SEARCH_MAX * 3 + 1];
    url_encode_text(query, encoded, sizeof(encoded));
    char search_url[MAX_URL];
    b_copy(search_url, sizeof(search_url),
           provider == SEARCH_WIKI ? "solis://wikipedia?q=" :
           provider == SEARCH_GITHUB ? "solis://github?q=" :
                                       "solis://search?q=");
    b_append(search_url, sizeof(search_url), encoded);

    int count = 0;
    int total = 0;
    bool failed = true;
    if (net_available()) {
        char api[1024];
        if (provider == SEARCH_WIKI) {
            b_copy(api, sizeof(api),
                   "https://en.wikipedia.org/w/api.php?action=query&generator=search"
                   "&format=json&gsrlimit=8&prop=pageimages|extracts&piprop=thumbnail"
                   "&pithumbsize=96&exintro=1&explaintext=1&gsrsearch=");
            b_append(api, sizeof(api), encoded);
        } else if (provider == SEARCH_GITHUB) {
            b_copy(api, sizeof(api),
                   "https://api.github.com/search/repositories?per_page=8&q=");
            b_append(api, sizeof(api), encoded);
        } else if (web_search_api == WEB_API_BING ||
                   web_search_api == WEB_API_DDG) {
            b_copy(api, sizeof(api), "");
        } else {
            b_copy(api, sizeof(api), "https://freeserp.ai/api.php?index=web&q=");
            b_append(api, sizeof(api), encoded);
            b_append(api, sizeof(api), "&size=8");
        }
        int bytes = api[0] ? fetch_url(api, response, RESPONSE_MAX) : 0;
        if (provider == SEARCH_WEB && web_search_api == WEB_API_DDG) {
            count = duckduckgo_search(query, 0, SEARCH_MAX_RESULTS);
            total = count;
        } else if (provider == SEARCH_WEB && web_search_api == WEB_API_BING) {
            count = web_search_bing(encoded, 0, SEARCH_MAX_RESULTS);
            total = count;
        } else if (bytes > 0) {
            response[bytes] = '\0';
            const char *end = (const char *)response + bytes;
            if (provider == SEARCH_WIKI) {
                count = wiki_parse_json((const char *)response, bytes);
                total = json_int((const char *)response, end, "totalhits");
            } else if (provider == SEARCH_GITHUB) {
                count = github_parse_json((const char *)response, bytes);
                total = json_int((const char *)response, end, "total_count");
            } else {
                count = search_parse_json((const char *)response, bytes);
                total = json_int((const char *)response, end, "total");
            }
        }
        if (provider == SEARCH_WEB) {
            if (count == 0 && web_search_api != WEB_API_BING) {
                count = web_search_bing(encoded, 0, SEARCH_MAX_RESULTS);
                total = count;
            }
            if (count == 0 && web_search_api != WEB_API_DDG &&
                duckduckgo_mode >= DDG_FALLBACK) {
                count = duckduckgo_search(query, 0, SEARCH_MAX_RESULTS);
                total = count;
            }
            if (count == 0 && web_search_api != WEB_API_FREESERP) {
                count = web_search_freeserp(encoded, 0, SEARCH_MAX_RESULTS);
                total = count;
            }
            if (count > 0 && duckduckgo_mode == DDG_ADD &&
                web_search_api != WEB_API_DDG)
                count = duckduckgo_search(query, count, SEARCH_MAX_RESULTS);
        }
        if (count > 0 || total > 0) failed = false;
    }

    search_append_control(query, provider, count, total, failed);
    int len = b_strlen(search_html);
    load_document(search_html, len, search_url, push_history);
    set_address(query);
    address_focus = false;
    address_select_all = false;
    set_status(failed ? "Search service unavailable." : "Search results ready.",
               failed);
}

static void search_load(const char *raw_query, bool push_history) {
    search_load_provider(raw_query, SEARCH_WEB, push_history);
}

static void browser_load(const char *input, bool push_history) {
    char url[MAX_URL];
    char query[SEARCH_MAX];
    if (b_starts_ci(input, "solis://browser-settings")) {
        browser_settings_open(input, push_history);
        return;
    }
    if (b_starts_ci(input, "solis://home") || b_starts_ci(input, "solis://start")) {
        show_home();
        return;
    }
    int provider = search_provider_from_url(input, query, sizeof(query));
    if (provider >= 0) {
        search_load_provider(query, provider, push_history);
        return;
    }
    {
        char trimmed[SEARCH_MAX];
        b_trim_copy(trimmed, sizeof(trimmed), input);
        if (b_starts_ci(trimmed, "wiki ")) {
            search_load_provider(trimmed + 5, SEARCH_WIKI, push_history);
            return;
        }
        if (b_starts_ci(trimmed, "wikipedia ")) {
            search_load_provider(trimmed + 10, SEARCH_WIKI, push_history);
            return;
        }
        if (b_starts_ci(trimmed, "github ") || b_starts_ci(trimmed, "gh ")) {
            search_load_provider(trimmed + (b_starts_ci(trimmed, "gh ") ? 3 : 7),
                                 SEARCH_GITHUB, push_history);
            return;
        }
    }
    if (looks_like_search(input)) {
        search_load(input, push_history);
        return;
    }
    if (!normalize_url(input, url, sizeof(url))) {
        set_status("Type a web address or a search phrase.", true);
        return;
    }
    set_address(url);
    b_copy(page_url, sizeof(page_url), url);

    if (b_starts_ci(url, "file://")) {
        char path[MAX_LOCAL_PATH];
        if (!parse_file_url(url, path, sizeof(path))) {
            set_status("Invalid local path.", true);
            return;
        }
        load_local_file(path, url, push_history);
        return;
    }

    if (!net_available()) {
        set_status("Network disconnected.", true);
        show_error("Network disconnected",
                   "Connect the network, then try this address again.");
        return;
    }

    char query_value[SEARCH_MAX];
    if (b_find_ci(url, 0, b_strlen(url), "github.com/search") >= 0 &&
        url_query_param(url, "q", query_value, sizeof(query_value))) {
        search_load_provider(query_value, SEARCH_GITHUB, push_history);
        return;
    }
    if (b_find_ci(url, 0, b_strlen(url), "wikipedia.org/w/index.php") >= 0 &&
        url_query_param(url, "search", query_value, sizeof(query_value))) {
        search_load_provider(query_value, SEARCH_WIKI, push_history);
        return;
    }
    if (github_home_page(url, push_history)) return;
    if (github_repo_page(url, push_history)) return;

    set_status("Loading page...", false);
    char fetch_target[MAX_URL];
    b_copy(fetch_target, sizeof(fetch_target), url);
    wikipedia_render_url(url, fetch_target, sizeof(fetch_target));
    int bytes = fetch_url(fetch_target, response, RESPONSE_MAX);
    if (bytes <= 0) {
        set_status("Could not load the page.", true);
        show_error("Could not load page",
                   "The host did not answer, DNS failed, or the page is empty.");
        return;
    }
    response[bytes] = '\0';
    load_document((const char *)response, bytes, url, push_history);
}

static void browser_follow(const char *href) {
    if (!href || !href[0]) return;
    if (b_starts_ci(href, "javascript:")) {
        set_status("javascript: links are not supported.", true);
        return;
    }
    if (b_starts_ci(href, "mailto:")) {
        set_status("Mail links are not available in Browser.", true);
        return;
    }
    if (href[0] == '#') {
        if (!page_url[0]) return;
        int cut = 0;
        while (page_url[cut] && page_url[cut] != '#') cut++;
        page_url[cut] = '\0';
        if (href[1]) {
            b_append(page_url, sizeof(page_url), href);
            url_decode_text(href + 1, pending_anchor, sizeof(pending_anchor));
            anchor_pending = true;
        } else {
            pending_anchor[0] = '\0';
            anchor_pending = false;
            scroll_y = 0;
        }
        set_address(page_url);
        layout_dirty = 1;
        return;
    }
    char url[MAX_URL];
    if (!resolve_href(href, url, sizeof(url))) return;
    browser_load(url, true);
}

static int dom_form_ancestor(int node) {
    int depth = 0;
    for (int n = node; n > 0 && n < dom_node_used && depth++ < 128;
         n = dom_nodes[n].parent) {
        if (dom_nodes[n].tag == DOM_FORM) return n;
        if (n == dom_root) break;
    }
    return -1;
}

static void form_append_fields(int form, char *url, int max, int *first) {
    /* Walk the node table instead of the child list.  Site chrome is spliced
     * into the tree after parsing, and a malformed link there must not turn
     * form submission into an endless recursive walk. */
    for (int node = 1; node < dom_node_used; node++) {
        if (dom_nodes[node].tag != DOM_INPUT || dom_form_ancestor(node) != form)
            continue;
        const char *name = dom_get_attr(node, "name");
        const char *type = dom_get_attr(node, "type");
        if (!name[0] || b_equal_ci(type, "submit") ||
            b_equal_ci(type, "button") || b_equal_ci(type, "reset") ||
            b_equal_ci(type, "image") || b_equal_ci(type, "file") ||
            b_equal_ci(type, "checkbox") || b_equal_ci(type, "radio"))
            continue;
        char enc_name[256];
        char enc_value[320];
        url_encode_text(name, enc_name, sizeof(enc_name));
        url_encode_text(dom_get_attr(node, "value"), enc_value,
                        sizeof(enc_value));
        b_append(url, max, *first ? "?" : "&");
        *first = 0;
        b_append(url, max, enc_name);
        b_append(url, max, "=");
        b_append(url, max, enc_value);
    }
}

static void browser_submit_form(int node) {
    int form = dom_form_ancestor(node);
    if (form < 0) return;
    char action[MAX_URL];
    const char *attr_action = dom_get_attr(form, "action");
    if (attr_action[0]) {
        if (!resolve_href(attr_action, action, sizeof(action))) return;
    } else {
        b_copy(action, sizeof(action), page_url);
        for (int i = 0; action[i]; i++)
            if (action[i] == '#') { action[i] = '\0'; break; }
    }
    int first = 1;
    for (int i = 0; action[i]; i++) {
        if (action[i] == '?') { first = 0; break; }
    }
    char target[MAX_URL + 768];
    b_copy(target, sizeof(target), action);
    form_append_fields(form, target, sizeof(target), &first);
    browser_load(target, true);
}

static void form_focus_input(int node) {
    if (node < 0 || node >= dom_node_used) return;
    form_input_node = node;
    b_copy(form_input, sizeof(form_input), dom_get_attr(node, "value"));
    form_input_len = b_strlen(form_input);
    address_focus = false;
    address_select_all = false;
    layout_dirty = 1;
}

static void browser_back(void) {
    if (history_pos <= 0) return;
    history_pos--;
    browser_load(history[history_pos], false);
}

static void browser_home(void) {
    show_home();
}

static void open_local_dialog(void) {
    char path[MAX_LOCAL_PATH];
    path[0] = '\0';
    if (!gui_open_dialog(path, sizeof(path))) return;
    browser_open_file(path);
}

void browser_open_file(const char *path) {
    if (!path || !path[0]) return;
    browser_load(path, true);
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

static void page_scroll_to(int value) {
    int page_h = view_page_h;
    int max_scroll = content_height - page_h;
    if (max_scroll < 0) max_scroll = 0;
    if (value < 0) value = 0;
    if (value > max_scroll) value = max_scroll;
    scroll_y = value;
}

static int dom_is_descendant_of(int node, int ancestor) {
    if (node < 0 || ancestor < 0) return 0;
    int depth = 0;
    for (int n = node; n >= 0 && n < dom_node_used && depth++ < 128;
         n = dom_nodes[n].parent) {
        if (n == ancestor) return 1;
        if (n == 0) break;
    }
    return 0;
}

static void apply_pending_anchor(int page_y) {
    if (!anchor_pending) return;
    anchor_pending = false;
    if (!pending_anchor[0]) { page_scroll_to(0); return; }

    int target = -1;
    for (int i = 0; i < dom_node_used; i++) {
        if (dom_nodes[i].type != DOM_ELEMENT) continue;
        if (b_equal_ci(dom_get_attr(i, "id"), pending_anchor) ||
            (dom_nodes[i].tag == DOM_A &&
             b_equal_ci(dom_get_attr(i, "name"), pending_anchor))) {
            target = i;
            break;
        }
    }
    if (target < 0) {
        char msg[128];
        b_copy(msg, sizeof(msg), "Section not found: ");
        b_append(msg, sizeof(msg), pending_anchor);
        set_status(msg, true);
        return;
    }

    int target_y = -1;
    for (int i = 0; i < layout_item_count; i++) {
        if (!dom_is_descendant_of(layout_items[i].node, target)) continue;
        if (target_y < 0 || layout_items[i].y < target_y)
            target_y = layout_items[i].y;
    }
    if (target_y >= 0) page_scroll_to(target_y - page_y - 6);
    char msg[128];
    b_copy(msg, sizeof(msg), "Section: ");
    b_append(msg, sizeof(msg), pending_anchor);
    set_status(msg, false);
}

static void page_click(const struct browser_layout *L, int x, int y) {
    int content_w = L->content_w - 2;
    if (content_w < 40) content_w = 40;
    if (layout_dirty || layout_width != content_w)
        layout_build(L->content_x + 1, L->page_y, content_w);
    int yy = y + scroll_y;
    for (int i = layout_hit_count - 1; i >= 0; i--) {
        struct hit_rect *h = &layout_hits[i];
        if (x < h->x || x >= h->x + h->w || yy < h->y || yy >= h->y + h->h)
            continue;
        if (h->kind == 3) {
            const char *type = dom_get_attr(h->node, "type");
            if (b_equal_ci(type, "submit") || b_equal_ci(type, "button")) {
                browser_submit_form(h->node);
            } else {
                form_focus_input(h->node);
            }
            return;
        }
        if (h->kind == 2) {
            int r = browser_js_click(h->node);
            layout_dirty = 1;
            if (r < 0) {
                set_status(browser_js_last_error(), true);
            } else if (r > 0) {
                if (browser_js_pending_timers() > 0) browser_js_fire_timers();
                set_status("Script ran.", false);
            } else {
                browser_submit_form(h->node);
            }
            return;
        }
        if (h->kind == 1) {
            const char *href = dom_get_attr(h->node, "href");
            char target[MAX_URL];
            if (resolve_href(href, target, sizeof(target))) {
                if (dom_get_attr(h->node, "download")[0] ||
                    looks_like_download(target)) {
                    if (download_url(target, 0, 0) < 0)
                        set_status("Download failed.", true);
                    return;
                }
            }
            browser_follow(href);
            return;
        }
    }
}

void browser_handle_key(char key) {
    unsigned char k = (unsigned char)key;

    if (k == 0x0C) {
        form_input_node = -1;
        address_focus = true;
        address_select_all = true;
        return;
    }
    if (k == 0x13) {
        browser_download_current();
        return;
    }
    if (k == 0x12) {
        if (home_active) show_home();
        else if (page_url[0]) browser_load(page_url, false);
        else browser_load(address, false);
        return;
    }
    if (form_input_node >= 0 && form_input_node < dom_node_used) {
        if (k == 0x1B) {
            form_input_node = -1;
            layout_dirty = 1;
            return;
        }
        if (k == 0x09) {
            form_input_node = -1;
            address_focus = true;
            address_select_all = true;
            return;
        }
        if (k == '\n' || k == '\r') {
            int node = form_input_node;
            form_input_node = -1;
            browser_submit_form(node);
            return;
        }
        if ((k == 0x08 || k == 0x7F) && form_input_len > 0) {
            form_input[--form_input_len] = '\0';
            dom_set_attribute(form_input_node, "value", form_input);
            return;
        }
        if (k >= 32 && k < 127) {
            if (form_input_len < (int)sizeof(form_input) - 1) {
                form_input[form_input_len++] = (char)k;
                form_input[form_input_len] = '\0';
                dom_set_attribute(form_input_node, "value", form_input);
            }
        }
        return;
    }
    if (k == 0x01 && address_focus) {
        address_select_all = true;
        return;
    }
    if (k == 0x1B) {
        address_focus = false;
        address_select_all = false;
        return;
    }
    if (k == 0x09) {
        address_focus = !address_focus;
        address_select_all = address_focus;
        return;
    }
    if (k == 0x80) { page_scroll_to(scroll_y - 44); return; }
    if (k == 0x81) { page_scroll_to(scroll_y + 44); return; }
    if (k == 0x82) {
        if (address_focus && address_cursor > 0) address_cursor--;
        return;
    }
    if (k == 0x83) {
        if (address_focus && address_cursor < address_len) address_cursor++;
        return;
    }
    if (k == '\n' || k == '\r') {
        browser_load(address, true);
        address_focus = false;
        address_select_all = false;
        return;
    }
    if (k == 0x08 || k == 0x7F) {
        if (!address_focus) {
            if (k == 0x08) browser_back();
            return;
        }
        if (address_select_all) {
            address_len = 0;
            address_cursor = 0;
            address[0] = '\0';
            address_select_all = false;
            return;
        }
        if (k == 0x08) {
            if (address_cursor > 0) {
                for (int i = address_cursor - 1; i < address_len - 1; i++)
                    address[i] = address[i + 1];
                address_cursor--;
                address_len--;
                address[address_len] = '\0';
            }
        } else if (address_cursor < address_len) {
            for (int i = address_cursor; i < address_len - 1; i++)
                address[i] = address[i + 1];
            address_len--;
            address[address_len] = '\0';
        }
        return;
    }
    if (k >= 32 && k < 127) {
        if (!address_focus) {
            address_focus = true;
            address_select_all = true;
        }
        if (address_select_all) {
            address_len = 0;
            address_cursor = 0;
            address[0] = '\0';
            address_select_all = false;
        }
        if (address_len < MAX_ADDRESS - 1) {
            for (int i = address_len; i > address_cursor; i--)
                address[i] = address[i - 1];
            address[address_cursor] = (char)k;
            address_len++;
            address_cursor++;
            address[address_len] = '\0';
        }
    }
}

void browser_handle_mouse(int x, int y, int w, int h, int mx, int my) {
    struct browser_layout L;
    browser_layout(x, y, w, h, &L);

    if (my >= L.nav_y && my < L.nav_y + L.nav_h) {
        if (mx >= L.back_x && mx < L.back_x + L.back_w) {
            browser_back();
            return;
        }
        if (mx >= L.home_x && mx < L.home_x + L.home_w) {
            browser_home();
            return;
        }
        if (mx >= L.open_x && mx < L.open_x + L.open_w) {
            open_local_dialog();
            return;
        }
        if (mx >= L.save_x && mx < L.save_x + L.save_w) {
            browser_download_current();
            return;
        }
        if (mx >= L.settings_x && mx < L.settings_x + L.settings_w) {
            browser_settings_page(true);
            return;
        }
        if (mx >= L.go_x && mx < L.go_x + L.go_w) {
            browser_load(address, true);
            address_focus = false;
            address_select_all = false;
            return;
        }
        if (mx >= L.addr_x && mx < L.addr_x + L.addr_w) {
            address_focus = true;
            address_select_all = false;
            int cursor = (mx - (L.addr_x + 28)) / CHAR_W;
            if (cursor < 0) cursor = 0;
            if (cursor > address_len) cursor = address_len;
            address_cursor = cursor;
            return;
        }
    }

    if (home_active && my >= L.content_y &&
        my < L.content_y + L.content_h &&
        mx >= L.content_x && mx < L.content_x + L.content_w) {
        struct start_layout S;
        compute_start_layout(&L, &S);
        if (my >= S.search_y && my < S.search_y + S.search_h &&
            mx >= S.search_x && mx < S.search_x + S.search_w) {
            address_focus = true;
            address_select_all = true;
            return;
        }
        if (my >= S.button_y && my < S.button_y + S.button_h) {
            if (mx >= S.button1_x && mx < S.button1_x + S.button_w) {
                address_focus = true;
                address_select_all = true;
                return;
            }
            if (mx >= S.button2_x && mx < S.button2_x + S.button_w) {
                open_local_dialog();
                return;
            }
        }
        if (my >= S.shortcut_y && my < S.shortcut_y + S.shortcut_h) {
            for (int i = 0; i < START_SHORTCUTS; i++) {
                if (mx < S.shortcut_x[i] ||
                    mx >= S.shortcut_x[i] + S.shortcut_w) continue;
                if (i == 0)
                    browser_load("https://en.wikipedia.org/wiki/Main_Page", true);
                else if (i == 1)
                    search_load_provider("linux", SEARCH_GITHUB, true);
                else if (i == 2)
                    browser_load("http://example.com/", true);
                else if (i == 3)
                    browser_load("https://en.wikipedia.org/wiki/Special:Random", true);
                else
                    browser_load("file:///welcome.html", true);
                return;
            }
        }
        address_focus = false;
        address_select_all = false;
        return;
    }

    if (!home_active && my >= L.content_y && my < L.content_y + L.content_h &&
        mx >= L.content_x && mx < L.content_x + L.content_w) {
        page_click(&L, mx, my);
        address_focus = false;
        address_select_all = false;
    }
}

void browser_handle_scroll(int x, int y, int w, int h, int notches) {
    (void)x; (void)y; (void)w; (void)h;
    if (home_active) return;
    page_scroll_to(scroll_y - notches * 44);
}

void browser_get_size(int *w, int *h) {
    if (w) *w = BROWSER_W;
    if (h) *h = BROWSER_H;
}

int browser_tick(void) {
    anim_phase++;
    int online = net_available() ? 1 : 0;
    if (online != was_online) {
        was_online = online;
        set_status(online ? "Network connected." : "Network disconnected.",
                   !online);
        return 1;
    }
    if (image_pending_count() > 0) {
        image_load_next();
        return 1;
    }
    if (onload_pending) {
        onload_pending = false;
        if (browser_js_fire_load() < 0) {
            set_status(browser_js_last_error(), true);
        } else {
            set_status(b_starts_ci(page_url, "file://") ? "Local page ready."
                                                         : "Page ready.",
                       false);
        }
        layout_dirty = 1;
        return 1;
    }
    if (browser_js_pending_timers() > 0) {
        if (browser_js_fire_timers() < 0)
            set_status(browser_js_last_error(), true);
        layout_dirty = 1;
        return 1;
    }
    return 0;
}

void browser_init(void) {
    browser_settings_load();
    browser_js_reset();
    history_count = 0;
    history_pos = -1;
    was_online = net_available() ? 1 : 0;
    page_url[0] = '\0';
    page_title[0] = '\0';
    page_bytes_status[0] = '\0';
    page_bytes = 0;
    script_errors = 0;
    anim_phase = 0;
    show_home();
    address_focus = true;
    address_select_all = true;
}
