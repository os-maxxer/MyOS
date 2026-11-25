#include <solis/apps/browser_jpeg.h>
#include <stdint.h>

/*
 * Baseline JPEG decoder
 * ---------------------
 * This is deliberately a baseline-only decoder: SOF0, eight-bit samples,
 * Huffman coding, and 1/3 components with 1x1, 2x1, 1x2 or 2x2 sampling.
 * Progressive JPEG and arithmetic coding are rejected.
 *
 * An MCU is decoded into small 8x8 component blocks and converted straight
 * into the scaled ARGB destination.  Consequently a 4000x3000 camera image
 * costs no more static memory than a 100x100 one.  This matters because SPX
 * applications share a fixed 2 MiB slot.
 *
 * The integer IDCT constants and the two-pass transform follow the public
 * NanoJPEG decoder by Martin Fiedler (KeyJ), adapted for direct MCU output.
 * NanoJPEG is published under KeyJ's Research License 0.2; its terms permit
 * use, modification and redistribution and require this attribution.
 */

#define JP_MAX_COMPS 3
#define JP_MAX_BLOCKS 4
#define JP_MAX_DIM 16384

struct jp_huff {
    uint8_t counts[17];
    uint8_t symbols[256];
    int nsym;
};

struct jp_component {
    int id;
    int h, v;
    int tq;
    int dc_tbl;
    int ac_tbl;
    int dc_pred;
};

struct jp_state {
    const uint8_t *data;
    int len;
    int pos;

    int width, height;
    int ncomp;
    int max_h, max_v;
    int mcu_w, mcu_h;
    int mcux, mcuy;

    struct jp_component comp[JP_MAX_COMPS];
    uint8_t qt[4][64];             /* zig-zag order, as stored in DQT */
    struct jp_huff huff[2][4];
    int restart_interval;
    int restart_left;
    int next_restart;

    uint32_t bitbuf;
    int bitcnt;
    int failed;

    uint8_t blocks[JP_MAX_COMPS][JP_MAX_BLOCKS][64];
    uint32_t *out;
    int out_w, out_h;
};

/* zig-zag index -> natural 8x8 index */
static const uint8_t jp_zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

static int jp_clip(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return value;
}

#define JP_W1 2841
#define JP_W2 2676
#define JP_W3 2408
#define JP_W5 1609
#define JP_W6 1108
#define JP_W7 565

static void jp_row_idct(int *blk) {
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = blk[4] << 11) |
          (x2 = blk[6]) |
          (x3 = blk[2]) |
          (x4 = blk[1]) |
          (x5 = blk[7]) |
          (x6 = blk[5]) |
          (x7 = blk[3]))) {
        blk[0] = blk[1] = blk[2] = blk[3] = blk[4] = blk[5] = blk[6] =
            blk[7] = blk[0] << 3;
        return;
    }
    x0 = (blk[0] << 11) + 128;
    x8 = JP_W7 * (x4 + x5);
    x4 = x8 + (JP_W1 - JP_W7) * x4;
    x5 = x8 - (JP_W1 + JP_W7) * x5;
    x8 = JP_W3 * (x6 + x7);
    x6 = x8 - (JP_W3 - JP_W5) * x6;
    x7 = x8 - (JP_W3 + JP_W5) * x7;
    x8 = x0 + x1;
    x0 -= x1;
    x1 = JP_W6 * (x3 + x2);
    x2 = x1 - (JP_W2 + JP_W6) * x2;
    x3 = x1 + (JP_W2 - JP_W6) * x3;
    x1 = x4 + x6;
    x4 -= x6;
    x6 = x5 + x7;
    x5 -= x7;
    x7 = x8 + x3;
    x8 -= x3;
    x3 = x0 + x2;
    x0 -= x2;
    x2 = (181 * (x4 + x5) + 128) >> 8;
    x4 = (181 * (x4 - x5) + 128) >> 8;
    blk[0] = (x7 + x1) >> 8;
    blk[1] = (x3 + x2) >> 8;
    blk[2] = (x0 + x4) >> 8;
    blk[3] = (x8 + x6) >> 8;
    blk[4] = (x8 - x6) >> 8;
    blk[5] = (x0 - x4) >> 8;
    blk[6] = (x3 - x2) >> 8;
    blk[7] = (x7 - x1) >> 8;
}

