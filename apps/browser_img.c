#include <solis/apps/browser_img.h>
#include <solis/apps/browser_jpeg.h>
#include <stdint.h>

/*
 * Browser image decoder
 * ---------------------
 * A small, self-contained BMP/PNG/JPEG/GIF/ICO/TGA/PNM/QOI/XBM reader. PNG
 * uses a compact DEFLATE decoder and supports non-interlaced, 8-bit gray, RGB,
 * palette, gray+alpha and RGBA images. Baseline JPEG is handled by
 * browser_jpeg.c. GIF decodes the first frame, including interlaced images,
 * ICO picks the best embedded icon, TGA supports truecolor, grayscale,
 * colormapped and RLE data, PNM covers binary PPM/PGM/PBM, QOI is decoded
 * directly, and XBM is parsed from its C source form. Images are capped before
 * decoding so the browser can keep a fixed number of decoded thumbnails in
 * static memory.
 */

#define PNG_MAX_W 384
#define PNG_MAX_H 256
#define PNG_RAW_MAX (PNG_MAX_W * PNG_MAX_H * 4 + PNG_MAX_H)

struct png_info {
    int width, height;
    int depth, color, interlace;
    int channels;
    int row_bytes;
    int has_palette;
    int has_trns;
    uint8_t palette[256][3];
    uint8_t alpha[256];
};

struct bits {
    const uint8_t *data;
    int len;
    int pos;
    uint32_t buffer;
    int count;
    int error;
};

struct huff {
    uint16_t count[16];
    uint16_t symbol[288];
};

static uint8_t png_raw[PNG_RAW_MAX];

static int read_u32(const uint8_t *p) {
    return ((int)p[0] << 24) | ((int)p[1] << 16) |
           ((int)p[2] << 8) | (int)p[3];
}

static int read_le32(const uint8_t *p) {
    return (int)p[0] | ((int)p[1] << 8) |
           ((int)p[2] << 16) | ((int)p[3] << 24);
}

static int read_le16(const uint8_t *p) {
    return (int)p[0] | ((int)p[1] << 8);
}

static int bits_get(struct bits *b, int n) {
    if (n == 0) return 0;
    while (b->count < n) {
        if (b->pos >= b->len) {
            b->error = 1;
            return 0;
        }
        b->buffer |= (uint32_t)b->data[b->pos++] << b->count;
        b->count += 8;
    }
    int value = (int)(b->buffer & ((1U << n) - 1));
    b->buffer >>= n;
    b->count -= n;
    return value;
}

static void bits_align(struct bits *b) {
    b->buffer = 0;
    b->count = 0;
}

static int huff_build(struct huff *h, const uint8_t *lengths, int n) {
    for (int i = 0; i < 16; i++) h->count[i] = 0;
    for (int i = 0; i < n; i++) h->count[lengths[i]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return -1;
    }
    uint16_t offsets[16];
    offsets[1] = 0;
    for (int len = 1; len < 15; len++)
        offsets[len + 1] = (uint16_t)(offsets[len] + h->count[len]);
    for (int i = 0; i < n; i++) {
        if (lengths[i]) h->symbol[offsets[lengths[i]]++] = (uint16_t)i;
    }
    return 0;
}

static int huff_decode(struct bits *b, const struct huff *h) {
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len < 16; len++) {
        code |= bits_get(b, 1);
        if (b->error) return -1;
        int count = h->count[len];
        if (code - first < count)
            return h->symbol[index + code - first];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    b->error = 1;
    return -1;
}

static int inflate_stream(const uint8_t *src, int src_len, uint8_t *out,
                          int out_max) {
    struct bits b;
    b.data = src;
    b.len = src_len;
    b.pos = 0;
    b.buffer = 0;
    b.count = 0;
    b.error = 0;

    if (src_len < 2) return -1;
    int cmf = src[0];
    int flg = src[1];
    if ((cmf & 0x0F) != 8 || ((cmf << 8) + flg) % 31 != 0) return -1;
    b.pos = 2;
    if (flg & 0x20) {
        if (b.pos + 4 > b.len) return -1;
        b.pos += 4;
    }

    int out_len = 0;
    int final = 0;
    while (!final) {
        final = bits_get(&b, 1);
        int type = bits_get(&b, 2);
        if (b.error) return -1;

        if (type == 0) {
            bits_align(&b);
            if (b.pos + 4 > b.len) return -1;
            int len = (int)b.data[b.pos] | ((int)b.data[b.pos + 1] << 8);
            int nlen = (int)b.data[b.pos + 2] | ((int)b.data[b.pos + 3] << 8);
            b.pos += 4;
            if ((len ^ 0xFFFF) != nlen || b.pos + len > b.len) return -1;
            if (out_len + len > out_max) return -1;
            for (int i = 0; i < len; i++) out[out_len++] = b.data[b.pos++];
            continue;
        }
        if (type != 1 && type != 2) return -1;

        struct huff lit;
        struct huff dist;
        if (type == 1) {
            uint8_t ll[288];
            uint8_t dl[32];
            for (int i = 0; i < 144; i++) ll[i] = 8;
            for (int i = 144; i < 256; i++) ll[i] = 9;
            for (int i = 256; i < 280; i++) ll[i] = 7;
            for (int i = 280; i < 288; i++) ll[i] = 8;
            for (int i = 0; i < 32; i++) dl[i] = 5;
            if (huff_build(&lit, ll, 288) < 0 ||
                huff_build(&dist, dl, 32) < 0) return -1;
        } else {
            int hlit = bits_get(&b, 5) + 257;
            int hdist = bits_get(&b, 5) + 1;
            int hclen = bits_get(&b, 4) + 4;
            if (b.error || hlit > 286 || hdist > 30) return -1;
            static const uint8_t order[19] = {
                16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
            };
            uint8_t cl[19];
            for (int i = 0; i < 19; i++) cl[i] = 0;
            for (int i = 0; i < hclen; i++) cl[order[i]] = (uint8_t)bits_get(&b, 3);
            if (b.error) return -1;
            struct huff clh;
            if (huff_build(&clh, cl, 19) < 0) return -1;

            uint8_t lengths[288 + 32];
            int total = hlit + hdist;
            int at = 0;
            while (at < total) {
                int sym = huff_decode(&b, &clh);
                if (sym < 0) return -1;
                if (sym < 16) {
                    lengths[at++] = (uint8_t)sym;
                } else if (sym == 16) {
                    if (at == 0) return -1;
                    int rep = bits_get(&b, 2) + 3;
                    uint8_t value = lengths[at - 1];
                    while (rep-- > 0 && at < total) lengths[at++] = value;
                } else if (sym == 17) {
                    int rep = bits_get(&b, 3) + 3;
                    while (rep-- > 0 && at < total) lengths[at++] = 0;
                } else if (sym == 18) {
                    int rep = bits_get(&b, 7) + 11;
                    while (rep-- > 0 && at < total) lengths[at++] = 0;
                } else {
                    return -1;
                }
            }
            if (b.error || lengths[256] == 0) return -1;
            if (huff_build(&lit, lengths, hlit) < 0 ||
                huff_build(&dist, lengths + hlit, hdist) < 0) return -1;
        }

        static const int len_base[29] = {
            3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
            35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
        };
        static const int len_extra[29] = {
            0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
            3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
        };
        static const int dist_base[30] = {
            1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
            257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
            8193, 12289, 16385, 24577
        };
        static const int dist_extra[30] = {
            0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
            7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
        };

        for (;;) {
            int sym = huff_decode(&b, &lit);
            if (sym < 0) return -1;
            if (sym < 256) {
                if (out_len >= out_max) return -1;
                out[out_len++] = (uint8_t)sym;
            } else if (sym == 256) {
                break;
            } else {
                sym -= 257;
                if (sym >= 29) return -1;
                int length = len_base[sym] + bits_get(&b, len_extra[sym]);
                int dsym = huff_decode(&b, &dist);
                if (dsym < 0 || dsym >= 30) return -1;
                int distance = dist_base[dsym] + bits_get(&b, dist_extra[dsym]);
                if (b.error || distance <= 0 || distance > out_len) return -1;
                if (out_len + length > out_max) return -1;
                for (int i = 0; i < length; i++) {
                    out[out_len] = out[out_len - distance];
                    out_len++;
                }
            }
        }
    }
    return out_len;
}

