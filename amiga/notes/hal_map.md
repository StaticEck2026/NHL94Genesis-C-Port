# P0.2: the game's hardware calls (`src/hal.h`), by module

Counted from the source on commit f1ec3d1 (`grep -cw`, `src/z80.c` left out: it is the Z80 relay, a host
file the Amiga build does not link). Each call is one 68000 access in the original. The Amiga backend that
answers each one is in the last column (PROMPT.md section 7).

| Call | Modules (calls) | What the game does with it | Amiga answer |
|---|---|---|---|
| `vdp_ctrl16` | middle 35, checksum 5, hockey 5, stats 4, title 3, cards 2, scoreboard 2, setup 2, sega 1 | VDP registers (mode, plane bases, scroll mode, auto-increment) and half commands | Display backend: mode and layout state, then copper list |
| `vdp_ctrl` | middle 7, stats 5, sega 4, period 1, sram 1, title 1, video 1 | VRAM, CRAM and VSRAM write commands, and DMA starts | Display backend: the target of the next data writes |
| `vdp_data` | print 7, middle 6, stats 5, setup 2, video 2, checksum, menu, period, sega, sram, title 1 each | Name-table cells, palette words, scroll words, VRAM fills | Tile cache, playfield cells, palette, scroll |
| `vdp_data32` | middle 3, stats 3, cards 2, sega 2 | As `vdp_data`, two words | As `vdp_data` |
| `vdp_read` | middle 1 | A VRAM read back | Display backend keeps the game-visible copy it needs (P6 decides) |
| `vdp_status` | sega 2, hockey 1, middle 1 | DMA busy, vblank and PAL bits | Display backend: DMA is synchronous, so never busy |
| `vdp_hv16` | logic 10, optsetup 4, collide 3, hockey 2, scoreboard 2, title 2 | **The game's random source**: AI choices (`& 3`, `& 0x7F`) and the seed at the title (`g.seed`) | **D-9**: the host build's deterministic HV function, never the Amiga's beam |
| `vdp_hv8` | collide 2 | V counter, random as above | As `vdp_hv16` |
| `hal_wait_vblank` | 16 modules (setup 4, hockey 3, ...) | The game loop's wait for the next frame | The level 3 vblank (or CIA pacing, D-2) |
| `psg_write` | video 8, sega 1 | SN76489 tone and noise writes (sound effects) | Sound backend: PSG events to Paula voices |
| `z80_busreq` / `z80_busack` | sound 10/2, middle 4/2, sega 2/1 | Taking the Z80's bus to write the command buffer | Sound backend: a no-op with the ack the game expects |
| `z80_ram_write8` / `z80_ram_read8` | sound 17/1, sega 1 | The 33-byte command buffer, PCM pointers, the bytes patched into the relay | **The sound backend's input** (the command stream, PROMPT.md 3.4) |
| `z80_reset` | sega 4, sound 4, fourway 2 | Holding the Z80 in reset, or running it | Sound backend: stop or start |
| `pad_read` / `pad_write` | fourway 1/6, sound 2/3, sega 1 | Pads through TH select, and the 4 Way Play adaptor's select | Input: Amiga ports, CD32 pad, keyboard, four-player adapter |
| `sram_read8` / `sram_write16` | sram 3/4 | Battery save RAM | `CORE` image, written to `NHL94Data:NHL94.sram` at safe points |
| `cpu_get_sr` / `cpu_set_sr` | set_sr in 11 modules; get_sr logic, middle, sound | Masking the vblank interrupt around critical code | The level 3 interrupt's enable (`INTENA`), the same order |
| `io_read32` / `io_read16` | sega 1/1 | The I/O chip's version register (region, PAL) | Region per D-2 |
| `tmss_write` | sega 1 | Unlocking the VDP | No-op |

Findings that change the plan:

* **The HV counter is the game's random number source.** The host build answers it from the beam position
  its own clock gives (`ext/pulseman/src/mdrt/mdrt.c`, `beam_hv`: the frame's master clock from counted
  port accesses, nudged by 97 at each read). For A-LOGIC the Amiga must return the same values. Reading
  the Amiga's own beam would match neither the cartridge nor the host build. Decision D-9.
* `hal_wait_vblank` is the only frame wait, so pacing (D-2) has one place to live.
* Sound reaches the hardware only through Z80 RAM writes and the PSG, so the command stream is a clean
  input for the Paula backend.
