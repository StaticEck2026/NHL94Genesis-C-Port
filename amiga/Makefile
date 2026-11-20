# The native Amiga 1200 AGA build (amiga/PROMPT.md). From the repository root: make -C amiga
#   make -C amiga            build/amiga/NHL94AGA (the AmigaDOS program) and build/amiga/nhl94-aga-vX.Y.Z.adf
#   make -C amiga check      the gates the host can run: the program's hunks, and A-LEGAL on the ADF (ROM=...)
#   make -C amiga host-tests ROM=...  the host checks: arenatest (P3), halcheck on every vanilla golden run (P5)
#   make -C amiga verify     build/amiga/NHL94VFY: the game's C for the 68020 with amiga/host/hal_core.c (P5)
#   make -C amiga verify-disks ROM=...  the private P5 test disks (build/amiga/private: never published)
# Toolchain (decision recorded in STATUS.md): GNU GCC and binutils for m68k (Debian/Ubuntu
# gcc-m68k-linux-gnu), freestanding, -mcpu=68020 -msoft-float, linked by amiga/host/amiga.ld and
# turned into a hunk file by amiga/tools/elf2hunk.py. ADF: amitools' xdftool.
CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
OBJDUMP := $(CROSS)objdump
PY      := python3 -I
VERSION := $(shell cat VERSION)
COMMIT  := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
DATE    := $(shell git log -1 --format=%cd --date=format:%d.%m.%Y 2>/dev/null || date -u +%d.%m.%Y)
B       := ../build/amiga
ROM     ?= ../NHL 94 (UE) [!].gen
CFLAGS  := -mcpu=68020 -msoft-float -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-common \
           -fno-asynchronous-unwind-tables --param=min-pagesize=0 -std=gnu11 -Wall -Wextra -Wshadow -Wstrict-prototypes \
           -Wmissing-prototypes -Werror -I$(B) -Ihost
LIBGCC  := $(shell $(CC) -mcpu=68020 -print-libgcc-file-name)
OBJS    := $(B)/start.o $(B)/boot.o $(B)/libc.o
# the game's C (src/, without the Z80 relay) and the verify program (P5)
BESRC   := $(B)/src-be
GFLAGS  := -mcpu=68020 -msoft-float -O2 -fomit-frame-pointer -fno-pic -fno-common -fno-asynchronous-unwind-tables \
           --param=min-pagesize=0 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -std=gnu11 -Wall -Wextra -Wshadow \
           -Wstrict-prototypes -Wmissing-prototypes -Wno-unused-parameter -Werror -I$(BESRC) -Ihost -I$(B)
# src/ under the m68k ABI only: ram.c's little-endian table is unused on a big-endian machine, and
# optsetup.c reads past an array's end as the 68000 does (src/mem.h); both stay warnings (STATUS.md)
GAMEWARN := -Wno-unused-const-variable -Wno-error=array-bounds
# the game is compiled from a copy of src/ with the big-endian RAM accessors (D-4, amiga/tools/mem_be.py);
# src/ itself is unchanged
GSRC    := $(filter-out ../src/z80.c,$(wildcard ../src/*.c))
GOBJS   := $(GSRC:../src/%.c=$(B)/game/%.o)
VOBJS   := $(B)/start.o $(B)/verify.o $(B)/hal_core.o $(B)/libc_amiga.o $(B)/libc.o $(GOBJS)
ADF     := $(B)/nhl94-aga-v$(VERSION).adf

all: $(B)/NHL94AGA $(B)/NHL94VFY $(ADF)

$(B)/version.h: FORCE
	@mkdir -p $(B)
	@printf '#define NHL94AGA_VERSION "%s"\n#define NHL94AGA_DATE "%s"\n#define NHL94AGA_COMMIT "%s"\n#define NHL94AGA_VSTRING "NHL94AGA %s (%s) %s"\n' \
	    "$(VERSION)" "$(DATE)" "$(COMMIT)" "$(VERSION)" "$(DATE)" "$(COMMIT)" > $@.new
	@cmp -s $@.new $@ && rm $@.new || mv $@.new $@
FORCE:
$(B)/%.o: host/%.c host/amiga.h $(B)/version.h
	$(CC) $(CFLAGS) -c $< -o $@
$(BESRC)/.stamp: $(wildcard ../src/*.c ../src/*.h) tools/mem_be.py
	$(PY) tools/mem_be.py ../src $(BESRC)
	@touch $@
$(B)/game/%.o: ../src/%.c $(BESRC)/.stamp
	@mkdir -p $(B)/game
	$(CC) $(GFLAGS) $(GAMEWARN) -c $(BESRC)/$*.c -o $@
$(B)/verify.o $(B)/hal_core.o $(B)/libc_amiga.o: $(B)/%.o: host/%.c host/hal_core.h host/hash.h host/libc_amiga.h host/amiga.h $(B)/version.h $(BESRC)/.stamp
	$(CC) $(GFLAGS) -c $< -o $@
$(B)/NHL94VFY.elf: $(VOBJS) host/amiga.ld
	$(LD) -T host/amiga.ld --emit-relocs -nostdlib -o $@ $(VOBJS) $(LIBGCC) -Map $(B)/NHL94VFY.map
$(B)/NHL94VFY: $(B)/NHL94VFY.elf tools/elf2hunk.py
	$(PY) tools/elf2hunk.py $< $@
verify: $(B)/NHL94VFY
verify-disks: $(B)/NHL94AGA $(B)/NHL94VFY tools/mkverify.sh tools/mkadf.sh
	tools/mkverify.sh "$(CURDIR)/$(ROM)" $(B) $(VERSION)
$(B)/%.o: host/%.S
	@mkdir -p $(B)
	$(CC) -mcpu=68020 -c $< -o $@
$(B)/NHL94AGA.elf: $(OBJS) host/amiga.ld
	$(LD) -T host/amiga.ld --emit-relocs -nostdlib -o $@ $(OBJS) $(LIBGCC) -Map $(B)/NHL94AGA.map
$(B)/NHL94AGA: $(B)/NHL94AGA.elf tools/elf2hunk.py
	$(PY) tools/elf2hunk.py $< $@
$(ADF): $(B)/NHL94AGA $(B)/NHL94VFY tools/mkadf.sh CHANGELOG.md $(wildcard disk/* disk/s/*)
	tools/mkadf.sh $(B) $(VERSION) $@

host-tests:
	@mkdir -p ../build/amiga-tools
	cc -std=gnu11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -Ihost host/arena.c tools/arenatest.c -o ../build/amiga-tools/arenatest
	../build/amiga-tools/arenatest
	cd .. && amiga/tools/halcheck.sh "$(CURDIR)/$(ROM)"
check: all
	hunktool info $(B)/NHL94AGA
	$(PY) tools/noretail.py --program $(B)/NHL94VFY "$(ROM)" $(ADF)
disasm: $(B)/NHL94AGA.elf
	$(OBJDUMP) -d $< | less
clean:
	rm -f $(B)/*.o $(B)/game/*.o $(B)/NHL94AGA $(B)/NHL94AGA.elf $(B)/NHL94AGA.map $(B)/NHL94VFY $(B)/NHL94VFY.elf $(B)/NHL94VFY.map $(B)/version.h
.PHONY: all check disasm clean verify verify-disks host-tests FORCE
