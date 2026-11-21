#ifndef SOLIS_APPS_BROWSER_JPEG_H
#define SOLIS_APPS_BROWSER_JPEG_H

#include <stdint.h>

/* Decode a baseline (SOF0) JPEG into a tightly packed ARGB buffer.  The
 * decoder processes one MCU at a time and writes directly into the scaled
 * output, so it does not need a source-sized RGB allocation.  Grayscale and
 * YCbCr images with 4:4:4, 4:2:2 and 4:2:0 sampling are supported.
 *
 * Returns 0 on success and fills out_w/out_h.  Returns -1 for unsupported,
 * progressive, truncated or malformed streams. */
int browser_jpeg_decode(const uint8_t *data, int len, uint32_t *out,
                        int max_w, int max_h, int *out_w, int *out_h);

#endif
