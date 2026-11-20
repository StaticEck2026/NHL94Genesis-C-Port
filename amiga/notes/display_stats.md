# P6.2/P6.3: what the picture is made of (measured)

From the seven vanilla golden runs played through `amiga/host/hal_core.c` on the host
(`HALCHECK_STATS=FILE build/amiga-tools/halcheck ...`, summarised by `amiga/tools/dispstats.py`).
Only frames with the palette lit are counted: 68,928 of 69,293.

## The picture reference

The picture the host build shows for a frame is the VDP state at the game's wait, drawn at once
(`halcheck`: 69,293 frames, 0 differing from the golden runs' picture hashes). No beam timing is needed
to say what must be on screen for a pass. `amiga/host/disp_core.c`'s lists (planar tile cache, cell maps,
latched sprite entries, palette, scroll), drawn on the host, give the same picture on all 69,293 frames.

## By kind of screen

| Screen | Frames | Planes | Sprites | Scroll | The busiest pass sends |
|---|---|---|---|---|---|
| Menus, 320 px, no sprites | 32,870 | A up to 874 cells not empty, B up to 1,120 (the whole screen); all four palette lines; both planes use priority | none | never changes while lit, except 24 frames of one in-match screen | 7,916 cell bytes (3,958 cells), 1,954 tile bytes |
| Title and credits, 256 px, no sprites | 6,353 | A up to 237 cells, B up to 835; palette lines 1 to 3; window rows on | none | per-line horizontal scroll (mode 3), changing in about one frame in five | 488 cell bytes |
| Match, 256 px | 29,390 | B (the rink) up to 853 cells, all four palette lines, priority cells in half the frames (up to 172); A (overlays) up to 523 cells, palette lines 0 to 2, priority always; the window's 28 rows on in the stats screens | up to 62 on screen (mean 21.8); area up to 13,504 px (mean 5,129); up to 20 on a line (over the 16 the VDP shows in this width: the limit is part of the picture); all four palette lines; high priority in about a fifth of the frames | the rink scrolls in about a quarter of the frames (whole-plane scroll) | 4,132 cell bytes, 7,072 tile bytes, 506 sprite table bytes; means 17 cell bytes and 165 tile bytes a pass |
| A logo screen, 320 px, sprites | 315 | A up to 465 cells, B up to 1,189 | 10 large ones, 9,216 px, 2 on a line | none | nothing |

VRAM copies (104 to 404 a run, in matches): rows of plane A's cells moved up a row (58 bytes each, a
scrolling list), and whole tiles copied to another tile (32 bytes each, animation frames). Both are whole
cells or whole tiles at even addresses.

## What this says about the design (PROMPT.md 7.1, issue I-1)

* **Sprites use all four palette lines, and so does the rink.** A 16-colour sprite playfield over a
  16-colour rink (dual playfield) cannot hold either. The picture needs the one 6-bitplane playfield of
  the starting design, with the objects drawn into it.
* **The object load in a match**: mean 5,129 px of sprite area a frame, worst 13,504 px. In six bitplanes
  that is 320 words a plane on average and 844 at worst. A cookie-cut blit moves a word in four chip-bus
  cycles (1.12 us) and a restore in two: about 3.2 ms of blitter time a frame on average and 8.5 ms at
  worst for restore plus draw, before the set-up of each blit (62 objects at worst). The mean fits beside
  the logic's 1.5 to 3.9 ms; the worst frames do not leave much. Measuring this on the taken machine is
  P2's and P7's first job; hardware sprites for the puck and a few small objects, and merging the sprites
  of one player into one blit, are the levers.
* **Priority is real**: rink cells in front of the players (boards, nets) in half the match frames, and
  the overlays of plane A in front of everything. Both need a mask step after the objects.
* **Menus never scroll and have no sprites**: P6 can draw them as dirty cells composed from planes A and
  B into the one bitmap. The busiest pass names 3,958 cells; it is a screen build (the screen is black).
* **The title needs per-line horizontal scroll** of plane B under a fixed plane A: a copper list with a
  scroll value and plane pointers on each line cannot move one plane of a composed bitmap against the
  other, so the title's scrolled band is composed in software each frame it moves (about one frame in
  five), or shown as its own playfield band by a copper split. To decide by measurement in P6.
