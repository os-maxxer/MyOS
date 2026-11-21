#ifndef SOLIS_AUDIO_H
#define SOLIS_AUDIO_H

#include <solis/types.h>
#include <stdbool.h>

/* AC'97 PCM output.  The kernel driver owns the DMA pages; applications can
 * ask for a short generated tone through the syscall table. */
bool audio_init(void);
int audio_available(void);
void audio_stop(void);
int audio_play_tone(uint32_t hz, uint32_t ms);

#endif
