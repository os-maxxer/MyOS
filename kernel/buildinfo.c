#include <solis/buildinfo.h>

/* Compiled with -DSOLIS_BUILD_ID="<timestamp>-<sha>" by the Makefile. Keeping
 * it in its own translation unit means the stamp can change without forcing
 * a rebuild of the whole kernel. */
const char solis_build_id[] = SOLIS_BUILD_ID;
