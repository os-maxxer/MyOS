#ifndef SOLIS_BOOT_H
#define SOLIS_BOOT_H

/* Framebuffer boot report. These calls are no-ops if the framebuffer is not
 * ready, so the kernel can call them unconditionally after graphics_init. */
void boot_report_begin(void);
void boot_report_step(const char *message);
void boot_report_finish(void);

#endif
