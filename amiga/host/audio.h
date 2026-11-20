/* amiga/host/audio.c: sound.c's voices (paula[]) onto Paula's registers, once a pass, with the
 * machine taken. */
#pragma once
void audio_start(void);
void audio_apply(void);
void audio_stop(void);
