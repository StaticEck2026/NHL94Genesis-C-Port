/* amiga/host/hostmenu.c: the host menu before the title (PROMPT.md 7.6): region and speed (D-2), the
 * controllers (which port is player 1, which player the keyboard is, the 4 Way Play adapter), the
 * audio filter and power LED, Start and Quit. It sets nothing inside the game's own menus. Its choices
 * are kept in NHL94.prefs beside the program. */
#pragma once

struct hostprefs { int pal, swap, keyboard, fourway, filter; };

int hostmenu_font(void *sysbase);                         /* the OS running: the default font copied (0: none; blocks drawn) */
void hostmenu_load(struct hostprefs *p, const char *path);   /* the OS running: unchanged when there is no file */
int hostmenu_save(const struct hostprefs *p, const char *path);
int hostmenu_run(struct hostprefs *p);                    /* the machine taken, input started: 1 start, 0 quit */
