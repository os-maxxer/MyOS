/*
 * Minimal libc for the Solis Doom port.
 *
 * This is deliberately small: it covers the C library calls referenced by
 * the shareware Doom sources, keeps the embedded WAD readable through a
 * tiny stdio shim, and never touches the host operating system.
 */
#include <solis/doom.h>
#include <solis/syscall.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

typedef unsigned long solis_ulong;

struct solis_doom_file {
    unsigned int pos;
    unsigned int length;
    int writable;
};

static struct solis_doom_file wad_stream;
static struct solis_doom_file sink_stream;
FILE *stdin = &sink_stream;
FILE *stdout = &sink_stream;
FILE *stderr = &sink_stream;
int errno;

extern void DG_Error(const char *message);
extern void DG_Quit(void);

static const struct syscall_table *sys(void) {
    return (const struct syscall_table *)SYSCALL_TABLE_ADDR;
}

static const uint8_t *doom_wad_data(void) { return sys()->doom_wad; }
static uint32_t doom_wad_size(void) { return sys()->doom_wad_size; }
static uint32_t timer_get_ticks(void) { return sys()->get_ticks(); }

/* ------------------------------------------------------------------ */
/* Memory                                                              */
/* ------------------------------------------------------------------ */
struct heap_block {
    size_t size;
    size_t used;
};

static size_t heap_used;

static void *heap_alloc(size_t size, size_t align) {
    if (size == 0) size = 1;
    if (align < 16) align = 16;
    size_t start = (heap_used + align - 1) & ~(align - 1);
    if (start + size > SOLIS_DOOM_HEAP_SIZE) return NULL;
    heap_used = start + size;
    return (void *)(uintptr_t)(SOLIS_DOOM_HEAP_BASE + start);
}

void *malloc(size_t size) {
    return heap_alloc(size, 16);
}

void *calloc(size_t count, size_t size) {
    if (count != 0 && size > (size_t)-1 / count) return NULL;
    size_t total = count * size;
    void *memory = malloc(total);
    if (memory) memset(memory, 0, total);
    return memory;
}

void free(void *ptr) {
    (void)ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) return NULL;
    void *next = malloc(size);
    if (!next) return NULL;
    /* The allocator only grows a contiguous arena, so copying the new size is
     * safe: the source is always inside the same reserved region. */
    memcpy(next, ptr, size);
    return next;
}

/* ------------------------------------------------------------------ */
/* Strings                                                             */
/* ------------------------------------------------------------------ */
void *memcpy(void *dst, const void *src, size_t size) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < size; i++) d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t size) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        for (size_t i = 0; i < size; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = size; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

void *memset(void *dst, int value, size_t size) {
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < size; i++) d[i] = (uint8_t)value;
    return dst;
}

