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

#endif
