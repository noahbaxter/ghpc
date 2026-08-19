# M2 booting the recompiled GH2

Criterion: reaches main, survives IOP init. Met.

## Build

12,664 generated files plus runtime linked into a 127 MB binary. 493 ninja
targets, 0 errors, 284s with unity build (batch 32) and ccache. Incremental
runtime-only rebuild and relink is ~3 min.

## Boot progression

| run | change | result |
|---|---|---|
| 1 | baseline | SIGTRAP, heap corruption |
| 2 | GetRomName fix, game data staged | clean exit on unimplemented stub |
| 3 | sceFs stubs no-op | runs indefinitely, unhandled IOP RPC |

Recompiled game code executes: the process constructed the path `GEN/MAIN.HDR`
from format strings present only in the game binary (`gen/main.hdr`,
`gen/main_%i.ark`) and absent from the runtime. It also allocated 640x512 and
512x128 textures, its GS render targets.

Boot reaches `_start`, `ps2_main`, `App::App`, `SystemInit`, `DateTimeInit`,
filesystem init, IOP RPC.

## Bug 1, unbounded write in ps2_syscalls::GetRomName

Presenting crash was a libmalloc "memory corruption of free block" abort on the
main thread inside CoreGraphics. Origin was on GameThread: `__bzero` from
`strncpy` from `GetRomName`, length register 5,722,059.

The handler read `$a1` as a buffer size and called `strncpy(dst, name, size-1)`.
`strncpy` zero-pads to n, so a garbage length writes past guest RAM into the
host heap.

Disassembly of the caller (`IsT10K` at 0x34a538) shows `jal` to 0x34a4c8 with a
`nop` delay slot and neither `$a0` nor `$a1` set. The function takes no
arguments; `$a1` held a stale value.

0x34a4c8 is statically-linked Sony libscf. It calls `sceOpen` on a rom0: path
and caches the result behind an init guard at 0x4473d0.

`ps2ResolveGuestPointer` never fails. Out-of-range addresses are masked back
into RAM (`phys &= PS2_RAM_MASK`) and it returns true, so `getMemPtr` cannot
reject a bad pointer. Bounding the length is the only available check.

Fix: `patches/0001-bound-GetRomName-write.patch`. Ignores `$a1`, writes a fixed
ROMVER string, checks remaining space against RAM or scratchpad size.

## Bug 2, unimplemented CDVD filesystem semaphores

`sceFsSemInit`, `sceFsSemExit`, `sceFsSigSema`, `sceFsIntrSigSema`, `sceFsDbChk`
were `TODO_NAMED`, which halts the run.

The no-op patch applied here was superseded and removed. See m3. The correct fix
was to stop replacing the game's own implementations.

## Blocker at end of M2

One unhandled RPC, `sid=0x0 rpc=0xc pc=0x34f2c8`, with `loadedModules=[]`.

Disc IOP modules (saved to `work/disc/IOP/`): CDVDSTM, LGAUD, LIBSD, MCMAN,
MCSERV, MSIFRPC, PADMAN, SCRTCHPD, SDRDRV, SIO2MAN, SYNTH_S, IOPRP300.IMG.
`SYNTH_S`/`SYNTH_R` and `LGAUD` carry audio.

## Stub binding

The analyzer binds stubs by matching game symbols to runtime handler names. A
symbolized ELF supplies 12,663 candidate names. Audited all 251 stubs, the
remainder are libc and kernel syscalls. See m3 for the full mechanism.