static int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

static int png_parse(uint8_t *data, int len, struct png_info *info,
                     uint8_t **zdata, int *zlen) {
    if (len < 8 || data[0] != 0x89 || data[1] != 'P' ||
        data[2] != 'N' || data[3] != 'G' ||
        data[4] != 0x0D || data[5] != 0x0A ||
        data[6] != 0x1A || data[7] != 0x0A)
        return -1;

    int pos = 8;
    int write = 0;
    *zdata = data;
    *zlen = 0;
    info->has_palette = 0;
    info->has_trns = 0;
    for (int i = 0; i < 256; i++) info->alpha[i] = 255;

    while (pos + 12 <= len) {
        int size = read_u32(data + pos);
        if (size < 0 || pos + 12 + size > len) return -1;
        const uint8_t *type = data + pos + 4;
        const uint8_t *body = data + pos + 8;
        if (type[0] == 'I' && type[1] == 'H' && type[2] == 'D' && type[3] == 'R') {
            if (size < 13) return -1;
            info->width = read_u32(body);
            info->height = read_u32(body + 4);
            info->depth = body[8];
            info->color = body[9];
            info->interlace = body[12];
            if (info->width <= 0 || info->height <= 0 ||
                info->width > PNG_MAX_W || info->height > PNG_MAX_H ||
                info->depth != 8 || info->interlace != 0 ||
                body[10] != 0 || body[11] != 0)
                return -1;
            if (info->color == 0 || info->color == 3) info->channels = 1;
            else if (info->color == 2) info->channels = 3;
            else if (info->color == 4) info->channels = 2;
            else if (info->color == 6) info->channels = 4;
            else return -1;
            info->row_bytes = info->width * info->channels;
            if (info->row_bytes > PNG_MAX_W * 4) return -1;
        } else if (type[0] == 'P' && type[1] == 'L' && type[2] == 'T' && type[3] == 'E') {
            if (size < 3 || size / 3 > 256) return -1;
            info->has_palette = 1;
            for (int i = 0; i < size / 3; i++) {
                info->palette[i][0] = body[i * 3];
                info->palette[i][1] = body[i * 3 + 1];
                info->palette[i][2] = body[i * 3 + 2];
            }
        } else if (type[0] == 't' && type[1] == 'R' && type[2] == 'N' && type[3] == 'S') {
            info->has_trns = 1;
            for (int i = 0; i < size && i < 256; i++) info->alpha[i] = body[i];
        } else if (type[0] == 'I' && type[1] == 'D' && type[2] == 'A' && type[3] == 'T') {
            if (write + size > len - 8) return -1;
            for (int i = 0; i < size; i++) data[write + i] = body[i];
            write += size;
            *zlen = write;
        } else if (type[0] == 'I' && type[1] == 'E' && type[2] == 'N' && type[3] == 'D') {
            break;
        }
        pos += 12 + size;
    }

    if (info->width <= 0 || info->height <= 0 || *zlen <= 0) return -1;
    if (info->color == 3 && !info->has_palette) return -1;
    return 0;
}

static int decode_png(uint8_t *data, int len, uint32_t *out,
                      int max_w, int max_h, int *out_w, int *out_h) {
    struct png_info info = {0};
    uint8_t *zdata = 0;
    int zlen = 0;
    if (png_parse(data, len, &info, &zdata, &zlen) < 0) return -1;

    int expected = info.height * (info.row_bytes + 1);
    if (expected > PNG_RAW_MAX) return -1;
    int raw_len = inflate_stream(zdata, zlen, png_raw, PNG_RAW_MAX);
    if (raw_len < expected) return -1;

    int bpp = info.channels;
    for (int y = 0; y < info.height; y++) {
        uint8_t *row = png_raw + y * (info.row_bytes + 1);
        uint8_t *cur = row + 1;
        uint8_t *prev = y > 0 ? png_raw + (y - 1) * (info.row_bytes + 1) + 1 : 0;
        int filter = row[0];
        for (int x = 0; x < info.row_bytes; x++) {
            int left = x >= bpp ? cur[x - bpp] : 0;
            int up = prev ? prev[x] : 0;
            int upleft = prev && x >= bpp ? prev[x - bpp] : 0;
            int value = cur[x];
            if (filter == 1) value += left;
            else if (filter == 2) value += up;
            else if (filter == 3) value += (left + up) / 2;
            else if (filter == 4) value += paeth(left, up, upleft);
            else if (filter != 0) return -1;
            cur[x] = (uint8_t)value;
        }
    }

    int tw = info.width;
    int th = info.height;
    if (tw > max_w || th > max_h) {
        int sw = max_w * 1000 / tw;
        int sh = max_h * 1000 / th;
        int scale = sw < sh ? sw : sh;
        if (scale < 1) scale = 1;
        tw = info.width * scale / 1000;
        th = info.height * scale / 1000;
        if (tw < 1) tw = 1;
        if (th < 1) th = 1;
    }
    if (out_w) *out_w = tw;
    if (out_h) *out_h = th;

    for (int y = 0; y < th; y++) {
        int sy = y * info.height / th;
        uint8_t *row = png_raw + sy * (info.row_bytes + 1) + 1;
        for (int x = 0; x < tw; x++) {
            int sx = x * info.width / tw;
            uint8_t r = 0, g = 0, b = 0, a = 255;
            if (info.color == 0) {
                r = g = b = row[sx];
            } else if (info.color == 2) {
                r = row[sx * 3];
                g = row[sx * 3 + 1];
                b = row[sx * 3 + 2];
            } else if (info.color == 3) {
                int index = row[sx];
                r = info.palette[index][0];
                g = info.palette[index][1];
                b = info.palette[index][2];
                if (info.has_trns) a = info.alpha[index];
            } else if (info.color == 4) {
                r = g = b = row[sx * 2];
                a = row[sx * 2 + 1];
            } else {
                r = row[sx * 4];
                g = row[sx * 4 + 1];
                b = row[sx * 4 + 2];
                a = row[sx * 4 + 3];
            }
            if (a < 255) {
                r = (uint8_t)((r * a + 255 * (255 - a)) / 255);
                g = (uint8_t)((g * a + 255 * (255 - a)) / 255);
                b = (uint8_t)((b * a + 255 * (255 - a)) / 255);
            }
            out[y * tw + x] = 0xFF000000U | ((uint32_t)r << 16) |
                              ((uint32_t)g << 8) | b;
        }
    }
    return 0;
}

