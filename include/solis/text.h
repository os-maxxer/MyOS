#ifndef SOLIS_TEXT_H
#define SOLIS_TEXT_H

#include <solis/types.h>
#include <stdbool.h>

/* Decode one UTF-8 sequence. Writes the codepoint to *cp and returns the
 * number of bytes consumed (1..4). Invalid or truncated sequences are
 * replaced with U+FFFD and consume exactly one byte, so callers can always
 * make progress even on a corrupt buffer. Returns 0 at the terminator. */
int text_decode(const char *s, uint32_t *cp);

/* Encode one codepoint as UTF-8 into out (at least 4 bytes). Returns the
 * number of bytes written, or 0 for an invalid codepoint. */
int text_encode(uint32_t cp, char *out);

/* Width in pixels of one codepoint in the built-in text grid. */
int text_advance(uint32_t cp);

/* True for double-width scripts (CJK, Hangul, fullwidth forms, emoji). */
bool text_is_wide(uint32_t cp);

/* Pixel width of a UTF-8 string, and of its first len bytes. Both stop at
 * the terminator and never split a sequence: an incomplete tail counts as
 * one replacement cell. Newlines reset the run, so the result is the width
 * of a single line. */
int text_width(const char *s);
int text_width_bytes(const char *s, int len);

/* Copy at most max_px pixels worth of whole UTF-8 sequences from src to dst
 * (dst must hold max_px / 8 + 1 bytes for narrow text). Always terminates. */
int text_clip(const char *src, char *dst, int dst_max, int max_px);

/* Glyph lookup used by graphics_draw_string. Returns 16 rows of width/8
 * bytes each (8 pixels per byte, most significant bit leftmost): 16 bytes
 * for a normal cell, 32 for a double-width one. The returned pointer may
 * refer to shared scratch storage, so draw it before the next call. */
const uint8_t *text_glyph(uint32_t cp, int *width);

#endif
