/*
 * Minimal HTTPS client transport.
 *
 * BearSSL is used as a library: TLS 1.2, X.509 minimal validation, and the
 * compiled-in public-root bundle. The engine talks to the kernel's
 * IPv4/TCP implementation through the small net_tcp_* API in net.c.
 *
 * This code runs on the kernel stack. BearSSL is built without OS entropy
 * or time support; entropy comes from RDRAND (with a TSC/RTC fallback), and
 * the certificate validation time comes from the CMOS RTC.
 */

#include <solis/tls.h>
#include <solis/net.h>
#include <solis/rtc.h>
#include <solis/timer.h>
#include <solis/dbg.h>

#include <bearssl.h>

extern const br_x509_trust_anchor tls_trust_anchors[];
extern const size_t tls_trust_anchors_num;

#define TLS_TIMEOUT_TICKS  500
#define TLS_REQUEST_MAX    768

static br_ssl_client_context tls_cc;
static br_x509_minimal_context tls_xc;
static unsigned char tls_iobuf[BR_SSL_BUFSIZE_BIDI];

static uint64_t tls_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static int tls_rdrand_supported(void) {
    uint32_t eax = 1, ebx = 0, ecx = 0, edx = 0;
    __asm__ __volatile__("cpuid"
                         : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (ecx & (1u << 30)) != 0;
}

static int tls_rdrand64(uint64_t *out) {
    if (!tls_rdrand_supported()) return 0;
    for (int i = 0; i < 10; i++) {
        uint64_t value;
        unsigned char ok;
        __asm__ __volatile__("rdrand %0; setc %1"
                             : "=r"(value), "=qm"(ok));
        if (ok) {
            *out = value;
            return 1;
        }
    }
    return 0;
}

static uint64_t tls_fallback_state;

static uint64_t tls_fallback_random(void) {
    uint64_t x = tls_fallback_state;
    if (x == 0) {
        x = tls_rdtsc() ^ ((uint64_t)timer_get_ticks() << 32);
        x ^= 0x9E3779B97F4A7C15ULL;
    }
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    tls_fallback_state = x;
    return x;
}

static void tls_seed_entropy(unsigned char seed[32]) {
    for (int i = 0; i < 4; i++) {
        uint64_t value;
        if (!tls_rdrand64(&value)) value = tls_fallback_random();
        for (int j = 0; j < 8; j++)
            seed[i * 8 + j] = (unsigned char)(value >> (j * 8));
    }
}

/* Days since 1970-01-01 for a Gregorian date (Howard Hinnant's algorithm). */
static int tls_days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

static void tls_set_validation_time(void) {
    struct rtc_time now;
    rtc_get_time(&now);
    int unix_days = tls_days_from_civil(now.year, now.month, now.day);
    /* BearSSL counts days from January 1st, 0 AD, not from 1970. */
    uint32_t days = (uint32_t)(unix_days + 719528);
    uint32_t seconds = (uint32_t)now.hour * 3600u +
                       (uint32_t)now.minute * 60u +
                       (uint32_t)now.second;
    br_x509_minimal_set_time(&tls_xc, days, seconds);
}

static int tls_low_read(void *ctx, unsigned char *data, size_t len) {
    struct net_tcp_conn *conn = (struct net_tcp_conn *)ctx;
    int n = net_tcp_read(conn, data, (int)len, TLS_TIMEOUT_TICKS);
    return n > 0 ? n : -1;
}

static int tls_low_write(void *ctx, const unsigned char *data, size_t len) {
    struct net_tcp_conn *conn = (struct net_tcp_conn *)ctx;
    size_t done = 0;
    while (done < len) {
        size_t chunk = len - done;
        if (chunk > 1024) chunk = 1024;
        if (net_tcp_write(conn, data + done, (int)chunk) < 0)
            return -1;
        done += chunk;
    }
    return (int)len;
}

static int tls_fail(const char *where) {
    int err = br_ssl_engine_last_error(&tls_cc.eng);
    dbg_print("[TLS] ");
    dbg_print(where);
    dbg_print(" failed, error=");
    dbg_print_dec(err);
    dbg_print("\n");
    return -2;
}

static int tls_append(char *dst, int max, int *pos, const char *src) {
    while (*src) {
        if (*pos >= max - 1) return -1;
        dst[(*pos)++] = *src++;
    }
    dst[*pos] = '\0';
    return 0;
}

int tls_https_get(const uint8_t *ip, uint16_t port,
                  const char *host, const char *path,
                  uint8_t *response, uint32_t max_size) {
    if (!ip || !host || !path || !response || max_size == 0) return -1;
    if (!net_available()) return -1;

    struct net_tcp_conn conn;
    if (net_tcp_open(&conn, ip, port, TLS_TIMEOUT_TICKS) < 0)
        return -1;

    br_ssl_client_init_full(&tls_cc, &tls_xc,
                            tls_trust_anchors, tls_trust_anchors_num);
    br_ssl_engine_set_buffer(&tls_cc.eng, tls_iobuf, sizeof tls_iobuf, 0);

    unsigned char seed[32];
    tls_seed_entropy(seed);
    br_ssl_engine_inject_entropy(&tls_cc.eng, seed, sizeof seed);
    tls_set_validation_time();

    if (!br_ssl_client_reset(&tls_cc, host, 0))
        return tls_fail("reset");

    br_sslio_context ioc;
    br_sslio_init(&ioc, &tls_cc.eng,
                  tls_low_read, &conn, tls_low_write, &conn);

    char request[TLS_REQUEST_MAX];
    int req_len = 0;
    if (tls_append(request, sizeof request, &req_len, "GET ") < 0 ||
        tls_append(request, sizeof request, &req_len, path) < 0 ||
        tls_append(request, sizeof request, &req_len, " HTTP/1.0\r\nHost: ") < 0 ||
        tls_append(request, sizeof request, &req_len, host) < 0 ||
        tls_append(request, sizeof request, &req_len,
                   "\r\nUser-Agent: SolisBrowser/1.0\r\n"
                   "Accept: text/html,application/xhtml+xml,application/json;q=0.9,*/*;q=0.8\r\n"
                   "Accept-Language: en-US,en;q=0.9\r\n"
                   "Connection: close\r\n\r\n") < 0)
        return -1;

    if (br_sslio_write_all(&ioc, request, (size_t)req_len) < 0 ||
        br_sslio_flush(&ioc) < 0)
        return tls_fail("request");

    int total = 0;
    while ((uint32_t)total < max_size) {
        int n = br_sslio_read(&ioc, response + total,
                              (size_t)(max_size - (uint32_t)total));
        if (n <= 0) break;
        total += n;
    }

    if (total == 0 && br_ssl_engine_last_error(&tls_cc.eng) != BR_ERR_OK)
        return tls_fail("read");
    return total;
}