static int mask_shift(uint32_t mask) {
    if (!mask) return 0;
    int shift = 0;
    while (!(mask & 1)) { mask >>= 1; shift++; }
    return shift;
}

static int mask_bits(uint32_t mask) {
    int bits = 0;
    while (mask) { bits += (int)(mask & 1); mask >>= 1; }
    return bits;
}

static int decode_bmp(const uint8_t *data, int len, uint32_t *out,
                      int max_w, int max_h, int *out_w, int *out_h) {
    if (len < 54 || data[0] != 'B' || data[1] != 'M') return -1;
    int offset = read_le32(data + 10);
    int dib = read_le32(data + 14);
    int width = read_le32(data + 18);
    int height = read_le32(data + 22);
    int bpp = read_le16(data + 28);
    int compression = read_le32(data + 30);
    if (dib < 40 || (compression != 0 && compression != 3) ||
        (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 &&
         bpp != 32)) return -1;
    int top_down = height < 0;
    if (height < 0) height = -height;
    /* Large wallpapers and camera exports are accepted; the output is sampled
     * down to the caller's thumbnail size without allocating a source frame. */
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return -1;
    int row_size = ((width * bpp + 31) / 32) * 4;
    if (offset < 0 || row_size <= 0 || offset + row_size * height > len) return -1;

    uint8_t palette[256][4];
    for (int i = 0; i < 256; i++) {
        palette[i][0] = palette[i][1] = palette[i][2] = 0;
        palette[i][3] = 255;
    }
    if (bpp <= 8) {
        int colors = read_le32(data + 46);
        if (colors <= 0 || colors > 256) colors = 1 << bpp;
        int palette_at = 14 + dib;
        if (palette_at + colors * 4 > len) return -1;
        for (int i = 0; i < colors; i++) {
            palette[i][0] = data[palette_at + i * 4];
            palette[i][1] = data[palette_at + i * 4 + 1];
            palette[i][2] = data[palette_at + i * 4 + 2];
            palette[i][3] = data[palette_at + i * 4 + 3];
        }
    }

    uint32_t rmask = 0, gmask = 0, bmask = 0, amask = 0;
    if (bpp == 16) {
        rmask = 0x7C00; gmask = 0x03E0; bmask = 0x001F;
        if (compression == 3) {
            int at = 14 + dib;
            if (at + 12 > len) return -1;
            rmask = (uint32_t)read_le32(data + at);
            gmask = (uint32_t)read_le32(data + at + 4);
            bmask = (uint32_t)read_le32(data + at + 8);
        }
    } else if (bpp == 32) {
        rmask = 0x00FF0000; gmask = 0x0000FF00; bmask = 0x000000FF;
        if (compression == 3) {
            int at = 14 + dib;
            if (at + 12 > len) return -1;
            rmask = (uint32_t)read_le32(data + at);
            gmask = (uint32_t)read_le32(data + at + 4);
            bmask = (uint32_t)read_le32(data + at + 8);
            if (at + 16 <= len) amask = (uint32_t)read_le32(data + at + 12);
        }
    }
    int rshift = mask_shift(rmask), gshift = mask_shift(gmask);
    int bshift = mask_shift(bmask), ashift = mask_shift(amask);
    int rbits = mask_bits(rmask), gbits = mask_bits(gmask);
    int bbits = mask_bits(bmask), abits = mask_bits(amask);

    int tw = width;
    int th = height;
    if (tw > max_w || th > max_h) {
        int sw = max_w * 1000 / tw;
        int sh = max_h * 1000 / th;
        int scale = sw < sh ? sw : sh;
        if (scale < 1) scale = 1;
        tw = width * scale / 1000;
        th = height * scale / 1000;
        if (tw < 1) tw = 1;
        if (th < 1) th = 1;
    }
    if (out_w) *out_w = tw;
    if (out_h) *out_h = th;

    for (int y = 0; y < th; y++) {
        int sy = y * height / th;
        int src_y = top_down ? sy : height - 1 - sy;
        const uint8_t *row = data + offset + src_y * row_size;
        for (int x = 0; x < tw; x++) {
            int sx = x * width / tw;
            uint8_t r = 0, g = 0, b = 0, a = 255;
            if (bpp <= 8) {
                int index;
                if (bpp == 8) {
                    index = row[sx];
                } else {
                    int bit = sx * bpp;
                    index = (row[bit >> 3] >> (8 - bpp - (bit & 7))) &
                            ((1 << bpp) - 1);
                }
                b = palette[index][0]; g = palette[index][1];
                r = palette[index][2];
                /* The fourth palette byte is reserved, not alpha. Treating it
                 * as alpha made ordinary 8-bit BMPs blend into white. */
                a = 255;
            } else if (bpp == 16) {
                const uint8_t *px = row + sx * 2;
                uint32_t v = (uint32_t)px[0] | ((uint32_t)px[1] << 8);
                int rv = (int)((v & rmask) >> rshift);
                int gv = (int)((v & gmask) >> gshift);
                int bv = (int)((v & bmask) >> bshift);
                r = (uint8_t)(rbits ? rv * 255 / ((1 << rbits) - 1) : 0);
                g = (uint8_t)(gbits ? gv * 255 / ((1 << gbits) - 1) : 0);
                b = (uint8_t)(bbits ? bv * 255 / ((1 << bbits) - 1) : 0);
            } else {
                const uint8_t *px = row + sx * (bpp / 8);
                uint32_t v = (uint32_t)px[0] | ((uint32_t)px[1] << 8) |
                             ((uint32_t)px[2] << 16);
                if (bpp == 32) v |= (uint32_t)px[3] << 24;
                r = (uint8_t)((v & rmask) >> rshift);
                g = (uint8_t)((v & gmask) >> gshift);
                b = (uint8_t)((v & bmask) >> bshift);
                if (amask && abits)
                    a = (uint8_t)(((v & amask) >> ashift) * 255 / ((1 << abits) - 1));
            }
            if (a < 255) {
                r = (uint8_t)((r * a + 255 * (255 - a)) / 255);
                g = (uint8_t)((g * a + 255 * (255 - a)) / 255);
                b = (uint8_t)((b * a + 255 * (255 - a)) / 255);
            }
            out[y * tw + x] = 0xFF000000U | ((uint32_t)r << 16) |
                              ((uint32_t)g << 8) | b;
        }
    }
    return 0;
}

