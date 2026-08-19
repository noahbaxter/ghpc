# M2: booting the recompiled GH2

Kill criterion was "reaches main, survives IOP init". Passed, and further.

## Build

12,664 generated files + runtime linked into one 127 MB binary. 493 ninja
targets, 0 errors, 284s with unity build on (batch 32) and ccache. Faster than
the 15 min estimate. Incremental runtime-only rebuild + relink is ~3 min.

## Boot progression

Run 1: SIGTRAP, heap corruption.
Run 2 (after GetRomName fix + game data): clean exit on an unimplemented stub.
Run 3 (after sceFs stubs): runs indefinitely, hung on an unhandled IOP RPC.

Confirmed the recompiled game code genuinely executes: the process built the
path `GEN/MAIN.HDR` from format strings that exist only in the game binary
(`gen/main.hdr`, `gen/main_%i.ark`), not anywhere in the runtime. It also
allocated 640x512 and 512x128 textures, ie set up its GS render targets.

Boot reaches: `_start` -> `ps2_main` -> `App::App` -> `SystemInit` ->
`DateTimeInit` -> filesystem init -> IOP RPC.

## Bug 1: unbounded write in ps2_syscalls::GetRomName

Crash was a libmalloc "memory corruption of free block" abort on the main
thread, inside CoreGraphics. That was the symptom. Cause was on GameThread:
`__bzero` <- `strncpy` <- `GetRomName`, with a length register of 5,722,059.

The handler read `$a1` as a buffer size and did `strncpy(dst, name, size-1)`.
`strncpy` zero-pads to n, so a garbage length memsets its way off the end of
guest RAM and into the host heap.

Disassembly of the caller (`IsT10K` at 0x34a538) shows the `jal` to 0x34a4c8
with a `nop` in the delay slot and neither `$a0` nor `$a1` ever set. The real
function takes no arguments. `$a1` was stale garbage from an earlier call.

0x34a4c8 is statically-linked Sony libscf, not Harmonix code. It calls
`sceOpen` on a rom0: path and caches the result behind an init guard at
0x4473d0. Stubbing it was the right call; the handler's assumed signature was
simply wrong.

Aggravating factor: `ps2ResolveGuestPointer` never fails. Out-of-range
addresses get masked back into RAM (`phys &= PS2_RAM_MASK`) and it returns
true, so `getMemPtr` cannot reject a bad pointer. Bounding the length is the
only real defence.

Fix in `patches/0001-bound-GetRomName-write.patch`: ignore `$a1`, write a fixed
ROMVER string, check remaining space against RAM/scratchpad size first.

## Bug 2: unimplemented CDVD filesystem semaphores

`sceFsSemInit`, `sceFsSemExit`, `sceFsSigSema`, `sceFsIntrSigSema`, `sceFsDbChk`
were all `TODO_NAMED`, which hard-stops the run. They are Sony CDVD filesystem
lock helpers. The runtime services file I/O synchronously on the host, so there
is nothing to serialise and success is correct.

Fix in `patches/0002-implement-sceFs-sema-noops.patch`.

## Current blocker: IOP modules never load

Hung on one unhandled RPC, `sid=0x0 rpc=0xc pc=0x34f2c8`, with
`loadedModules=[]`. Nothing is stuck in a loop, it is waiting on an RPC server
that does not exist because no IRX has been loaded.

The disc ships these in `IOP/` (saved to `work/disc/IOP/`): CDVDSTM, LGAUD,
LIBSD, MCMAN, MCSERV, MSIFRPC, PADMAN, SCRTCHPD, SDRDRV, SIO2MAN, SYNTH_S,
IOPRP300.IMG. `SYNTH_S`/`SYNTH_R` and `LGAUD` are the audio path.

So the next chunk is a `ps2xIOP` game profile for GH2, and it lands directly on
audio. That was flagged as the real risk from the start and it still is.

## Note on symbol-name auto-binding

The analyzer binds stubs by matching game symbols against runtime handler
names. Our symbolized ELF gives it 12,663 symbols to collide with, so this is a
larger hazard here than on a stripped retail ELF, not a smaller one. Audited all
251 stubs: the rest are legitimate libc and kernel syscalls. GetRomName was the
only bad one, but the class of bug is worth re-checking as more get exercised.