int memcmp(const void *a, const void *b, size_t size) {
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    for (size_t i = 0; i < size; i++) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

size_t strlen(const char *text) {
    size_t n = 0;
    while (text && text[n]) n++;
    return n;
}

char *strcpy(char *dst, const char *src) {
    char *out = dst;
    while ((*dst++ = *src++) != '\0') { }
    return out;
}

char *strncpy(char *dst, const char *src, size_t size) {
    size_t i = 0;
    for (; i < size && src[i]; i++) dst[i] = src[i];
    for (; i < size; i++) dst[i] = '\0';
    return dst;
}

char *strcat(char *dst, const char *src) {
    strcpy(dst + strlen(dst), src);
    return dst;
}

char *strncat(char *dst, const char *src, size_t size) {
    char *out = dst + strlen(dst);
    size_t i = 0;
    for (; i < size && src[i]; i++) out[i] = src[i];
    out[i] = '\0';
    return dst;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t size) {
    for (size_t i = 0; i < size; i++) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

char *strchr(const char *text, int ch) {
    for (;;) {
        if (*text == (char)ch) return (char *)text;
        if (!*text) return NULL;
        text++;
    }
}

char *strrchr(const char *text, int ch) {
    const char *last = NULL;
    for (;;) {
        if (*text == (char)ch) last = text;
        if (!*text) return (char *)last;
        text++;
    }
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *a = haystack;
        const char *b = needle;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (char *)haystack;
    }
    return NULL;
}

char *strdup(const char *text) {
    size_t size = strlen(text) + 1;
    char *copy = (char *)malloc(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}

int strcasecmp(const char *a, const char *b) {
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t size) {
    for (size_t i = 0; i < size; i++) {
        int x = tolower((unsigned char)a[i]);
        int y = tolower((unsigned char)b[i]);
        if (x != y) return x - y;
        if (!a[i]) return 0;
    }
    return 0;
}

char *strerror(int error) {
    (void)error;
    return "system error";
}

/* ------------------------------------------------------------------ */
/* Character helpers                                                   */
/* ------------------------------------------------------------------ */
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isalpha(int c) { return islower(c) || isupper(c); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int tolower(int c) { return isupper(c) ? c + ('a' - 'A') : c; }
int toupper(int c) { return islower(c) ? c - ('a' - 'A') : c; }

/* ------------------------------------------------------------------ */
/* Conversion                                                          */
/* ------------------------------------------------------------------ */
int atoi(const char *text) { return (int)strtol(text, NULL, 10); }
long atol(const char *text) { return strtol(text, NULL, 10); }

long strtol(const char *text, char **end, int base) {
    while (isspace((unsigned char)*text)) text++;
    int sign = 1;
    if (*text == '-' || *text == '+') {
        if (*text == '-') sign = -1;
        text++;
    }
    if (base == 0) {
        if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            base = 16; text += 2;
        } else if (text[0] == '0') {
            base = 8;
        } else {
            base = 10;
        }
    } else if (base == 16 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text += 2;
    }
    long value = 0;
    bool any = false;
    for (;;) {
        int c = (unsigned char)*text;
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10;
        else break;
        if (digit >= base) break;
        value = value * base + digit;
        any = true;
        text++;
    }
    if (end) *end = (char *)(any ? text : text);
    return value * sign;
}

unsigned long strtoul(const char *text, char **end, int base) {
    return (unsigned long)strtol(text, end, base);
}

double atof(const char *text) {
    double value = 0;
    int sign = 1;
    while (isspace((unsigned char)*text)) text++;
    if (*text == '-') { sign = -1; text++; }
    while (*text >= '0' && *text <= '9') value = value * 10 + (*text++ - '0');
    if (*text == '.') {
        text++;
        double scale = 0.1;
        while (*text >= '0' && *text <= '9') {
            value += (*text++ - '0') * scale;
            scale *= 0.1;
        }
    }
    return value * sign;
}

int abs(int value) { return value < 0 ? -value : value; }
long labs(long value) { return value < 0 ? -value : value; }

static unsigned int random_state = 0x51A7C0DEu;
int rand(void) {
    random_state = random_state * 1103515245u + 12345u;
    return (int)((random_state >> 1) & 0x7FFFFFFFu);
}
void srand(unsigned int seed) { random_state = seed; }
char *getenv(const char *name) { (void)name; return NULL; }
int system(const char *command) { (void)command; return -1; }

void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *)) {
    uint8_t *items = (uint8_t *)base;
    for (size_t i = 1; i < count; i++) {
        for (size_t j = i; j > 0; j--) {
            if (compare(items + (j - 1) * size, items + j * size) <= 0) break;
            uint8_t swap[64];
            if (size > sizeof(swap)) return;
            memcpy(swap, items + (j - 1) * size, size);
            memcpy(items + (j - 1) * size, items + j * size, size);
            memcpy(items + j * size, swap, size);
        }
    }
}

/* ------------------------------------------------------------------ */
/* A small printf formatter used by Doom's logs and config paths.      */
/* ------------------------------------------------------------------ */
static void emit_char(char *out, size_t cap, size_t *used, char ch) {
    if (*used + 1 < cap) out[*used] = ch;
    (*used)++;
}

static void emit_number(char *out, size_t cap, size_t *used,
                        unsigned long long value, int base, bool upper,
                        int width, char pad) {
    char digits[32];
    int n = 0;
    const char *alphabet = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (value == 0) digits[n++] = '0';
    while (value) {
        digits[n++] = alphabet[value % (unsigned)base];
        value /= (unsigned)base;
    }
    while (n < width) digits[n++] = pad;
    while (n > 0) emit_char(out, cap, used, digits[--n]);
}