static void jp_col_idct(const int *blk, uint8_t *out, int stride) {
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = blk[8 * 4] << 8) |
          (x2 = blk[8 * 6]) |
          (x3 = blk[8 * 2]) |
          (x4 = blk[8 * 1]) |
          (x5 = blk[8 * 7]) |
          (x6 = blk[8 * 5]) |
          (x7 = blk[8 * 3]))) {
        x1 = jp_clip(((blk[0] + 32) >> 6) + 128);
        for (x0 = 8; x0; --x0) {
            *out = (uint8_t)x1;
            out += stride;
        }
        return;
    }
    x0 = (blk[0] << 8) + 8192;
    x8 = JP_W7 * (x4 + x5) + 4;
    x4 = (x8 + (JP_W1 - JP_W7) * x4) >> 3;
    x5 = (x8 - (JP_W1 + JP_W7) * x5) >> 3;
    x8 = JP_W3 * (x6 + x7) + 4;
    x6 = (x8 - (JP_W3 - JP_W5) * x6) >> 3;
    x7 = (x8 - (JP_W3 + JP_W5) * x7) >> 3;
    x8 = x0 + x1;
    x0 -= x1;
    x1 = JP_W6 * (x3 + x2) + 4;
    x2 = (x1 - (JP_W2 + JP_W6) * x2) >> 3;
    x3 = (x1 + (JP_W2 - JP_W6) * x3) >> 3;
    x1 = x4 + x6;
    x4 -= x6;
    x6 = x5 + x7;
    x5 -= x7;
    x7 = x8 + x3;
    x8 -= x3;
    x3 = x0 + x2;
    x0 -= x2;
    x2 = (181 * (x4 + x5) + 128) >> 8;
    x4 = (181 * (x4 - x5) + 128) >> 8;
    *out = (uint8_t)jp_clip(((x7 + x1) >> 14) + 128); out += stride;
    *out = (uint8_t)jp_clip(((x3 + x2) >> 14) + 128); out += stride;
    *out = (uint8_t)jp_clip(((x0 + x4) >> 14) + 128); out += stride;
    *out = (uint8_t)jp_clip(((x8 + x6) >> 14) + 128); out += stride;
    *out = (uint8_t)jp_clip(((x8 - x6) >> 14) + 128); out += stride;
    *out = (uint8_t)jp_clip(((x0 - x4) >> 14) + 128); out += stride;
    *out = (uint8_t)jp_clip(((x3 - x2) >> 14) + 128); out += stride;
    *out = (uint8_t)jp_clip(((x7 - x1) >> 14) + 128);
}

static void jp_idct(const int *coef, uint8_t *out) {
    int tmp[64];
    for (int i = 0; i < 64; i++) tmp[i] = coef[i];
    for (int y = 0; y < 8; y++) jp_row_idct(&tmp[y * 8]);
    for (int x = 0; x < 8; x++) jp_col_idct(&tmp[x], out + x, 8);
}

static uint8_t jp_next_byte(struct jp_state *s) {
    if (s->pos >= s->len) {
        s->failed = 1;
        return 0;
    }
    uint8_t b = s->data[s->pos++];
    if (b == 0xFF) {
        if (s->pos >= s->len) {
            s->failed = 1;
            return 0;
        }
        uint8_t next = s->data[s->pos];
        if (next == 0x00) {
            s->pos++;
        } else {
            /* A marker inside entropy data (or a restart marker encountered
             * at the wrong time) is not a pixel byte. */
            s->failed = 1;
            return 0;
        }
    }
    return b;
}

