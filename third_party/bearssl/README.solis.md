# BearSSL (vendored)

BearSSL 0.6 is vendored under `inc/` and `src/` for the kernel's minimal
HTTPS client. Upstream: <https://www.bearssl.org/>.

BearSSL is MIT-licensed; see `LICENSE.txt`.

Solis builds only the library (not BearSSL's tools or samples) with the
kernel's freestanding, non-SSE flags and these settings:

- `BR_USE_URANDOM=0`
- `BR_USE_WIN32_RAND=0`
- `BR_USE_UNIX_TIME=0`
- `BR_USE_WIN32_TIME=0`
- `BR_RDRAND=0`

The kernel injects entropy from RDRAND (with a TSC fallback) and sets the
X.509 validation time from the CMOS RTC. No host OS services are used.

Only TLS 1.2 with the library's default cipher suites is used. The trust
anchor in `kernel/tls_trust.c` is ISRG Root X2.