#define GIF_MAX_SIDE 4096

struct gif_bits {
    const uint8_t *data;
    int len;
    int pos;
    int block_left;
    uint32_t buffer;
    int count;
    int error;
};

struct gif_canvas {
    uint32_t *out;
    int out_w, out_h;
    int screen_w, screen_h;
    int image_w, image_h;
    int image_x, image_y;
    int row, col;
    int interlaced;
    int transparent;
};

static uint16_t gif_prefix[4096];
static uint8_t gif_suffix[4096];
/* The SPX app stack is deliberately tiny; keep the LZW output stack out of it. */
static uint8_t gif_stack[4096];

static int gif_block_byte(struct gif_bits *b) {
    while (b->block_left == 0) {
        if (b->pos >= b->len) { b->error = 1; return 0; }
        int n = b->data[b->pos++];
        if (n == 0 || b->pos + n > b->len) { b->error = 1; return 0; }
        b->block_left = n;
    }
    b->block_left--;
    return b->data[b->pos++];
}

static int gif_bits_get(struct gif_bits *b, int n) {
    while (b->count < n) {
        int v = gif_block_byte(b);
        if (b->error) return 0;
        b->buffer |= (uint32_t)v << b->count;
        b->count += 8;
    }
    int value = (int)(b->buffer & ((1U << n) - 1));
    b->buffer >>= n;
    b->count -= n;
    return value;
}

static int gif_row_for(int row, int height, int interlaced) {
    if (!interlaced) return row;
    int p1 = (height + 7) / 8;
    int p2 = (height + 3) / 8;
    int p3 = (height + 1) / 4;
    if (row < p1) return row * 8;
    row -= p1;
    if (row < p2) return 4 + row * 8;
    row -= p2;
    if (row < p3) return 2 + row * 4;
    row -= p3;
    return 1 + row * 2;
}

static void gif_emit(struct gif_canvas *c, const uint8_t palette[256][3], int index) {
    if (c->col >= c->image_w) {
        c->col = 0;
        c->row++;
    }
    if (c->row >= c->image_h) return;
    int x = c->image_x + c->col++;
    int y = c->image_y + gif_row_for(c->row, c->image_h, c->interlaced);
    if (x < 0 || y < 0 || x >= c->screen_w || y >= c->screen_h) return;
    if (index == c->transparent) return;
    int ox = x * c->out_w / c->screen_w;
    int oy = y * c->out_h / c->screen_h;
    if (ox >= c->out_w || oy >= c->out_h) return;
    /* Write only the sample pixel chosen for this output cell. */
    if (x != ox * c->screen_w / c->out_w ||
        y != oy * c->screen_h / c->out_h) return;
    c->out[oy * c->out_w + ox] = 0xFF000000U |
        ((uint32_t)palette[index][0] << 16) |
        ((uint32_t)palette[index][1] << 8) | palette[index][2];
}

static int gif_lzw(struct gif_bits *b, int min_code, struct gif_canvas *canvas,
                   const uint8_t palette[256][3]) {
    uint8_t *stack = gif_stack;
    int clear = 1 << min_code;
    int end = clear + 1;
    int next = end + 1;
    int code_size = min_code + 1;
    int old = -1;

    for (int i = 0; i < clear; i++) {
        gif_prefix[i] = 0;
        gif_suffix[i] = (uint8_t)i;
    }

    for (;;) {
        int code = gif_bits_get(b, code_size);
        if (b->error || code == end) break;
        if (code == clear) {
            next = end + 1;
            code_size = min_code + 1;
            old = -1;
            continue;
        }
        int in_code = code;
        if (old < 0) {
            if (code >= clear) { b->error = 1; break; }
            gif_emit(canvas, palette, code);
            old = code;
            continue;
        }
        int sp = 0;
        if (code >= next) {
            if (code != next) { b->error = 1; break; }
            stack[sp++] = gif_suffix[old];
            code = old;
        }
        while (code >= clear) {
            if (code >= next || sp >= 4095) { b->error = 1; return -1; }
            stack[sp++] = gif_suffix[code];
            code = gif_prefix[code];
        }
        if (sp >= 4095) { b->error = 1; return -1; }
        stack[sp++] = gif_suffix[code];
        while (sp > 0) gif_emit(canvas, palette, stack[--sp]);
        if (next < 4096) {
            gif_prefix[next] = (uint16_t)old;
            gif_suffix[next] = gif_suffix[code];
            next++;
            if (next == (1 << code_size) && code_size < 12) code_size++;
        }
        old = in_code;
    }
    return b->error ? -1 : 0;
}