static int jp_getbit(struct jp_state *s) {
    if (s->bitcnt == 0) {
        s->bitbuf = jp_next_byte(s);
        s->bitcnt = 8;
        if (s->failed) return 0;
    }
    int bit = (int)((s->bitbuf >> 7) & 1);
    s->bitbuf = (s->bitbuf << 1) & 0xFF;
    s->bitcnt--;
    return bit;
}

static int jp_getbits(struct jp_state *s, int n) {
    int value = 0;
    for (int i = 0; i < n; i++)
        value = (value << 1) | jp_getbit(s);
    return value;
}

static int jp_extend(int value, int bits) {
    if (bits == 0) return 0;
    if (value < (1 << (bits - 1)))
        value -= (1 << bits) - 1;
    return value;
}

static int jp_huff_decode(struct jp_state *s, const struct jp_huff *h) {
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= 16; len++) {
        code = (code << 1) | jp_getbit(s);
        if (s->failed) return -1;
        int count = h->counts[len];
        if (code - first < count) {
            int at = index + code - first;
            if (at < 0 || at >= h->nsym) return -1;
            return h->symbols[at];
        }
        index += count;
        first = (first + count) << 1;
    }
    return -1;
}

static int jp_get_marker(struct jp_state *s) {
    while (s->pos < s->len) {
        if (s->data[s->pos++] != 0xFF) {
            /* Allow garbage between segments, but only until the next FF. */
            continue;
        }
        while (s->pos < s->len && s->data[s->pos] == 0xFF) s->pos++;
        if (s->pos >= s->len) return -1;
        return s->data[s->pos++];
    }
    return -1;
}

static void jp_sync_restart(struct jp_state *s) {
    s->bitcnt = 0;
    if (s->pos + 1 >= s->len ||
        s->data[s->pos] != 0xFF ||
        (s->data[s->pos + 1] & 0xF8) != 0xD0 ||
        (s->data[s->pos + 1] & 7) != s->next_restart) {
        s->failed = 1;
        return;
    }
    s->pos += 2;
    s->next_restart = (s->next_restart + 1) & 7;
    for (int i = 0; i < s->ncomp; i++) s->comp[i].dc_pred = 0;
}

static int jp_decode_block(struct jp_state *s, struct jp_component *c,
                           uint8_t *out) {
    int coef[64];
    for (int i = 0; i < 64; i++) coef[i] = 0;

    int t = jp_huff_decode(s, &s->huff[0][c->dc_tbl]);
    if (t < 0 || t > 15) return -1;
    int diff = jp_extend(jp_getbits(s, t), t);
    c->dc_pred += diff;
    coef[0] = c->dc_pred * s->qt[c->tq][0];

    int k = 1;
    while (k < 64) {
        int rs = jp_huff_decode(s, &s->huff[1][c->ac_tbl]);
        if (rs < 0) return -1;
        int run = rs >> 4;
        int size = rs & 15;
        if (size == 0) {
            if (run == 15) {
                k += 16;
                if (k > 64) return -1;
                continue;
            }
            break;
        }
        k += run;
        if (k >= 64) return -1;
        int value = jp_extend(jp_getbits(s, size), size);
        coef[jp_zigzag[k]] = value * s->qt[c->tq][k];
        k++;
    }
    jp_idct(coef, out);
    return 0;
}

static int jp_parse_dqt(struct jp_state *s, int length) {
    int end = s->pos + length - 2;
    while (s->pos < end) {
        int info = s->data[s->pos++];
        int precision = info >> 4;
        int id = info & 15;
        if (precision != 0 || id >= 4 || s->pos + 64 > end) return -1;
        for (int i = 0; i < 64; i++) s->qt[id][i] = s->data[s->pos++];
    }
    return s->pos == end ? 0 : -1;
}

