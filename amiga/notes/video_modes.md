# P0.4: the display modes the game sets

From the source on commit f1ec3d1: `middle.c` `setVram_0` sets the VDP's layout for every screen, from
`g.disflags` (`df32c`: 32 columns) and each map's `colshift`.

| Screen | Columns (width) | Set in | Planes (VRAM) | Notes |
|---|---|---|---|---|
| The rink (match) | **32 (256 px)** | `setup.c` `setupice` | B at $C000 (64 wide), A at $E000 (64 wide), window at $F000 (32 wide), sprites at $DC00, H-scroll at $FC00 | The wide view rule (`view_rink_mode`, not vanilla) uses 40 |
| Title | 32 (256 px) | `title.c` | `screen_layout` | Seeds `g.seed` from the HV counter here (D-9) |
| Stats screen in the match | 32 | `stats.c` | window rows on (`WINY` $17) | The window plane is used here |
| Front-end menus: setup, options, records, scout, cards, period stats, UI | **40 (320 px)** | `setup.c`, `optsetup.c`, `records.c`, `scout.c`, `cards.c`, `stats.c`, `ui.c` | maps 32 or 64 cells wide (`colshift` 5 or 6), plane size 64x32, or 128x32 when map 0's `colshift` is not 6 | |

Every screen has:

* 224 lines (`MODE2` $64: V28, display on, vblank interrupt on, DMA on), non-interlaced.
* **No shadow/highlight**. `MODE4` is $00 or $81, and the shadow bit ($08) is never set.
* **Whole-screen scroll** (`MODE3` $00: one H scroll and one V scroll for each plane) on every screen but
  the title. There the copper needs only bitplane pointers and `BPLCON1` once a frame, plus the split at
  the ring buffer's wrap.
* **The title is the exception**: `title.c` sets `MODE3` + 3 (per-line H scroll) for the credits
  (`CreditsLineScroll`, $51 lines of plane B moving). On the Amiga that is a copper `MOVE` of `BPLCON1`
  and plane B's pointers on each of those lines, rebuilt each frame from the game's scroll table.
* Planes 32 cells (256 px) tall, 64 cells (or 128) wide. The rink is wider and taller than the plane, so the
  game streams rows and columns into the plane as it scrolls. The Genesis plane is already a ring buffer,
  and the Amiga playfield can mirror it cell for cell (PROMPT.md 7.1).
* Colour 0 of palette 0 is the border (`BGCOL` $00).

So the display backend needs two widths (256 and 320, both standard low-res DMA fetches), two planes plus
a window, sprites, and four 16-colour palettes. None of it needs anything AGA cannot do in 6 bitplanes:
there is no shadow mode to fold into the palette.