static int decode_gif(const uint8_t *data, int len, uint32_t *out,
                      int max_w, int max_h, int *out_w, int *out_h) {
    if (len < 13 || data[0] != 'G' || data[1] != 'I' || data[2] != 'F') return -1;
    int screen_w = read_le16(data + 6);
    int screen_h = read_le16(data + 8);
    if (screen_w <= 0 || screen_h <= 0 ||
        screen_w > GIF_MAX_SIDE || screen_h > GIF_MAX_SIDE) return -1;
    int packed = data[10];
    int has_global = (packed & 0x80) != 0;
    int global_size = has_global ? 3 * (1 << ((packed & 7) + 1)) : 0;
    int pos = 13;
    if (pos + global_size > len) return -1;
    uint8_t palette[256][3];
    for (int i = 0; i < 256; i++) palette[i][0] = palette[i][1] = palette[i][2] = 255;
    for (int i = 0; has_global && i < global_size / 3 && i < 256; i++) {
        palette[i][0] = data[pos + i * 3];
        palette[i][1] = data[pos + i * 3 + 1];
        palette[i][2] = data[pos + i * 3 + 2];
    }
    pos += global_size;

    int transparent = -1;
    int tw = screen_w, th = screen_h;
    if (tw > max_w || th > max_h) {
        int sw = max_w * 1000 / tw;
        int sh = max_h * 1000 / th;
        int scale = sw < sh ? sw : sh;
        if (scale < 1) scale = 1;
        tw = screen_w * scale / 1000;
        th = screen_h * scale / 1000;
        if (tw < 1) tw = 1;
        if (th < 1) th = 1;
    }
    for (int i = 0; i < tw * th; i++) out[i] = 0xFFFFFFFF;

    while (pos < len) {
        int block = data[pos++];
        if (block == 0x3B) break;
        if (block == 0x21) {
            if (pos >= len) break;
            int label = data[pos++];
            if (label == 0xF9 && pos + 1 < len) {
                int n = data[pos++];
                if (n >= 4 && pos + n <= len) {
                    int gce = data[pos];
                    if (gce & 1) transparent = data[pos + 3];
                }
                pos += n;
                if (pos < len) pos++;
            } else {
                while (pos < len) {
                    int n = data[pos++];
                    if (n == 0) break;
                    pos += n;
                }
            }
            continue;
        }
        if (block != 0x2C) break;
        if (pos + 9 > len) return -1;
        int left = read_le16(data + pos);
        int top = read_le16(data + pos + 2);
        int iw = read_le16(data + pos + 4);
        int ih = read_le16(data + pos + 6);
        int ipacked = data[pos + 8];
        pos += 9;
        int local = (ipacked & 0x80) != 0;
        int lsize = local ? 3 * (1 << ((ipacked & 7) + 1)) : 0;
        if (local) {
            if (pos + lsize > len) return -1;
            for (int i = 0; i < lsize / 3 && i < 256; i++) {
                palette[i][0] = data[pos + i * 3];
                palette[i][1] = data[pos + i * 3 + 1];
                palette[i][2] = data[pos + i * 3 + 2];
            }
            pos += lsize;
        }
        if (iw <= 0 || ih <= 0 || iw > GIF_MAX_SIDE || ih > GIF_MAX_SIDE ||
            pos >= len) return -1;
        int min_code = data[pos++];
        if (min_code < 2 || min_code > 8) return -1;
        struct gif_canvas canvas;
        canvas.out = out;
        canvas.out_w = tw;
        canvas.out_h = th;
        canvas.screen_w = screen_w;
        canvas.screen_h = screen_h;
        canvas.image_w = iw;
        canvas.image_h = ih;
        canvas.image_x = left;
        canvas.image_y = top;
        canvas.row = 0;
        canvas.col = 0;
        canvas.interlaced = (ipacked & 0x40) != 0;
        canvas.transparent = transparent;
        struct gif_bits bits;
        bits.data = data;
        bits.len = len;
        bits.pos = pos;
        bits.block_left = 0;
        bits.buffer = 0;
        bits.count = 0;
        bits.error = 0;
        if (gif_lzw(&bits, min_code, &canvas, palette) < 0) return -1;
        if (out_w) *out_w = tw;
        if (out_h) *out_h = th;
        return 0;
    }
    return -1;
}

static int ico_entry_score(const uint8_t *e) {
    int w = e[0] ? e[0] : 256;
    int h = e[1] ? e[1] : 256;
    int bpp = read_le16(e + 6);
    return w * h * (bpp >= 32 ? 3 : bpp >= 24 ? 2 : 1);
}

static int decode_ico(const uint8_t *data, int len, uint32_t *out,
                      int max_w, int max_h, int *out_w, int *out_h) {
    if (len < 22 || read_le16(data) != 0 || read_le16(data + 2) != 1) return -1;
    int count = read_le16(data + 4);
    if (count <= 0 || count > 32 || 6 + count * 16 > len) return -1;
    int best = -1, best_score = -1;
    for (int i = 0; i < count; i++) {
        const uint8_t *e = data + 6 + i * 16;
        int bytes = read_le32(e + 8);
        int offset = read_le32(e + 12);
        if (bytes <= 0 || offset < 0 || offset + bytes > len) continue;
        int score = ico_entry_score(e);
        if (score > best_score) { best_score = score; best = i; }
    }
    if (best < 0) return -1;
    const uint8_t *e = data + 6 + best * 16;
    int bytes = read_le32(e + 8);
    int offset = read_le32(e + 12);
    const uint8_t *body = data + offset;
    if (bytes >= 8 && body[0] == 0x89 && body[1] == 'P' &&
        body[2] == 'N' && body[3] == 'G')
        return decode_png((uint8_t *)body, bytes, out, max_w, max_h, out_w, out_h);
    if (bytes < 40) return -1;
    int dib = read_le32(body);
    int width = read_le32(body + 4);
    int height = read_le32(body + 8) / 2;
    int bpp = read_le16(body + 14);
    int compression = read_le32(body + 16);
    if (dib < 40 || width <= 0 || height <= 0 ||
        width > 1024 || height > 1024 || compression != 0 ||
        (bpp != 8 && bpp != 24 && bpp != 32)) return -1;
    int palette_count = bpp == 8 ? (read_le32(body + 32) ? read_le32(body + 32) : 256) : 0;
    int pixel_at = dib + palette_count * 4;
    int row_size = ((width * bpp + 31) / 32) * 4;
    if (pixel_at < 0 || pixel_at + row_size * height > bytes) return -1;
    int tw = width, th = height;
    if (tw > max_w || th > max_h) {
        int sw = max_w * 1000 / tw;
        int sh = max_h * 1000 / th;
        int scale = sw < sh ? sw : sh;
        if (scale < 1) scale = 1;
        tw = width * scale / 1000;
        th = height * scale / 1000;
        if (tw < 1) tw = 1;
        if (th < 1) th = 1;
    }
    for (int y = 0; y < th; y++) {
        int sy = y * height / th;
        const uint8_t *row = body + pixel_at + (height - 1 - sy) * row_size;
        for (int x = 0; x < tw; x++) {
            int sx = x * width / tw;
            uint8_t r = 0, g = 0, b = 0, a = 255;
            if (bpp == 8) {
                int index = row[sx];
                const uint8_t *pal = body + dib + index * 4;
                b = pal[0]; g = pal[1]; r = pal[2];
            } else {
                const uint8_t *px = row + sx * (bpp / 8);
                b = px[0]; g = px[1]; r = px[2];
                if (bpp == 32) a = px[3];
            }
            if (a < 255) {
                r = (uint8_t)((r * a + 255 * (255 - a)) / 255);
                g = (uint8_t)((g * a + 255 * (255 - a)) / 255);
                b = (uint8_t)((b * a + 255 * (255 - a)) / 255);
            }
            out[y * tw + x] = 0xFF000000U | ((uint32_t)r << 16) |
                              ((uint32_t)g << 8) | b;
        }
    }
    if (out_w) *out_w = tw;
    if (out_h) *out_h = th;
    return 0;
}

static void image_fit(int sw, int sh, int max_w, int max_h,
                      int *tw, int *th) {
    *tw = sw;
    *th = sh;
    if (*tw > max_w || *th > max_h) {
        int swp = max_w * 1000 / *tw;
        int shp = max_h * 1000 / *th;
        int scale = swp < shp ? swp : shp;
        if (scale < 1) scale = 1;
        *tw = sw * scale / 1000;
        *th = sh * scale / 1000;
        if (*tw < 1) *tw = 1;
        if (*th < 1) *th = 1;
    }
}