static void format_output(char *out, size_t cap, const char *format, va_list args) {
    size_t used = 0;
    for (const char *p = format; p && *p; p++) {
        if (*p != '%') {
            emit_char(out, cap, &used, *p);
            continue;
        }
        p++;
        if (!*p) break;
        char pad = ' ';
        int width = 0;
        bool left = false;
        if (*p == '-') { left = true; p++; }
        if (*p == '0') { pad = '0'; p++; }
        while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        int precision = -1;
        if (*p == '.') {
            p++;
            precision = 0;
            while (*p >= '0' && *p <= '9') precision = precision * 10 + (*p++ - '0');
        }
        bool is_long = false;
        while (*p == 'l' || *p == 'z' || *p == 't') { is_long = true; p++; }
        char spec = *p;
        if (left) width = -width;
        switch (spec) {
            case 's': {
                const char *text = va_arg(args, const char *);
                if (!text) text = "(null)";
                int len = (int)strlen(text);
                if (precision >= 0 && len > precision) len = precision;
                for (int i = 0; i < len; i++) emit_char(out, cap, &used, text[i]);
                break;
            }
            case 'c':
                emit_char(out, cap, &used, (char)va_arg(args, int));
                break;
            case 'd':
            case 'i': {
                long long value = is_long ? va_arg(args, long long) : va_arg(args, int);
                int digits = width;
                char digit_pad = pad;
                if (precision > digits) digits = precision;
                if (precision > 0) digit_pad = '0';
                if (value < 0) {
                    emit_char(out, cap, &used, '-');
                    emit_number(out, cap, &used, (unsigned long long)(-value), 10, false, digits > 0 ? digits - 1 : 0, digit_pad);
                } else {
                    emit_number(out, cap, &used, (unsigned long long)value, 10, false, digits, digit_pad);
                }
                break;
            }
            case 'u':
            case 'x':
            case 'X': {
                unsigned long long value = is_long ? va_arg(args, unsigned long long)
                                                   : va_arg(args, unsigned int);
                int digits = width;
                char digit_pad = pad;
                if (precision > digits) digits = precision;
                if (precision > 0) digit_pad = '0';
                emit_number(out, cap, &used, value, spec == 'u' ? 10 : 16,
                            spec == 'X', digits, digit_pad);
                break;
            }
            case 'p': {
                uintptr_t value = (uintptr_t)va_arg(args, void *);
                emit_char(out, cap, &used, '0');
                emit_char(out, cap, &used, 'x');
                emit_number(out, cap, &used, value, 16, false, (int)(sizeof(void *) * 2), '0');
                break;
            }
            case 'f': {
                double value = va_arg(args, double);
                bool negative = value < 0;
                if (negative) value = -value;
                if (negative) emit_char(out, cap, &used, '-');
                unsigned long long whole = (unsigned long long)value;
                double fraction = value - (double)whole;
                emit_number(out, cap, &used, whole, 10, false, 0, ' ');
                emit_char(out, cap, &used, '.');
                int decimals = precision < 0 ? 6 : precision;
                while (decimals-- > 0) {
                    fraction *= 10.0;
                    int digit = (int)fraction;
                    emit_char(out, cap, &used, (char)('0' + digit));
                    fraction -= digit;
                }
                break;
            }
            case '%':
                emit_char(out, cap, &used, '%');
                break;
            default:
                emit_char(out, cap, &used, '%');
                if (spec) emit_char(out, cap, &used, spec);
                break;
        }
    }
    if (cap) out[used < cap ? used : cap - 1] = '\0';
}

int vsnprintf(char *out, size_t size, const char *format, va_list args) {
    format_output(out, size, format, args);
    return (int)strlen(out);
}
int vsprintf(char *out, const char *format, va_list args) {
    return vsnprintf(out, (size_t)-1, format, args);
}
int snprintf(char *out, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vsnprintf(out, size, format, args);
    va_end(args);
    return result;
}
int sprintf(char *out, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vsprintf(out, format, args);
    va_end(args);
    return result;
}
int vprintf(const char *format, va_list args) { (void)format; (void)args; return 0; }
int vfprintf(FILE *file, const char *format, va_list args) {
    (void)file; (void)format; (void)args;
    return 0;
}
int printf(const char *format, ...) { (void)format; return 0; }
int fprintf(FILE *file, const char *format, ...) { (void)file; (void)format; return 0; }

