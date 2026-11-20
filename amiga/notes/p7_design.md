# P7: the match picture on the A1200 (design, from the P6.3 measurements)

## What the match asks for (amiga/notes/display_stats.md)

* Plane B is the rink: 64 x 32 cells (512 x 256 pixels), scrolled as one piece, horizontally and
  vertically, in about a quarter of the match frames. Up to 853 cells shown, all four palette lines,
  priority cells (boards, nets) in half the frames.
* Plane A is the overlays (clock, score, messages): never scrolled in a match (its scroll is 0 in
  every match frame of the seven runs), always high priority, mean about 100 cells.
* Sprites: up to 62 on screen, mean 21.8; 5,129 pixels of area on average, 13,504 at worst; all four
  palette lines; high priority in a fifth of the frames; sometimes more on a line than the VDP shows.
* Width 256, no per-line or per-column scroll.

Composing the whole screen again on every scrolled frame is about 7,168 line bytes; even at 20
instructions a byte that is 40 to 50 ms of 68020 time. The rink must scroll in hardware.

## The design

* **The rink bitmap**: plane B drawn once into its own planar bitmap (interleaved, six planes a line:
  four colour planes and the palette line as two more), 512 pixels wide plus 256 pixels repeated on
  the right, so any 256-pixel window is one piece; 256 lines. Only cells that change are drawn into it.
  A one-plane mask of its high-priority opaque pixels beside it.
* **Two display buffers**, copies of the rink bitmap, shown in turn. The copper list points the
  bitplanes at the scroll position (BPLxPT, BPLCON1 for the fine scroll) and, where the plane's 256
  lines wrap on screen, starts them again at the top (a copper split).
* **Objects, every frame, into the buffer not shown** (painter's order, each with its own opaque mask,
  so the VDP's priority rule comes out exactly): restore last time's object areas from the rink bitmap;
  sprites of low priority (in reverse link order, so the first in link order ends on top); the rink's
  high-priority pixels again over them (from the rink bitmap through its mask); plane A (pre-drawn in
  its own screen-sized bitmap with a mask; low priority A would go before the sprites); sprites of high
  priority. All of it is blitter work, cookie-cut (minterm $CA), which runs while the CPU does the next
  pass's logic.
* **The VDP's sprite limits** (16 a line in this width, 256 pixels a line, the mask at X 0): the lines
  where a limit cuts in are composed by the CPU instead (rare frames).
* **The check**: the list of blits is produced by portable code; on the host a C version of the
  blits runs it and the visible window, turned back into colours, is compared with the reference
  picture on every frame of the golden runs, as P6.4 is. On the Amiga the blitter runs the same list
  and the planes' hash is compared with the host's (A-DISPLAY-PLANES).

Measured cost to confirm on the taken machine: about 3 to 4 ms of blitter time a frame on average for
the sprites, about as much for plane A and the restores.

## Screens that stay with P6's composer

Menus (320 pixels, no sprites, no scroll) and the title (per-line scroll of plane B) keep the composed
bitmap: nothing there moves every frame except the title's credits band, which needs the copper (each
line's own plane pointers and fine scroll into a plane-B bitmap) where plane A is empty on the line.
