/* amiga/host/input.c: the Amiga's own controllers as Genesis pads (PROMPT.md 7.4, D-6), read once a
 * pass while the machine is taken: joysticks in both 9-pin ports (fire 2 through POTGO), the CD32 pad
 * (its shift register), the keyboard (CIA-A's serial line, with the handshake, by the level 2
 * interrupt), and two more joysticks on a parallel-port four-player adapter. */
#pragma once
#include <stdint.h>

/* the Genesis pad's bits (as MDRT_PAD_*) */
enum { PAD_UP = 0x01, PAD_DOWN = 0x02, PAD_LEFT = 0x04, PAD_RIGHT = 0x08, PAD_B = 0x10, PAD_C = 0x20, PAD_A = 0x40, PAD_START = 0x80 };

void input_start(void);                   /* with the machine taken: the keyboard's interrupt on, the ports set up */
void input_stop(void);                    /* before the machine is given back */
uint16_t input_pad(int pad);              /* pad 0 to 3: what the controllers assigned to it hold now */
int input_quit(void);                     /* Esc was pressed */
extern uint8_t input_cd32[2];             /* a CD32 pad was seen in port 1 / port 2 */
/* the host menu's choices: swap (player 1 on port 1, player 2 on port 2), keyboard (the pad it is
 * part of, 0 or 1), fourway (the adapter read; off: pads 2 and 3 hold nothing), filter (the audio
 * filter and the power LED on); set before input_start */
struct input_cfg { int swap, keyboard, fourway, filter; };
extern struct input_cfg input_cfg;