/* ------------------------------------------------------------------ */
/* WAD-only stdio shim                                                 */
/* ------------------------------------------------------------------ */
static bool has_wad_suffix(const char *path) {
    if (!path) return false;
    size_t length = strlen(path);
    return length >= 9 && !strcasecmp(path + length - 9, "doom1.wad");
}

FILE *fopen(const char *path, const char *mode) {
    if (!path || !mode) return NULL;
    if (has_wad_suffix(path)) {
        wad_stream.pos = 0;
        wad_stream.length = doom_wad_size();
        wad_stream.writable = 0;
        return &wad_stream;
    }
    if (mode[0] == 'w' || mode[0] == 'a') {
        sink_stream.pos = 0;
        sink_stream.length = 0;
        sink_stream.writable = 1;
        return &sink_stream;
    }
    return NULL;
}

int fclose(FILE *file) { (void)file; return 0; }

size_t fread(void *ptr, size_t size, size_t count, FILE *file) {
    if (!ptr || !file || size == 0 || count == 0) return 0;
    size_t requested = size * count;
    size_t available = file->length > file->pos ? file->length - file->pos : 0;
    size_t bytes = requested < available ? requested : available;
    if (file == &wad_stream) {
        memcpy(ptr, doom_wad_data() + file->pos, bytes);
    } else {
        memset(ptr, 0, bytes);
    }
    file->pos += (unsigned int)bytes;
    return bytes / size;
}

size_t fwrite(const void *ptr, size_t size, size_t count, FILE *file) {
    (void)ptr;
    if (!file) return 0;
    file->pos += (unsigned int)(size * count);
    if (file->pos > file->length) file->length = file->pos;
    return count;
}

int fseek(FILE *file, long offset, int origin) {
    if (!file) return -1;
    long base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? (long)file->pos
                                                             : (long)file->length;
    long next = base + offset;
    if (next < 0) next = 0;
    if (next > (long)file->length) next = (long)file->length;
    file->pos = (unsigned int)next;
    return 0;
}

long ftell(FILE *file) { return file ? (long)file->pos : -1; }
int feof(FILE *file) { return !file || file->pos >= file->length; }
int fflush(FILE *file) { (void)file; return 0; }
static bool scan_integer(const char **cursor, int base, long *out) {
    const char *p = *cursor;
    while (isspace((unsigned char)*p)) p++;
    bool negative = false;
    if (*p == '-' || *p == '+') { negative = *p == '-'; p++; }
    if (base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
        else if (p[0] == '0') { base = 8; }
        else base = 10;
    } else if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }
    long value = 0;
    bool any = false;
    for (;;) {
        int digit;
        if (*p >= '0' && *p <= '9') digit = *p - '0';
        else if (*p >= 'a' && *p <= 'f') digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') digit = *p - 'A' + 10;
        else break;
        if (digit >= base) break;
        value = value * base + digit;
        any = true;
        p++;
    }
    if (!any) return false;
    *out = negative ? -value : value;
    *cursor = p;
    return true;
}

