# The native Amiga 1200 AGA build (amiga/PROMPT.md). From the repository root: make -C amiga
#   make -C amiga            build/amiga/NHL94AGA (the AmigaDOS program) and build/amiga/nhl94-aga-vX.Y.Z.adf
#   make -C amiga check      the gates the host can run: the program's hunks, and A-LEGAL on the ADF (ROM=...)
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
ADF     := $(B)/nhl94-aga-v$(VERSION).adf

all: $(B)/NHL94AGA $(ADF)

$(B)/version.h: VERSION
	@mkdir -p $(B)
	@printf '#define NHL94AGA_VERSION "%s"\n#define NHL94AGA_VSTRING "NHL94AGA %s (%s) %s"\n' \
	    "$(VERSION)" "$(VERSION)" "$(DATE)" "$(COMMIT)" > $@
$(B)/%.o: host/%.c host/amiga.h $(B)/version.h
	$(CC) $(CFLAGS) -c $< -o $@
$(B)/%.o: host/%.S
	@mkdir -p $(B)
	$(CC) -mcpu=68020 -c $< -o $@
$(B)/NHL94AGA.elf: $(OBJS) host/amiga.ld
	$(LD) -T host/amiga.ld --emit-relocs -nostdlib -o $@ $(OBJS) $(LIBGCC) -Map $(B)/NHL94AGA.map
$(B)/NHL94AGA: $(B)/NHL94AGA.elf tools/elf2hunk.py
	$(PY) tools/elf2hunk.py $< $@
$(ADF): $(B)/NHL94AGA tools/mkadf.sh CHANGELOG.md $(wildcard disk/* disk/s/*)
	tools/mkadf.sh $(B)/NHL94AGA $(VERSION) $@

check: all
	hunktool info $(B)/NHL94AGA
	$(PY) tools/noretail.py "$(ROM)" $(ADF)
disasm: $(B)/NHL94AGA.elf
	$(OBJDUMP) -d $< | less
clean:
	rm -f $(B)/*.o $(B)/NHL94AGA $(B)/NHL94AGA.elf $(B)/NHL94AGA.map $(B)/version.h
.PHONY: all check disasm clean