static int jp_parse_dht(struct jp_state *s, int length) {
    int end = s->pos + length - 2;
    while (s->pos < end) {
        if (s->pos + 17 > end) return -1;
        int info = s->data[s->pos++];
        int tc = info >> 4;
        int th = info & 15;
        if (tc > 1 || th >= 4) return -1;
        struct jp_huff *h = &s->huff[tc][th];
        int total = 0;
        for (int i = 1; i <= 16; i++) {
            h->counts[i] = s->data[s->pos++];
            total += h->counts[i];
        }
        h->counts[0] = 0;
        if (total > 256 || s->pos + total > end) return -1;
        for (int i = 0; i < total; i++) h->symbols[i] = s->data[s->pos++];
        h->nsym = total;
    }
    return s->pos == end ? 0 : -1;
}

static int jp_parse_sof(struct jp_state *s, int length) {
    int payload = s->pos;
    if (length < 11 || payload + length - 2 > s->len) return -1;
    if (s->data[payload] != 8) return -1;
    s->height = (s->data[payload + 1] << 8) | s->data[payload + 2];
    s->width = (s->data[payload + 3] << 8) | s->data[payload + 4];
    s->ncomp = s->data[payload + 5];
    if (s->width <= 0 || s->height <= 0 ||
        s->width > JP_MAX_DIM || s->height > JP_MAX_DIM ||
        (s->ncomp != 1 && s->ncomp != 3)) return -1;
    if (length < 8 + s->ncomp * 3) return -1;
    s->pos = payload + 6;

    s->max_h = 0;
    s->max_v = 0;
    for (int i = 0; i < s->ncomp; i++) {
        struct jp_component *c = &s->comp[i];
        c->id = s->data[s->pos++];
        int hv = s->data[s->pos++];
        c->h = hv >> 4;
        c->v = hv & 15;
        c->tq = s->data[s->pos++];
        if (c->h < 1 || c->h > 2 || c->v < 1 || c->v > 2 ||
            c->tq >= 4) return -1;
        if (c->h > s->max_h) s->max_h = c->h;
        if (c->v > s->max_v) s->max_v = c->v;
        c->dc_pred = 0;
    }
    s->pos = payload + length - 2;
    s->mcu_w = s->max_h * 8;
    s->mcu_h = s->max_v * 8;
    s->mcux = (s->width + s->mcu_w - 1) / s->mcu_w;
    s->mcuy = (s->height + s->mcu_h - 1) / s->mcu_h;
    if (s->mcux <= 0 || s->mcuy <= 0 ||
        (long)s->mcux * s->mcuy > 300000L) return -1;
    return 0;
}

static int jp_parse_sos(struct jp_state *s, int length) {
    int payload = s->pos;
    if (length < 6 || payload + length - 2 > s->len) return -1;
    int ns = s->data[payload];
    s->pos = payload + 1;
    if (ns != s->ncomp) return -1;
    for (int i = 0; i < ns; i++) {
        if (s->pos + 2 > s->len) return -1;
        int id = s->data[s->pos++];
        int tables = s->data[s->pos++];
        int dc = tables >> 4;
        int ac = tables & 15;
        if (dc >= 4 || ac >= 4) return -1;
        int found = -1;
        for (int j = 0; j < s->ncomp; j++)
            if (s->comp[j].id == id) { found = j; break; }
        if (found < 0) return -1;
        s->comp[found].dc_tbl = dc;
        s->comp[found].ac_tbl = ac;
    }
    if (s->pos + 3 > s->len) return -1;
    if (s->data[s->pos] != 0 || s->data[s->pos + 1] != 63 ||
        s->data[s->pos + 2] != 0) return -1;
    s->pos = payload + length - 2;
    return 0;
}

