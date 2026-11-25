#ifndef SOLIS_BUILDINFO_H
#define SOLIS_BUILDINFO_H

/* The build identity is compiled into exactly one object (kernel/buildinfo.c)
 * from -DSOLIS_BUILD_ID, so a running system can say which image it is.
 *
 * The value is "<utc timestamp>-<git sha>", for example
 * "20261121-184501-3f9a1c2". It is printed on the serial boot line, shown in
 * Settings > About, in the CK3 help screen, and it is the exact string
 * tools/verify_images.py looks for inside the ISO, disk image and VDI. That
 * is what makes a stale artifact detectable instead of invisible. */
#ifndef SOLIS_BUILD_ID
#define SOLIS_BUILD_ID "unstamped"
#endif

extern const char solis_build_id[];

/* Build identity of the running system, as seen by an SPX app. */
const char *sys_get_build_id(void);

#endif
