#ifndef SOLIS_TLS_H
#define SOLIS_TLS_H

#include <solis/types.h>

/*
 * Fetch an HTTPS resource and return the raw HTTP response (headers and
 * body) in response. Returns the number of bytes written to response, or
 * -1 for a TCP/connect failure and -2 for a TLS handshake or certificate
 * failure. The caller owns HTTP parsing and redirect handling.
 */
int tls_https_get(const uint8_t *ip, uint16_t port,
                  const char *host, const char *path,
                  uint8_t *response, uint32_t max_size);

/*
 * Streaming variant used by the package manager, so a caller can draw a
 * progress bar while a package arrives. One session at a time.
 *
 * open() connects, performs the handshake and sends the request; it returns
 * 0 on success. read() returns the bytes of the raw HTTP response that are
 * available now, 0 at end of stream and -1 on failure. close() drops the
 * session state without sending a close_notify (the server closes anyway).
 */
int tls_stream_open(const uint8_t *ip, uint16_t port,
                    const char *host, const char *path);
int tls_stream_read(uint8_t *buf, uint32_t max);
void tls_stream_close(void);

#endif