static int jp_decode_scan(struct jp_state *s) {
    s->bitcnt = 0;
    s->bitbuf = 0;
    s->failed = 0;
    s->restart_left = s->restart_interval;
    s->next_restart = 0;

    for (int my = 0; my < s->mcuy; my++) {
        for (int mx = 0; mx < s->mcux; mx++) {
            if (s->restart_interval) {
                if (s->restart_left == 0) {
                    jp_sync_restart(s);
                    if (s->failed) return -1;
                    s->restart_left = s->restart_interval;
                }
            }

            for (int ci = 0; ci < s->ncomp; ci++) {
                struct jp_component *c = &s->comp[ci];
                for (int by = 0; by < c->v; by++) {
                    for (int bx = 0; bx < c->h; bx++) {
                        int bi = by * c->h + bx;
                        if (jp_decode_block(s, c, s->blocks[ci][bi]) < 0 ||
                            s->failed) return -1;
                    }
                }
            }

            int sx0 = mx * s->mcu_w;
            int sy0 = my * s->mcu_h;
            int sx1 = sx0 + s->mcu_w;
            int sy1 = sy0 + s->mcu_h;
            if (sx1 > s->width) sx1 = s->width;
            if (sy1 > s->height) sy1 = s->height;

            int ox0 = (sx0 * s->out_w + s->width - 1) / s->width;
            int ox1 = (sx1 * s->out_w + s->width - 1) / s->width;
            int oy0 = (sy0 * s->out_h + s->height - 1) / s->height;
            int oy1 = (sy1 * s->out_h + s->height - 1) / s->height;
            if (ox0 < 0) ox0 = 0;
            if (oy0 < 0) oy0 = 0;
            if (ox1 > s->out_w) ox1 = s->out_w;
            if (oy1 > s->out_h) oy1 = s->out_h;

            for (int oy = oy0; oy < oy1; oy++) {
                int sy = ((2 * oy + 1) * s->height) / (2 * s->out_h);
                if (sy < sy0 || sy >= sy1) continue;
                int yby = (sy - sy0) >> 3;
                int yly = (sy - sy0) & 7;
                struct jp_component *cy = &s->comp[0];
                int yblock = yby * cy->h;
                for (int ox = ox0; ox < ox1; ox++) {
                    int sx = ((2 * ox + 1) * s->width) / (2 * s->out_w);
                    if (sx < sx0 || sx >= sx1) continue;
                    int ybx = (sx - sx0) >> 3;
                    int ylx = (sx - sx0) & 7;
                    int yi = yblock + ybx;
                    if (yi >= JP_MAX_BLOCKS) continue;
                    int Y = s->blocks[0][yi][yly * 8 + ylx];
                    int r = Y, g = Y, b = Y;
                    if (s->ncomp == 3) {
                        int cx = (sx - sx0) * s->comp[1].h / s->max_h;
                        int cyy = (sy - sy0) * s->comp[1].v / s->max_v;
                        int cbx = cx >> 3;
                        int cby = cyy >> 3;
                        int cbix = cbx + cby * s->comp[1].h;
                        int crx = (sx - sx0) * s->comp[2].h / s->max_h;
                        int cry = (sy - sy0) * s->comp[2].v / s->max_v;
                        int crbx = crx >> 3;
                        int crby = cry >> 3;
                        int crix = crbx + crby * s->comp[2].h;
                        if (cbix >= JP_MAX_BLOCKS || crix >= JP_MAX_BLOCKS)
                            continue;
                        int Cb = s->blocks[1][cbix][(cyy & 7) * 8 + (cx & 7)] - 128;
                        int Cr = s->blocks[2][crix][(cry & 7) * 8 + (crx & 7)] - 128;
                        int yv = Y * 256;
                        r = jp_clip((yv + 359 * Cr + 128) >> 8);
                        g = jp_clip((yv - 88 * Cb - 183 * Cr + 128) >> 8);
                        b = jp_clip((yv + 454 * Cb + 128) >> 8);
                    }
                    s->out[oy * s->out_w + ox] = 0xFF000000U |
                        ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                }
            }

            if (s->restart_interval && s->restart_left > 0)
                s->restart_left--;
        }
    }
    return 0;
}