int sscanf(const char *text, const char *format, ...) {
    va_list args;
    va_start(args, format);
    const char *p = text ? text : "";
    int assigned = 0;
    for (const char *f = format; f && *f; f++) {
        if (isspace((unsigned char)*f)) {
            while (isspace((unsigned char)*p)) p++;
            continue;
        }
        if (*f != '%') {
            if (*p != *f) break;
            p++;
            continue;
        }
        f++;
        if (*f == '%') { if (*p == '%') p++; else break; continue; }
        int width = 0;
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        bool long_value = false;
        while (*f == 'l' || *f == 'h' || *f == 'z' || *f == 't') {
            if (*f == 'l') long_value = true;
            f++;
        }
        char spec = *f;
        if (spec == 's') {
            while (isspace((unsigned char)*p)) p++;
            char *out = va_arg(args, char *);
            int n = 0;
            while (*p && !isspace((unsigned char)*p) && (width == 0 || n < width)) out[n++] = *p++;
            if (n == 0) break;
            out[n] = '\0';
            assigned++;
        } else if (spec == '[') {
            if (*f == '[') f++;
            bool negate = *f == '^';
            if (negate) f++;
            char terminator = *f;
            char *out = va_arg(args, char *);
            int n = 0;
            while (*p && (width == 0 || n < width)) {
                bool matches = *p == terminator;
                if (negate) matches = !matches;
                if (!matches) break;
                out[n++] = *p++;
            }
            out[n] = '\0';
            assigned++;
        } else if (spec == 'c') {
            char *out = va_arg(args, char *);
            if (!*p) break;
            *out = *p++;
            assigned++;
        } else if (spec == 'd' || spec == 'i' || spec == 'u' || spec == 'x' || spec == 'X' || spec == 'o') {
            long value = 0;
            int base = spec == 'x' || spec == 'X' ? 16 : spec == 'o' ? 8 : spec == 'i' ? 0 : 10;
            if (!scan_integer(&p, base, &value)) break;
            if (long_value) *va_arg(args, long *) = value;
            else *va_arg(args, int *) = (int)value;
            assigned++;
        } else if (spec == 'f') {
            char *end = (char *)p;
            double value = atof(p);
            if (end == p) break;
            *va_arg(args, double *) = value;
            p = end;
            assigned++;
        } else {
            break;
        }
    }
    va_end(args);
    return assigned;
}

int fscanf(FILE *file, const char *format, ...) {
    char line[256];
    if (!fgets(line, sizeof(line), file)) return EOF;
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int result = 0;
    /* The config reader only needs the common two-field form. */
    if (format && strstr(format, "%79s")) {
        char *a = va_arg(copy, char *);
        char *b = va_arg(copy, char *);
        int n = 0;
        while (line[n] && !isspace((unsigned char)line[n]) && n < 79) { a[n] = line[n]; n++; }
        a[n] = '\0';
        while (isspace((unsigned char)line[n])) n++;
        int m = 0;
        while (line[n] && line[n] != '\n' && m < 99) { b[m++] = line[n++]; }
        b[m] = '\0';
        result = 2;
    } else {
        result = 0;
    }
    va_end(copy);
    va_end(args);
    return result;
}

int fgetc(FILE *file) {
    unsigned char ch = 0;
    return fread(&ch, 1, 1, file) == 1 ? ch : EOF;
}
int fputc(int ch, FILE *file) { (void)file; return ch; }
char *fgets(char *text, int size, FILE *file) {
    int i = 0;
    if (!text || size <= 0) return NULL;
    while (i + 1 < size) {
        int ch = fgetc(file);
        if (ch == EOF) break;
        text[i++] = (char)ch;
        if (ch == '\n') break;
    }
    text[i] = '\0';
    return i ? text : NULL;
}
int puts(const char *text) { (void)text; return 0; }
int getchar(void) { return EOF; }
int putchar(int ch) { return ch; }
int remove(const char *path) { (void)path; return 0; }
int rename(const char *from, const char *to) { (void)from; (void)to; return 0; }
void perror(const char *text) { (void)text; }

/* ------------------------------------------------------------------ */
/* Process and filesystem stubs                                        */
/* ------------------------------------------------------------------ */
void exit(int status) {
    (void)status;
    DG_Error("Doom requested exit");
}

void abort(void) {
    DG_Error("Doom aborted");
}

int atexit(void (*fn)(void)) { (void)fn; return 0; }
void solis_assert_fail(const char *expr, const char *file, int line) {
    (void)expr; (void)file; (void)line;
    DG_Error("Doom assertion failed");
}

int stat(const char *path, struct stat *out) { (void)path; if (out) memset(out, 0, sizeof(*out)); return -1; }
int mkdir(const char *path, mode_t mode) { (void)path; (void)mode; return 0; }
int gettimeofday(struct timeval *out, void *timezone) {
    (void)timezone;
    uint32_t ticks = timer_get_ticks();
    if (out) { out->tv_sec = ticks / 100; out->tv_usec = (ticks % 100) * 10000; }
    return 0;
}
time_t time(time_t *out) {
    time_t now = (time_t)(timer_get_ticks() / 100);
    if (out) *out = now;
    return now;
}
struct tm *localtime(const time_t *value) { (void)value; return NULL; }
int isatty(int fd) { (void)fd; return 0; }
int fileno(void *stream) { (void)stream; return -1; }
int usleep(unsigned int microseconds) { (void)microseconds; return 0; }
int close(int fd) { (void)fd; return 0; }
long read(int fd, void *buffer, size_t size) { (void)fd; (void)buffer; (void)size; return 0; }
long write(int fd, const void *buffer, size_t size) { (void)fd; (void)buffer; (void)size; return 0; }
int unlink(const char *path) { (void)path; return 0; }
char *getcwd(char *buffer, size_t size) { if (buffer && size) buffer[0] = '\0'; return buffer; }
int chdir(const char *path) { (void)path; return 0; }

