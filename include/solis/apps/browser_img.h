#ifndef SOLIS_APPS_BROWSER_IMG_H
#define SOLIS_APPS_BROWSER_IMG_H

#include <stdint.h>

/* Decode a BMP, PNG, baseline JPEG, GIF (first frame), ICO, TGA, PPM/PGM/PBM,
 * QOI, or XBM image into a tightly packed ARGB buffer. The decoded image is
 * scaled down, preserving aspect ratio, to fit max_w x max_h. The input buffer
 * is scratch space: PNG IDAT chunks are compacted in place so the decoder does
 * not need a second compressed-image buffer.
 *
 * Returns 0 on success and fills out_w/out_h.  Returns -1 for unsupported or
 * malformed data, oversized images, or buffers that are too small. */
int browser_image_decode(uint8_t *data, int len, uint32_t *out,
                         int max_w, int max_h, int *out_w, int *out_h);

/* Return a short format label without decoding pixels. */
const char *browser_image_format(const uint8_t *data, int len);

#endif