int browser_jpeg_decode(const uint8_t *data, int len, uint32_t *out,
                        int max_w, int max_h, int *out_w, int *out_h) {
    if (!data || len < 4 || !out || max_w <= 0 || max_h <= 0)
        return -1;
    if (data[0] != 0xFF || data[1] != 0xD8) return -1;

    struct jp_state s;
    /* Zero the small state, not the destination buffer. */
    uint8_t *raw = (uint8_t *)&s;
    for (int i = 0; i < (int)sizeof(s); i++) raw[i] = 0;
    s.data = data;
    s.len = len;
    s.pos = 2;

    int saw_sof = 0;
    int saw_dqt = 0;
    int saw_dht = 0;

    while (s.pos < s.len) {
        int marker = jp_get_marker(&s);
        if (marker < 0) break;
        if (marker == 0xD9) break;
        if (marker == 0xD8) continue;
        if (marker >= 0xD0 && marker <= 0xD7) continue;
        if (marker == 0x01) continue;

        if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
            if (marker != 0xC0) return -1;
            if (s.pos + 2 > s.len) return -1;
            int length = (s.data[s.pos] << 8) | s.data[s.pos + 1];
            s.pos += 2;
            if (jp_parse_sof(&s, length) < 0) return -1;
            saw_sof = 1;
            continue;
        }
        if (marker == 0xC4) {
            if (s.pos + 2 > s.len) return -1;
            int length = (s.data[s.pos] << 8) | s.data[s.pos + 1];
            s.pos += 2;
            if (jp_parse_dht(&s, length) < 0) return -1;
            saw_dht = 1;
            continue;
        }
        if (marker == 0xDB) {
            if (s.pos + 2 > s.len) return -1;
            int length = (s.data[s.pos] << 8) | s.data[s.pos + 1];
            s.pos += 2;
            if (jp_parse_dqt(&s, length) < 0) return -1;
            saw_dqt = 1;
            continue;
        }
        if (marker == 0xDD) {
            if (s.pos + 4 > s.len) return -1;
            int length = (s.data[s.pos] << 8) | s.data[s.pos + 1];
            if (length < 4) return -1;
            s.restart_interval = (s.data[s.pos + 2] << 8) | s.data[s.pos + 3];
            s.pos += length;
            continue;
        }
        if (marker == 0xDA) {
            if (!saw_sof || !saw_dqt || !saw_dht) return -1;
            if (s.pos + 2 > s.len) return -1;
            int length = (s.data[s.pos] << 8) | s.data[s.pos + 1];
            s.pos += 2;
            if (jp_parse_sos(&s, length) < 0) return -1;

            if (s.width > max_w || s.height > max_h) {
                int sw = max_w * 1000 / s.width;
                int sh = max_h * 1000 / s.height;
                int scale = sw < sh ? sw : sh;
                if (scale < 1) scale = 1;
                s.out_w = s.width * scale / 1000;
                s.out_h = s.height * scale / 1000;
                if (s.out_w < 1) s.out_w = 1;
                if (s.out_h < 1) s.out_h = 1;
            } else {
                s.out_w = s.width;
                s.out_h = s.height;
            }
            if (s.out_w > max_w || s.out_h > max_h) return -1;

            /* Fill the destination before the MCU pass so that any skipped
             * edge samples cannot expose stale pixels. */
            for (int i = 0; i < s.out_w * s.out_h; i++) out[i] = 0xFFFFFFFF;
            s.out = out;

            if (jp_decode_scan(&s) < 0 || s.failed) return -1;
            if (out_w) *out_w = s.out_w;
            if (out_h) *out_h = s.out_h;
            return 0;
        }

        if (s.pos + 2 > s.len) return -1;
        int length = (s.data[s.pos] << 8) | s.data[s.pos + 1];
        if (length < 2 || s.pos + length > s.len) return -1;
        s.pos += length;
    }
    return -1;
}