/* Write the one source pixel that represents this output cell. This keeps
 * scaled RLE/streaming decoders from doing a full-size framebuffer write. */
static void image_sample(uint32_t *out, int tw, int th, int sw, int sh,
                         int x, int y, uint32_t color) {
    int ox = x * tw / sw;
    int oy = y * th / sh;
    if (ox < 0 || oy < 0 || ox >= tw || oy >= th) return;
    if (x != ox * sw / tw || y != oy * sh / th) return;
    out[oy * tw + ox] = color;
}

static uint8_t expand5(int v) {
    return (uint8_t)((v << 3) | (v >> 2));
}

#define TGA_MAX_SIDE 8192

static uint32_t tga_color(const uint8_t *p, int bpp, int desc) {
    uint8_t r, g, b, a = 255;
    if (bpp == 8) {
        r = g = b = p[0];
    } else if (bpp == 16) {
        uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8);
        b = expand5((int)(v & 31));
        g = expand5((int)((v >> 5) & 31));
        r = expand5((int)((v >> 10) & 31));
    } else if (bpp == 24) {
        b = p[0]; g = p[1]; r = p[2];
    } else {
        b = p[0]; g = p[1]; r = p[2];
        if (desc & 0x0f) a = p[3];
    }
    if (a < 255) {
        r = (uint8_t)((r * a + 255 * (255 - a)) / 255);
        g = (uint8_t)((g * a + 255 * (255 - a)) / 255);
        b = (uint8_t)((b * a + 255 * (255 - a)) / 255);
    }
    return 0xFF000000U | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static int decode_tga(const uint8_t *data, int len, uint32_t *out,
                      int max_w, int max_h, int *out_w, int *out_h) {
    if (len < 18) return -1;
    int id_len = data[0];
    int cmap_type = data[1];
    int type = data[2];
    int cmap_first = read_le16(data + 3);
    int cmap_len = read_le16(data + 5);
    int cmap_depth = data[7];
    int width = read_le16(data + 12);
    int height = read_le16(data + 14);
    int bpp = data[16];
    int desc = data[17];
    int rle = type == 9 || type == 10 || type == 11;
    int base = rle ? type - 8 : type;
    if (width <= 0 || height <= 0 || width > TGA_MAX_SIDE ||
        height > TGA_MAX_SIDE) return -1;
    if (base != 1 && base != 2 && base != 3) return -1;
    if (base == 1 && bpp != 8) return -1;
    if (base == 2 && bpp != 16 && bpp != 24 && bpp != 32) return -1;
    if (base == 3 && bpp != 8) return -1;
    if (cmap_type && base != 1) return -1;
    int cmap_bytes = (cmap_depth + 7) / 8;
    if (base == 1 && (cmap_bytes != 2 && cmap_bytes != 3 && cmap_bytes != 4))
        return -1;
    if (cmap_len < 0 || cmap_len > 256) cmap_len = cmap_len < 0 ? 0 : 256;
    int cmap_at = 18 + id_len;
    if (cmap_at + cmap_len * cmap_bytes > len) return -1;
    uint8_t palette[256][4];
    for (int i = 0; i < 256; i++)
        palette[i][0] = palette[i][1] = palette[i][2] = 255, palette[i][3] = 255;
    for (int i = 0; i < cmap_len; i++) {
        const uint8_t *p = data + cmap_at + i * cmap_bytes;
        uint32_t c = tga_color(p, cmap_bytes * 8, 8);
        palette[i][0] = (uint8_t)(c >> 16);
        palette[i][1] = (uint8_t)(c >> 8);
        palette[i][2] = (uint8_t)c;
        palette[i][3] = (uint8_t)(c >> 24);
    }
    int bytes = bpp / 8;
    int off = cmap_at + (base == 1 ? cmap_len * cmap_bytes : 0);
    int tw, th;
    image_fit(width, height, max_w, max_h, &tw, &th);
    if (out_w) *out_w = tw;
    if (out_h) *out_h = th;
    for (int i = 0; i < tw * th; i++) out[i] = 0xFFFFFFFF;

    if (!rle) {
        if (off < 0 || off + width * height * bytes > len) return -1;
        for (int y = 0; y < th; y++) {
            int sy = y * height / th;
            int src_y = (desc & 0x20) ? sy : height - 1 - sy;
            for (int x = 0; x < tw; x++) {
                int sx = x * width / tw;
                int src_x = (desc & 0x10) ? width - 1 - sx : sx;
                const uint8_t *p = data + off + (src_y * width + src_x) * bytes;
                uint32_t c;
                if (base == 1) {
                    int idx = p[0] - cmap_first;
                    if (idx < 0 || idx >= cmap_len) c = 0xFFFFFFFF;
                    else c = 0xFF000000U | ((uint32_t)palette[idx][0] << 16) |
                             ((uint32_t)palette[idx][1] << 8) | palette[idx][2];
                } else {
                    c = tga_color(p, bpp, desc);
                }
                out[y * tw + x] = c;
            }
        }
        return 0;
    }

    int pos = off;
    int x = 0, y = 0;
    while (y < height) {
        if (pos >= len) return -1;
        int head = data[pos++];
        int count = (head & 0x7f) + 1;
        int raw = (head & 0x80) != 0;
        int px_bytes = base == 1 ? 1 : bytes;
        if (raw) {
            if (pos + count * px_bytes > len) return -1;
            for (int n = 0; n < count && y < height; n++) {
                const uint8_t *p = data + pos;
                uint32_t c;
                if (base == 1) {
                    int idx = p[0] - cmap_first;
                    c = (idx >= 0 && idx < cmap_len) ?
                        0xFF000000U | ((uint32_t)palette[idx][0] << 16) |
                        ((uint32_t)palette[idx][1] << 8) | palette[idx][2] :
                        0xFFFFFFFF;
                } else c = tga_color(p, bpp, desc);
                int sx = (desc & 0x10) ? width - 1 - x : x;
                int sy = (desc & 0x20) ? y : height - 1 - y;
                image_sample(out, tw, th, width, height, sx, sy, c);
                pos += px_bytes;
                x++;
                if (x >= width) { x = 0; y++; }
            }
        } else {
            if (pos + px_bytes > len) return -1;
            const uint8_t *p = data + pos;
            uint32_t c;
            if (base == 1) {
                int idx = p[0] - cmap_first;
                c = (idx >= 0 && idx < cmap_len) ?
                    0xFF000000U | ((uint32_t)palette[idx][0] << 16) |
                    ((uint32_t)palette[idx][1] << 8) | palette[idx][2] :
                    0xFFFFFFFF;
            } else c = tga_color(p, bpp, desc);
            pos += px_bytes;
            for (int n = 0; n < count && y < height; n++) {
                int sx = (desc & 0x10) ? width - 1 - x : x;
                int sy = (desc & 0x20) ? y : height - 1 - y;
                image_sample(out, tw, th, width, height, sx, sy, c);
                x++;
                if (x >= width) { x = 0; y++; }
            }
        }
    }
    return 0;
}