/* ------------------------------------------------------------------ */
/* Floating point                                                      */
/* ------------------------------------------------------------------ */
double fabs(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    bits &= 0x7FFFFFFFFFFFFFFFULL;
    memcpy(&value, &bits, sizeof(value));
    return value;
}
float fabsf(float value) { return (float)fabs(value); }

double floor(double value) {
    long long integer = (long long)value;
    if ((double)integer > value) integer--;
    return (double)integer;
}
double ceil(double value) {
    long long integer = (long long)value;
    if ((double)integer < value) integer++;
    return (double)integer;
}
float floorf(float value) { return (float)floor(value); }
float ceilf(float value) { return (float)ceil(value); }

double sqrt(double value) {
    if (value <= 0.0) return 0.0;
    double result;
    __asm__ volatile("sqrtsd %1, %0" : "=x"(result) : "x"(value));
    return result;
}
float sqrtf(float value) { return (float)sqrt((double)value); }

static double reduce_angle(double value) {
    const double two_pi = 6.28318530717958647692;
    while (value > 3.14159265358979323846) value -= two_pi;
    while (value < -3.14159265358979323846) value += two_pi;
    return value;
}

double sin(double value) {
    value = reduce_angle(value);
    double term = value;
    double result = value;
    for (int i = 1; i <= 9; i += 2) {
        term *= -value * value / ((double)(i + 1) * (double)(i + 2));
        result += term;
    }
    return result;
}

double cos(double value) {
    return sin(value + 1.57079632679489661923);
}

double tan(double value) {
    double c = cos(value);
    return c == 0.0 ? 0.0 : sin(value) / c;
}

double atan(double value) {
    int sign = value < 0 ? -1 : 1;
    value = fabs(value);
    int invert = value > 1.0;
    if (invert) value = 1.0 / value;
    double term = value;
    double result = value;
    for (int i = 1; i < 12; i++) {
        term *= -value * value;
        result += term / (double)(2 * i + 1);
    }
    if (invert) result = 1.57079632679489661923 - result;
    return result * sign;
}

double atan2(double y, double x) {
    if (x > 0.0) return atan(y / x);
    if (x < 0.0) return y >= 0.0 ? atan(y / x) + 3.14159265358979323846
                                 : atan(y / x) - 3.14159265358979323846;
    if (y > 0.0) return 1.57079632679489661923;
    if (y < 0.0) return -1.57079632679489661923;
    return 0.0;
}

double asin(double value) {
    if (value >= 1.0) return 1.57079632679489661923;
    if (value <= -1.0) return -1.57079632679489661923;
    return atan(value / sqrt(1.0 - value * value));
}

double acos(double value) {
    return 1.57079632679489661923 - asin(value);
}

double fmod(double value, double divisor) {
    if (divisor == 0.0) return 0.0;
    double quotient = (double)(long long)(value / divisor);
    return value - quotient * divisor;
}

double pow(double base, double exponent) {
    if (exponent == 0.0) return 1.0;
    if (base == 0.0) return 0.0;
    int integer = (int)exponent;
    if ((double)integer == exponent) {
        double result = 1.0;
        int negative = integer < 0;
        if (negative) integer = -integer;
        for (int i = 0; i < integer; i++) result *= base;
        return negative ? 1.0 / result : result;
    }
    /* Doom itself only needs the integer cases; keep a safe, cheap
     * approximation for any platform diagnostic that asks for a fraction. */
    return base;
}

float sinf(float value) { return (float)sin(value); }
float cosf(float value) { return (float)cos(value); }
float tanf(float value) { return (float)tan(value); }
