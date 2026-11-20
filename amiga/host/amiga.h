/* The A1200's firmware and chips as this port uses them: library calls by LVO (through lib_call,
 * amiga/host/start.S), the structure offsets it reads, and the custom chip registers. Written from
 * the AmigaOS 3.1 autodocs and the Amiga Hardware Reference Manual, 3rd edition; no NDK header is
 * needed. Every library call goes through its base and its LVO, never a fixed ROM address. */
#pragma once

typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;
typedef long LONG;
typedef signed char BYTE;
typedef short WORD;

/* lib_call(base, lvo, r): load d0, d1, d2, d3, a0, a1, a2, a3 from r[0..7], a6 = base, jsr lvo(a6);
 * returns d0. (a4, a5 and d4-d7 are never inputs in this port's calls.) */
ULONG lib_call(void *base, LONG lvo, const ULONG *r);
ULONG get_vbr(void);                     /* movec vbr,d0 through exec Supervisor() (68010 and up) */
void l3_handler(void);                   /* the level 3 interrupt (start.S): vblank -> vb_hook */
extern volatile ULONG vb_count;
extern void (*volatile vb_hook)(void);

enum { D0, D1, D2, D3, A0, A1, A2, A3 };
#define CALL(base, lvo, ...) lib_call((base), (lvo), (const ULONG[8]){ __VA_ARGS__ })
#define REG(r, v) [r] = (ULONG)(v)

/* exec.library */
#define LVO_Supervisor     (-30)
#define LVO_Forbid         (-132)
#define LVO_Permit         (-138)
#define LVO_AllocMem       (-198)
#define LVO_FreeMem        (-210)
#define LVO_AvailMem       (-216)
#define LVO_FindTask       (-294)
#define LVO_GetMsg         (-372)
#define LVO_ReplyMsg       (-378)
#define LVO_WaitPort       (-384)
#define LVO_CloseLibrary   (-414)
#define LVO_OpenDevice     (-444)
#define LVO_CloseDevice    (-450)
#define LVO_DoIO           (-456)
#define LVO_OpenLibrary    (-552)
#define LVO_CacheClearU    (-636)
#define LVO_CreateIORequest (-654)
#define LVO_DeleteIORequest (-660)
#define LVO_CreateMsgPort  (-666)
#define LVO_DeleteMsgPort  (-672)
#define MEMF_ANY     0x00000000UL
#define MEMF_CHIP    0x00000002UL
#define MEMF_FAST    0x00000004UL
#define MEMF_CLEAR   0x00010000UL
#define MEMF_LARGEST 0x00020000UL
#define EXEC_VERSION(s)  (*(const UWORD *)((const UBYTE *)(s) + 20))   /* lib_Version */
#define EXEC_SOFTVER(s)  (*(const UWORD *)((const UBYTE *)(s) + 34))   /* Kickstart revision: 40.068 */
#define EXEC_ATTNFLAGS(s) (*(const UWORD *)((const UBYTE *)(s) + 296))
#define AFF_68020 0x0002
#define AFF_68030 0x0004
#define AFF_68040 0x0008
#define AFF_68881 0x0010
#define PR_MSGPORT(p) ((void *)((UBYTE *)(p) + 92))                   /* struct Process */
#define PR_CLI(p)     (*(const ULONG *)((const UBYTE *)(p) + 172))

/* IOStdReq (48 bytes) */
#define IO_COMMAND(io) (*(UWORD *)((UBYTE *)(io) + 28))
#define IO_LENGTH(io)  (*(ULONG *)((UBYTE *)(io) + 36))
#define TD_MOTOR 9

/* graphics.library */
#define LVO_TextLength     (-54)
#define LVO_Text           (-60)
#define LVO_SetFont        (-66)
#define LVO_InitRastPort   (-198)
#define LVO_LoadView       (-222)
#define LVO_WaitBlit       (-228)
#define LVO_Move           (-240)
#define LVO_WaitTOF        (-270)
#define LVO_SetAPen        (-342)
#define LVO_InitBitMap     (-390)
#define LVO_OwnBlitter     (-456)
#define LVO_DisownBlitter  (-462)
#define GFX_ACTIVIEW(g)    (*(void **)((UBYTE *)(g) + 34))
#define GFX_COPINIT(g)     (*(void **)((UBYTE *)(g) + 38))
#define GFX_DEFAULTFONT(g) (*(void **)((UBYTE *)(g) + 154))
#define GFX_DISPLAYFLAGS(g) (*(const UWORD *)((const UBYTE *)(g) + 206))
#define GFX_CHIPREVBITS0(g) (*(const UBYTE *)((const UBYTE *)(g) + 236))
#define GFXF_AA_ALICE 0x04
#define GFXF_AA_LISA  0x08
#define DISPLAY_PAL   0x0004
#define BITMAP_SIZE   40
#define RASTPORT_SIZE 100
#define RP_BITMAP(rp) (*(void **)((UBYTE *)(rp) + 4))
#define BM_PLANE(bm, i) (*(void **)((UBYTE *)(bm) + 8 + 4 * (i)))

/* intuition.library */
#define LVO_RethinkDisplay  (-390)
#define LVO_EasyRequestArgs (-588)

/* dos.library */
#define LVO_Open   (-30)
#define LVO_Close  (-36)
#define LVO_Read   (-42)
#define LVO_Write  (-48)
#define LVO_Output (-60)
#define MODE_NEWFILE 1006

/* custom chips ($DFF000) */
#define CUSTOM 0xDFF000UL
#define CREG(o) (*(volatile UWORD *)(CUSTOM + (o)))
#define CREGL(o) (*(volatile ULONG *)(CUSTOM + (o)))
#define DMACONR 0x002
#define VPOSR   0x004
#define VHPOSR  0x006
#define ADKCONR 0x010
#define INTENAR 0x01C
#define INTREQR 0x01E
#define COP1LC  0x080
#define COPJMP1 0x088
#define DIWSTRT 0x08E
#define DIWSTOP 0x090
#define DDFSTRT 0x092
#define DDFSTOP 0x094
#define DMACON  0x096
#define INTENA  0x09A
#define INTREQ  0x09C
#define ADKCON  0x09E
#define BPL1PTH 0x0E0
#define BPL1PTL 0x0E2
#define BPLCON0 0x100
#define BPLCON1 0x102
#define BPLCON2 0x104
#define BPLCON3 0x106
#define BPL1MOD 0x108
#define BPL2MOD 0x10A
#define BPLCON4 0x10C
#define SPR0PTH 0x120
#define COLOR00 0x180
#define COLOR01 0x182
#define BEAMCON0 0x1DC
#define FMODE   0x1FC
#define DMAF_SETCLR 0x8000
#define DMAF_MASTER 0x0200
#define DMAF_RASTER 0x0100
#define DMAF_COPPER 0x0080
#define DMAF_BLITTER 0x0040
#define DMAF_SPRITE 0x0020
#define INTF_SETCLR 0x8000
#define INTF_INTEN  0x4000
#define INTF_VERTB  0x0020
#define BPLCON3_LOCT 0x0200

/* CIA-A ($BFE001): PRA bit 6 = port 1 fire (left mouse button), bit 7 = port 2 fire */
#define CIAA_PRA (*(volatile UBYTE *)0xBFE001UL)