#define PNM_MAX_SIDE 8192

static int pnm_skip_space(const uint8_t *data, int len, int *pos) {
    while (*pos < len &&
           (data[*pos] == ' ' || data[*pos] == '\t' ||
            data[*pos] == '\r' || data[*pos] == '\n')) (*pos)++;
    return 0;
}

static int pnm_number(const uint8_t *data, int len, int *pos, int *value) {
    for (;;) {
        pnm_skip_space(data, len, pos);
        if (*pos < len && data[*pos] == '#') {
            while (*pos < len && data[*pos] != '\n') (*pos)++;
            continue;
        }
        break;
    }
    if (*pos >= len || data[*pos] < '0' || data[*pos] > '9') return -1;
    int v = 0;
    while (*pos < len && data[*pos] >= '0' && data[*pos] <= '9') {
        v = v * 10 + (data[*pos] - '0');
        if (v > 100000000) return -1;
        (*pos)++;
    }
    *value = v;
    return 0;
}

static int decode_pnm(const uint8_t *data, int len, uint32_t *out,
                      int max_w, int max_h, int *out_w, int *out_h) {
    if (len < 4 || data[0] != 'P' ||
        (data[1] != '4' && data[1] != '5' && data[1] != '6')) return -1;
    int kind = data[1] - '0';
    int pos = 2;
    int width, height, maxval = 1;
    if (pnm_number(data, len, &pos, &width) < 0 ||
        pnm_number(data, len, &pos, &height) < 0) return -1;
    if (kind != 4 && pnm_number(data, len, &pos, &maxval) < 0) return -1;
    if (width <= 0 || height <= 0 || width > PNM_MAX_SIDE ||
        height > PNM_MAX_SIDE || maxval <= 0 || maxval > 65535) return -1;
    /* Exactly one separator follows the last header token. Do not skip all
     * whitespace here: the first binary sample may itself be whitespace. */
    if (pos >= len) return -1;
    if (data[pos] == '\r' && pos + 1 < len && data[pos + 1] == '\n') pos += 2;
    else if (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\r' ||
             data[pos] == '\n') pos++;

    int tw, th;
    image_fit(width, height, max_w, max_h, &tw, &th);
    if (out_w) *out_w = tw;
    if (out_h) *out_h = th;
    for (int i = 0; i < tw * th; i++) out[i] = 0xFFFFFFFF;

    if (kind == 4) {
        int row_bytes = (width + 7) / 8;
        if (pos + row_bytes * height > len) return -1;
        for (int y = 0; y < th; y++) {
            const uint8_t *row = data + pos + (y * height / th) * row_bytes;
            for (int x = 0; x < tw; x++) {
                int sx = x * width / tw;
                int bit = (row[sx >> 3] >> (7 - (sx & 7))) & 1;
                out[y * tw + x] = bit ? 0xFF000000U : 0xFFFFFFFFU;
            }
        }
        return 0;
    }

    int channels = kind == 6 ? 3 : 1;
    int sample_bytes = maxval < 256 ? 1 : 2;
    int row_bytes = width * channels * sample_bytes;
    if (pos + row_bytes * height > len) return -1;
    for (int y = 0; y < th; y++) {
        const uint8_t *row = data + pos + (y * height / th) * row_bytes;
        for (int x = 0; x < tw; x++) {
            int sx = x * width / tw;
            const uint8_t *p = row + sx * channels * sample_bytes;
            int r, g, b;
            if (channels == 1) {
                r = g = b = sample_bytes == 1 ? p[0] : (p[0] << 8) | p[1];
            } else if (sample_bytes == 1) {
                r = p[0]; g = p[1]; b = p[2];
            } else {
                r = (p[0] << 8) | p[1];
                g = (p[2] << 8) | p[3];
                b = (p[4] << 8) | p[5];
            }
            r = r * 255 / maxval;
            g = g * 255 / maxval;
            b = b * 255 / maxval;
            out[y * tw + x] = 0xFF000000U | ((uint32_t)r << 16) |
                              ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    return 0;
}

#define QOI_MAX_SIDE 4096

static int read_be32(const uint8_t *p) {
    return ((int)p[0] << 24) | ((int)p[1] << 16) |
           ((int)p[2] << 8) | (int)p[3];
}

static int decode_qoi(const uint8_t *data, int len, uint32_t *out,
                      int max_w, int max_h, int *out_w, int *out_h) {
    if (len < 14 || data[0] != 'q' || data[1] != 'o' ||
        data[2] != 'i' || data[3] != 'f') return -1;
    int width = read_be32(data + 4);
    int height = read_be32(data + 8);
    int channels = data[12];
    if (width <= 0 || height <= 0 || width > QOI_MAX_SIDE ||
        height > QOI_MAX_SIDE || (channels != 3 && channels != 4) ||
        width * height > 16 * 1024 * 1024) return -1;

    int tw, th;
    image_fit(width, height, max_w, max_h, &tw, &th);
    if (out_w) *out_w = tw;
    if (out_h) *out_h = th;
    for (int i = 0; i < tw * th; i++) out[i] = 0xFFFFFFFF;

    uint8_t index[64][4];
    for (int i = 0; i < 64; i++)
        index[i][0] = index[i][1] = index[i][2] = index[i][3] = 0;
    int r = 0, g = 0, b = 0, a = 255;
    int x = 0, y = 0, run = 0;
    int pos = 14;
    while (y < height) {
        if (run > 0) {
            run--;
        } else {
            if (pos >= len) return -1;
            int b1 = data[pos++];
            if (b1 == 0xFE) {
                if (pos + 3 > len) return -1;
                r = data[pos++]; g = data[pos++]; b = data[pos++];
            } else if (b1 == 0xFF) {
                if (pos + 4 > len) return -1;
                r = data[pos++]; g = data[pos++]; b = data[pos++]; a = data[pos++];
            } else if ((b1 & 0xC0) == 0x00) {
                r = index[b1][0]; g = index[b1][1];
                b = index[b1][2]; a = index[b1][3];
            } else if ((b1 & 0xC0) == 0x40) {
                r += ((b1 >> 4) & 3) - 2;
                g += ((b1 >> 2) & 3) - 2;
                b += (b1 & 3) - 2;
            } else if ((b1 & 0xC0) == 0x80) {
                if (pos >= len) return -1;
                int b2 = data[pos++];
                int dg = (b1 & 0x3f) - 32;
                r += dg + ((b2 >> 4) & 0x0f) - 8;
                g += dg;
                b += dg + (b2 & 0x0f) - 8;
            } else {
                run = b1 & 0x3f;
            }
            int h = (r * 3 + g * 5 + b * 7 + a * 11) & 63;
            index[h][0] = (uint8_t)r; index[h][1] = (uint8_t)g;
            index[h][2] = (uint8_t)b; index[h][3] = (uint8_t)a;
        }
        image_sample(out, tw, th, width, height, x, y,
                     0xFF000000U | ((uint32_t)r << 16) |
                     ((uint32_t)g << 8) | (uint32_t)b);
        x++;
        if (x >= width) { x = 0; y++; }
    }
    return 0;
}

#define XBM_MAX_SIDE 4096

static int xbm_find(const uint8_t *data, int len, const char *needle) {
    int n = 0;
    while (needle[n]) n++;
    for (int i = 0; i + n <= len; i++) {
        int j = 0;
        while (j < n && data[i + j] == (uint8_t)needle[j]) j++;
        if (j == n) return i;
    }
    return -1;
}

static int xbm_int(const uint8_t *data, int len, const char *key) {
    int at = xbm_find(data, len, key);
    if (at < 0) return -1;
    int key_len = 0;
    while (key[key_len]) key_len++;
    at += key_len;
    while (at < len && (data[at] == ' ' || data[at] == '\t' || data[at] == '=')) at++;
    if (at >= len || data[at] < '0' || data[at] > '9') return -1;
    int v = 0;
    while (at < len && data[at] >= '0' && data[at] <= '9') {
        v = v * 10 + (data[at] - '0');
        at++;
    }
    return v;
}

static int xbm_hex(const uint8_t *data, int len, int *pos, int *value) {
    while (*pos < len &&
           (data[*pos] == ' ' || data[*pos] == '\t' ||
            data[*pos] == '\r' || data[*pos] == '\n' || data[*pos] == ','))
        (*pos)++;
    if (*pos + 1 >= len || data[*pos] != '0' ||
        (data[*pos + 1] != 'x' && data[*pos + 1] != 'X')) return -1;
    *pos += 2;
    int v = 0, digits = 0;
    while (*pos < len) {
        int c = data[*pos], d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = (v << 4) | d;
        digits++;
        (*pos)++;
    }
    if (digits == 0) return -1;
    *value = v & 0xff;
    return 0;
}

static int decode_xbm(const uint8_t *data, int len, uint32_t *out,
                      int max_w, int max_h, int *out_w, int *out_h) {
    if (xbm_find(data, len, "#define") < 0) return -1;
    int width = xbm_int(data, len, "_width");
    int height = xbm_int(data, len, "_height");
    if (width <= 0 || height <= 0 || width > XBM_MAX_SIDE ||
        height > XBM_MAX_SIDE || width * height > 16 * 1024 * 1024) return -1;
    int brace = xbm_find(data, len, "{");
    if (brace < 0) return -1;
    int pos = brace + 1;
    int row_bytes = (width + 7) / 8;
    if (row_bytes > 512) return -1;
    uint8_t row[512];
    int tw, th;
    image_fit(width, height, max_w, max_h, &tw, &th);
    if (out_w) *out_w = tw;
    if (out_h) *out_h = th;
    for (int i = 0; i < tw * th; i++) out[i] = 0xFFFFFFFF;
    for (int y = 0; y < height; y++) {
        for (int i = 0; i < row_bytes; i++) {
            int v;
            if (xbm_hex(data, len, &pos, &v) < 0) return -1;
            row[i] = (uint8_t)v;
        }
        for (int x = 0; x < width; x++) {
            int bit = (row[x >> 3] >> (x & 7)) & 1;
            image_sample(out, tw, th, width, height, x, y,
                         bit ? 0xFF000000U : 0xFFFFFFFFU);
        }
    }
    return 0;
}

const char *browser_image_format(const uint8_t *data, int len) {
    if (!data) return "image";
    if (len >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
        data[3] == 'G') return "PNG";
    if (len >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
        return "JPEG";
    if (len >= 6 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F')
        return "GIF";
    if (len >= 6 && data[0] == 0 && data[1] == 0 && data[2] == 1 &&
        data[3] == 0) return "ICO";
    if (len >= 2 && data[0] == 'B' && data[1] == 'M') return "BMP";
    if (len >= 14 && data[0] == 'q' && data[1] == 'o' && data[2] == 'i' &&
        data[3] == 'f') return "QOI";
    if (len >= 4 && data[0] == 'P' &&
        (data[1] == '4' || data[1] == '5' || data[1] == '6')) {
        if (data[1] == '6') return "PPM";
        if (data[1] == '5') return "PGM";
        return "PBM";
    }
    if (len >= 18) {
        int type = data[2];
        int bpp = data[16];
        int w = read_le16(data + 12);
        int h = read_le16(data + 14);
        if ((type == 1 || type == 2 || type == 3 || type == 9 ||
             type == 10 || type == 11) && w > 0 && h > 0 &&
            (bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32))
            return "TGA";
    }
    if (len >= 8 && xbm_find(data, len, "#define") >= 0 &&
        xbm_find(data, len, "_width") >= 0 &&
        xbm_find(data, len, "_height") >= 0) return "XBM";
    return "image";
}

int browser_image_decode(uint8_t *data, int len, uint32_t *out,
                         int max_w, int max_h, int *out_w, int *out_h) {
    if (!data || len <= 0 || !out || max_w <= 0 || max_h <= 0) return -1;
    if (len >= 8 && data[0] == 0x89 && data[1] == 'P' &&
        data[2] == 'N' && data[3] == 'G')
        return decode_png(data, len, out, max_w, max_h, out_w, out_h);
    if (len >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
        return browser_jpeg_decode(data, len, out, max_w, max_h, out_w, out_h);
    if (len >= 2 && data[0] == 'B' && data[1] == 'M')
        return decode_bmp(data, len, out, max_w, max_h, out_w, out_h);
    if (len >= 6 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F')
        return decode_gif(data, len, out, max_w, max_h, out_w, out_h);
    if (len >= 6 && data[0] == 0 && data[1] == 0 && data[2] == 1 &&
        data[3] == 0)
        return decode_ico(data, len, out, max_w, max_h, out_w, out_h);
    if (len >= 14 && data[0] == 'q' && data[1] == 'o' && data[2] == 'i' &&
        data[3] == 'f')
        return decode_qoi(data, len, out, max_w, max_h, out_w, out_h);
    if (len >= 4 && data[0] == 'P' &&
        (data[1] == '4' || data[1] == '5' || data[1] == '6'))
        return decode_pnm(data, len, out, max_w, max_h, out_w, out_h);
    if (len >= 18 && decode_tga(data, len, out, max_w, max_h, out_w, out_h) == 0)
        return 0;
    if (len >= 8 && xbm_find(data, len, "#define") >= 0)
        return decode_xbm(data, len, out, max_w, max_h, out_w, out_h);
    return -1;
}
