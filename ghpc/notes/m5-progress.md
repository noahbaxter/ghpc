# M5 boot past the filesystem, in progress

M4 is complete: a fileio service answers SID 0x80000001 and the game reads the
ARK header. This note covers what stood between that and the renderer.

Eight blockers, in the order they were hit. Each was found by reading the ELF or
by the thread census rather than by guessing.

## 1. sceGetstat, not open

`fn=0x0c` is `sceGetstat`. The word at offset 0 of the request is an EE
semaphore id, not a command selector. Full function table and packet layout in
`m4-progress.md`.

## 2. Half the filesystem was stubbed

The runtime stubs six of the nine sceFs entry points (`sceOpen`, `sceClose`,
`sceRead`, `sceWrite`, `sceLseek`, `sceIoctl`) and forwards them to its own
`fioOpen` family, but has no stub for `sceGetstat`, `sceMkdir` or `sceLseek64`.
Getstat therefore ran the game's real code and reached the RPC while open was
redirected into the runtime's own descriptor table. All nine are now on the
denylist so one service owns the descriptors.

## 3. Version handshake

`sceFsInit+0x154` issues `fn=0xff` and copies the reply's first word to
`0x005b64e8`. `_fs_version` (`0x0034fa48`) memcmps it against the `"3000"` field
of `__ps2_klibinfo__` (`0x00447420`, `PsIIlibkernl3000`) and against the `"...."`
wildcard at `*_fswildcard`. `sceOpen+0x74` returns `0xfffefffc` before sending
anything when neither matches; `sceGetstat` has no such check. That asymmetry is
why getstat worked and open failed silently. The service answers `"3000"`.

## 4. Asynchronous reads

The ARK is opened with `flags=0x8001`, so SCE_NOWAIT. `sceRead+0xe0` parks its
semaphore id in `_sceFs_q` (32 entries at `0x00447448`) and negates the packet's
semaphore field, then returns 0 without waiting. `sceIoctl(fd, 1, argp)` is the
poll and sends no RPC at all: it scans that queue and reports busy while any slot
is occupied. On hardware `_sceFs_Rcv_Intr` retires the slot. This service
completes synchronously and now retires it directly.

MAIN.HDR then reads completely: 4 x 16384 + 13066 = 78602 bytes, matching the
file exactly.

## 5. COP0 Count never advanced

A runtime gap, not a GH2 quirk. `Timer::Sleep` (`0x002f2ca8`) busy-waits on
`mfc0 $9`, which the recompiler lowers to a plain read of `ctx->cop0_count`.
Nothing in the runtime ever wrote that field, so any game busy-waiting on Count
would hang forever. Now driven from `m_eeCycle >> 1` in
`EeScheduler::accountCycles`, which generated loops reach via `checkpointDue`.

## 6. USB keyboard assert

`Keyboard_EE.cpp line 75, ret == USBKB_OK`. Nothing registered SID 0x80000211,
and `sceUsbKbInit+0xd8` also fails when the reply's device count is zero, so an
empty but successful reply is not enough. The service reports one device. Reads
simply never produce keystrokes.

## 7. MMIO addresses were constant folded wrong

`InstructionTranslator::effectiveMemoryHintFor` forced `hasAddress` with the
analyzer's per-instruction MMIO constant. The analyzer only tracks the `lui`, so
a `lui`/`ori` pair collapsed to the page base: `PsRnd::VSync` writes T1_MODE
(`0x10000810`) and polls T1_COUNT (`0x10000800`), but both were emitted as
`0x10000000`. The mode write landed on timer 0's count, CUE was never set, and
the poll never terminated.

MMIO accesses are rare and already route through `runtime->LoadN`/`StoreN`, which
dispatch on the address they are handed, so the address is now computed from the
base register instead.

## 8. MFIFO and the fromSPR channel

`PsRnd::Reset+0x3a0` sets up a memory FIFO: `D_RBOR` = ring base, `D_RBSR` =
`0x7fff0` (512 KB), `D1_TADR` = ring base, `0x1000D010` (D8_MADR) = ring base,
and `D_CTRL |= 8` so the MFD field selects VIF1 as the drain channel.
`BeginDrawing` seeds TADR from D8_MADR and kicks `D1_CHCR = 0x105`.
`PsRnd::FlushPacket` builds packets in scratchpad and sends them with
`DmaPacket::Send(spr, 8)`, which kicks channel 8, fromSPR.

None of that was emulated. `RBOR`/`RBSR` appear only in a DMA save/restore stub,
the DMAC checked `D_CTRL` bit 0 and ignored MFD, and channel 8 was not handled at
all. The chain walker read whatever sat at TADR in the empty ring, treated it as
a tag, and left TADR one quadword past the fill pointer, so FlushPacket computed
16 bytes of free space forever.

Added: channel 8 copies scratchpad into the ring and advances D8_MADR with ring
wrap, and the VIF1 chain walk stops at the fill pointer and wraps inside the
ring. TADR and the fill pointer now track each other and packets flow.

## Rendering state

The magenta sentinel is gone. The GS configures a framebuffer, presents a
512x448 frame at displayFbp 56, and uploads it every frame, so the display half
of the pipeline works end to end.

The frame is empty: 0 of 229376 pixels written, and the GS reports
`gifPackets=0 imageUploads=0`. Nothing is drawn into the buffer.

That splits the GS cleanly:

- display and presentation (privileged registers, framebuffer config, present,
  host upload) works
- drawing (DMA -> VIF1 -> VU1 -> GIF -> rasterizer) delivers nothing

DMA reaches VIF1 (the MFIFO tracks correctly and FlushPacket submits), so the
break is between VIF1 and the GIF: VIF1 unpack, the VU1 microprogram, and the
XGKICK that hands primitives to the GIF. None of that path has been exercised
yet.

## Where it stops (updated 2026-08-20)

Superseded text here reported a fatal DTA assert, no pixels, and a magenta
framebuffer. All three are now wrong. Current state:

- The RedOctane splash renders correctly.
- The game boots through to its main menu and writes a memory card save to
  `work/mc0/BASLUS-21447/`.
- It sits on the main menu. In an automated run nothing presses a button, so
  use `GHPC_PAD_AUTO=1` to advance it.
- Magenta is not the current state. Magenta is the runtime's "GS never wrote a
  pixel" sentinel and the GS does latch real frames now.

## Storage stack: fully working

Three separate conclusions that the ARK "never loads" were all wrong. Verified
end to end:

- `Archive::Archive("gen/main")` -> `Archive::Read` -> `NewFile("gen/main.hdr")`
  -> `ArkHash::Read` x3. Header parses: 1718 string entries, 3370 offsets, 1585
  file records of 20 bytes, and the recorded ark size matches MAIN_0.ARK exactly.
- `Archive::GetFileInfo` returns 1 for `config/gen/gh2.dtb`, `macros.dtb` and
  `sfx_macros.dtb`. The header stores directories and filenames as separate
  entries, so the lookup splits and hashes each half.
- `BlockMgr` streams 64 KB blocks: `ArkFile::Read` -> `ReadAsync` ->
  `GetBlockData` -> `AddTask` -> `Poll` -> `ReadDone`.

The reason no filesystem open for MAIN_0.ARK ever appears is that the game does
not open it as a file. `BlockMgr::Poll` calls `CDRead(arkNum, sector, count,
buffer)` (`0x002f9f70`), which calls `sceCdSearchFile("\GEN\MAIN_0.ARK;1")`
once for a base LBN and then `sceCdRead` per block. The runtime's pseudo-LBN
table resolves that to the host file.

Data integrity confirmed at a 2.94 GB offset: `got=65536/65536`, and the bytes
delivered match the file at that offset exactly. Large seeks are fine.

## Rendering: pixels, no imagery

The GS presents a real 512x448 frame at displayFbp 56 every frame, so the
magenta sentinel is gone. The frame is empty: 0 of 229376 pixels written.

Only 3 GIF packets are submitted in a whole run (one PATH3 of 128 bytes, two
PATH2 of 32 bytes), which is register setup rather than drawing. The VIF1
opcode histogram shows 18 MPG microprogram uploads and 21 UNPACKs but zero
MSCAL, so VU1 is loaded and never run.

That is a consequence, not a cause. Only 8 `processVIF1Data` calls happen in a
whole run because the game stops drawing: `Debug::Fail` -> `DebugModal` ->
`JoypadWaitForButton` never calls `BeginDrawing`, so the last frame freezes.

## The DTA blocker, superseded and disproven

Earlier text here described a fatal `DataArray::ExecuteScript` assert at
`ui/ui.dtb:54`, with content from `ui/manage_bands.dtb:225` nested as a single
array node among Command siblings, and called it "the one remaining blocker".
That is not true. The root-cause section under it has been deleted rather than
left in place to be re-read as live.

Disproven by measurement, 2026-08-20. With the input stall cleared and the UI
advancing to the main menu, a 90 s run from a virgin card
(`GHPC_PAD_AUTO=1 GHPC_PROBE=0x2fcf98`):

    ui/ui.dtb      0 occurrences
    manage_bands   0 occurrences
    MISMATCH       0
    FAIL-AT        1   (arr=0x615390 nodeType=16, skipped, non-fatal)
    touched        splash, main, mem_card, init, pause, cheats_funcs

Neither file is loaded at all, so that assert cannot be occurring. The one
surviving FAIL-AT is a different array, is non-fatal, and is unchanged from
before the pad fix, so it is not gated on UI progress either.

If it ever resurfaces, the reference for what composition SHOULD do is
rb3-decomp `DataArray.cpp:747-753`: `kDataInclude` replaces the marker node in
place, `size += macro->Size() - 1`, and the child's elements become siblings of
the parent. `DataFile.cpp:211-231` is the parse-time path and pushes each child
node individually. `kDataMerge` is a different operation, do not conflate them.
## Tooling: scripts/dtb.py

Decrypts and inspects any DTB straight out of the ARK, so the data side can be
checked independently of the runtime.

```sh
./scripts/dtb.py --list                       # every file in the ARK
./scripts/dtb.py ui/gen/ui.dtb                # locate, decrypt, summarise
./scripts/dtb.py ui/gen/ui.dtb -o out.dtb     # write the plaintext
./scripts/dtb.py --raw <offset> <size>        # decrypt an explicit range
```

Cipher is the PS2 variant: a 256 entry LCG table keyed by the first four bytes,
walked with two rolling indices that wrap at 0xF9.

## Next

The DTA line is dead, see the superseded section above. Current state is that
the game boots to its main menu and writes a memory card save. Open threads,
none of them blocking a boot:

- `cop0_count` is not tied to wall time, so guest time races ahead of real time.
- 8-pixel stripes on later screens, a PSMCT32 block layout on a PSMCT16 buffer.
- Video is a second hardware seam an Rnd-layer replacement will not cover.

## Ruled out, do not reopen

- "The ARK never loads." Wrong three separate times. Storage works end to end.
- "The green fill on screens 2-3 is game output." It was manufactured by the
  present path re-reading a CT16 buffer as CT32. Fixed, patch 0025.
- "Undefined macros in `config/gen/macros.dtb` cause the is-not-Command error."
  Superseded with the rest of the DTA theory.
## Build configurations

Diagnostics are behind the `PS2X_GHPC_DIAG` CMake option, off by default. Debug
and release use separate build trees so both binaries exist at once and neither
forces a recompile of the other:

```sh
./scripts/build.sh --from=build --fast            # -> build/,       silent
./scripts/build.sh --from=build --fast --debug    # -> build-debug/, diagnostics
./scripts/run.sh --quiet                          # runs build/
./scripts/run.sh --quiet --debug                  # runs build-debug/
```

Each tree is about 1.5 GB. A cold debug tree takes ~5 minutes; after that both
rebuild incrementally in ~10-20s.

What `--debug` turns on, all `#if GHPC_DIAG`:

- `EeScheduler`: thread census every 3s (status, wait reason, `$ra`, `$sp`, COP0
  Count, semaphores, event flags, plus a stack sweep that recovers inline assert
  text)
- `gs_frontend` / `ps2_runtime`: presentation latch results and frame content
- `ps2_vif1_interpreter`: VIF1 packet sizes and an opcode histogram
- `ps2_memory`: GIF packet submissions
- `Kernel/Stubs/CD.cpp`: `sceCdRead` and `sceCdSearchFile` tracing
- `ps2xIOP/modules/fileio.cpp`: per-call filesystem trace
- generated files under `ps2xRuntime/src/runner/`: entry probes on the ARK path
  (`Archive`, `ArkHash`, `ArkFile`, `BlockMgr`, `CDRead`) and a `GHPC_SKIP_MODAL`
  env lever that makes `JoypadWaitForButton` return immediately

The generated-file probes are the only fragile part: `--from=recomp` regenerates
those files and drops them. Everything else lives in patches 0004 through 0013.

## The green is a present-path artifact, not game output (fixed)

The uniform green fill on screens 2-3 was never drawn by the game. It was
manufactured by a fallback in `GSCpuBackend::Present`.

Chain, each step measured:

- Green is exactly `(0,128,0)`. The PSMCT16 unpack computes `(g<<3)|(g>>2)`,
  which for g=16 gives 132. No 5-bit value yields 128, so the green cannot come
  from a CT16 read. The CT32 branch does `dst[1] = color >> 8`, so
  `color = 0x00008000` gives exactly 128. Little-endian that word is two
  PSMCT16 pixels, `0x8000` then `0x0000`.
- `0x8000` in RGBA5551 is black with the alpha bit set, which is what the game
  legitimately leaves in the framebuffer.
- `copySource` had: if the display frame is fbp 0 and its correct read comes
  back all black, re-read the same memory under every other context FRAME until
  one yields non-black pixels, then present that. The candidate
  `fbp=0 fbw=10 psm=0x0` aliases the real `fbp=0 fbw=8 psm=0x2` buffer, and
  decoding 16-bit data as 32-bit yields 184320 "non-black" pixels of green.

Probe output before the fix, three copies per present:

    fbp=0  psm=0x2 bpp=2 stride=1024 nonBlack=0
    fbp=56 psm=0x2 bpp=2 stride=1024 nonBlack=0
    fbp=0  psm=0x0 bpp=4 stride=2560 top=0x8000x184320   <- presented

Fix: the fallback may not reinterpret pixel format. Skip any candidate whose
`psm` or `fbw` differs from the display frame; a candidate that disagrees on
format is the same bytes decoded wrong, not another buffer.

Measured after, 50 s:

    psm=0x0 presents        1412 -> 0
    top=0x8000x184320 hits  1412 -> 0
    baseline tme/notme      17430/2933 unchanged
    RedOctane frame         nonBlack=9639 unchanged

Consequence for every earlier measurement: `[frame] nonBlack=184320
otherColors=0` did NOT mean "the game drew a flat green quad". It meant the
game drew nothing and the presenter invented pixels. Any theory built on the
green quad being real geometry was built on an artifact. This is why no VU1,
texture, or GIF change ever moved the green: nothing in the guest could.

## Screens 2-3 after the fix: 8-pixel stripes

With the green gone the frames show honest CT16 content: exact 8 px black /
8 px cyan alternation across the full width, `(0,198,222)`, which is an exact
RGBA5551 value (r=0 g=24 b=27). 101376 cyan / 128000 black of 229376.

8 px is the PSMCT32 block width. PSMCT16 blocks are 16x8. So this is the same
16-vs-32 format confusion as the green, one layer down. Next target: find which
side of the write/read pair uses the CT32 block layout on a CT16 buffer.

## Splash logo sequence: measured, then parked

Measured with entry probes on the recompiled Splash/Debug functions. Recording
so it is not re-derived. NOT a live investigation; parked in favour of the
Rnd-layer strategy.

- `App::App` (0x100278) calls `Splash::Show` exactly 3 times, at +0x3a8,
  +0x54c, +0x6d4, interleaved with subsystem init (SynthInit/CharInit between
  1 and 2; OptionsInit/UIManager::Init/Tips::Init/GHUtl::Init between 2 and 3).
- All 3 Show calls execute. `Debug::Fail` fires 0 times. The logos load and
  their objects are found. Loading is NOT the problem.
- `Splash::Wait` (0x24bb90) spins on COP0 Count. It exits after exactly ONE
  iteration on calls 2 and 3, so the intended 1.0 s hold is lost:

      Wait#1 gate(this+0x2c)=0 target=1.0 count=98606680     spins=0 (legit early exit)
      Wait#2 gate=1            target=1.0 count=988226900    spins=0
      Wait#3 gate=1            target=1.0 count=1875545316   spins=1
      Wait#4 gate=1            target=1.0 count=1877651308   spins=2

- Cause: `EeScheduler::accountCycles` (EeScheduler.cpp:628) sets
  `cop0_count = m_eeCycle >> 1`, and `m_eeCycle` is instruction accounting with
  nothing tying it to wall time. Guest time races ahead of real time, so by the
  time Wait is entered its target has "already elapsed".
- Open and NOT explained by the above: the frame dumped after Show#2 still
  reads `nonBlack=9639`, identical to RedOctane. A collapsed Wait shortens a
  hold, it does not stop the framebuffer changing. There is a second effect.

Build trap worth knowing: edits under `work/output` only reach the binary via
`./scripts/build.sh --from=stage`. `--from=build` skips the rsync staging step
and silently builds stale generated code.

## Input stall cleared: the game reaches the main menu

The memory card screen was never a service defect. Pad input is implemented
correctly and was truthfully reporting "no buttons pressed", because in an
automated run nothing presses anything. Unlike the m5 USB keyboard case, where
SID 0x80000211 was never registered, this service answers correctly.

Rejected by measurement first: `fillPadStatus` does `memset(data, 0, 32)` then
writes buttons straight in, and PS2 buttons are active low, so a zero default
would mean every button held forever and no press edges. Not the bug.
`PadInputState::buttons` already defaults to `0xFFFF`.

Patch 0027 adds `GHPC_PAD_AUTO=1`, which pulses Cross and Start alternately
(200 ms held in a 1.2 s cycle, after a 6 s settle) by clearing the active-low
bit in `readPadPortData`. Pulsed, not held, so the UI sees a press edge.

Measured with it enabled, 60 s:

    GotoScreen        4 -> 10
    Enter__8UIScreen  2 -> 7
    new file loaded   ui/main.dtb        (the main menu)
    game wrote        work/mc0/BASLUS-21447/{data,icon.sys,gh.icn}

The game now boots to its main menu and writes a memory card save. That is the
furthest this port has run.

Consequence to watch: the save persists and changes the next boot. See the
BACKLOG entry on `checkrun.sh`'s baseline depending on `work/mc0`. The save from
this session is preserved at `work/mc0.saved-by-autopad`.

## `Debug::Fail` at `ui/ui.dtb:54` traced to a `DataArray::Load` splice defect

The clean-source build dies on an assert whose text was truncated to `Data (`.
The message is formatted at runtime into `gBuf` (`0x5a5c80`, `.bss`), so it does
not exist in the ELF and only a live dump recovers it. The truncation was the
probe stopping at the first non-printable byte, which is the newline after
`Data (`. Full text:

    Data ('screen_back' {'helpbar' 'set_display' {$this 'get_help_text' ( )}} invalid)
     is not Command (file ui/ui.dtb, line 54)

`invalid` is the print name for a type 6 `unhandled` node. That is a legitimate
DTA keyword and is exactly what the disc contains, not corruption.

### Verified

- All 179 DTBs in the ARK parse byte-exact with zero trailing bytes. The data is
  not at fault.
- The assert is raised by `DataNode::Command` (`0x3058d0`), called from
  `DataArray::ExecuteScript+0x24c`, which runs `Node(i)->Command(parent)->Execute()`.
  One rejection in 79 calls.
- The executed array is `ui/ui.dtb:54` after `#include` splicing, 241 nodes.
  On disc that array holds 2 nodes (`symbol 'init'`, `include 'init.dta'`).
  Modelling recursive include splicing plus `#define` name+body pair consumption
  reproduces 241 exactly, which validates the model.
- Exactly 2 of the 241 elements are wrong. Index 14 should be the command at
  `manage_bands.dta:234`; it holds the array at `:225`, which is the last child
  of index 13 (`{new GHScreen nameprof_screen ...}` at `:131`). Index 15 is
  shifted by one and the real last root element, `:239`, is dropped. Net count is
  unchanged, which is why the size looked correct.
- The splice lives inside `DataArray::Load` (`0x2fc9d0`). `InsertNodes` is reached
  only from `DataInsertElems` (the DTA `insert_elems` function) and is not on the
  include path.
- `Load`'s include branch computes `newTotal = oldTotal - 1 + incSize`, which is
  correct, then copies with a `Node(i)` / `DataNode::operator=` loop at
  `0x2fce88..0x2fceb8`. The branch-likely at the loop back edge is translated
  correctly (delay slot annulled when not taken).

### Ruled out

- Corrupt ARK data, corrupt DTB, or a bogus type tag.
- `host0:` as a data path. `cdrom0:` serves `GEN/MAIN.HDR` in full (5 reads
  totalling 78602 bytes, the exact file size). The two `host0:` probes occur
  after the assert and come from the crash handler looking for an ELF to
  symbolicate its trace, which the on-screen "Couldn't find ELF for trace"
  confirms.
- The `file ui/ui.dtb, line 54` provenance being wrong. It is the include site,
  and the single-frame data stack trace is consistent. Correct behavior, and a
  trap worth not chasing.

### Root cause (fixed)

Not the include splice. The splice copy loop is lockstep on `$s2`/`$s4`, so it
cannot shift by one mid-copy, and the corruption is the same shape at every
nesting level. Tracing `DataArray::Load` per element against the on-disc tree
puts the innermost divergence at `manage_bands.dta:156`, whose 3 elements load
as `[symbol, int, array(:156)]` instead of `[symbol, array(:156),
command(:157)]`. A byte counter on `BinStream::Read` shows the stream never
desyncs: element 0 is read at 142943, element 2 at 142966, exactly 23 bytes
later. Element 1 is simply never written, and reads back as allocator garbage
that happens to look like a type 0 `int`. `command(:157)` then falls out of the
array and the parent picks it up, and that shift propagates up six levels to
`ui.dtb:54`.

The skipped write is `PS2Runtime::dispatchGuestBranch`:

    const uint32_t entryPc = ctx->pc;
    targetFn(rdram, ctx, this);
    ...
    if (ctx->pc == entryPc) { ctx->pc = fallthroughPc; }

`entryPc` is just `targetPc`. The rewrite exists so a stub that returns without
touching `pc` still reads as a completed call. But a checkpoint can fire before
the callee runs an instruction, which unwinds the host stack with `ctx->pc`
parked on the callee entry so the scheduler can resume it. By `pc` alone the two
are identical, and they collide whenever the parked address equals the frame's
own target. The recursive `DataArray::Load` / `DataNode::Load` parser is exactly
that: an inner `Load` yields on its call to `0x306258` while an outer frame is
mid-call to `0x306258`. The outer frame rewrites `pc` to its fallthrough, the
resume point is gone, and the element is never loaded.

It fires once in 80756 dispatches of `DataNode::Load`, which is why the damage
was a single node.

Fix: an explicit `yieldInFlight` latch on `EeScheduler`, set on every yielding
return from `checkpointDue` and cleared when the scheduler is about to run guest
code again. `dispatchGuestBranch` returns false while it is set rather than
interpreting `ctx->pc`. After the fix the assert is gone, `DataNode::Command`
rejects nothing in 79 calls, and boot reaches the VIF1 render loop.

Reading `pc` to infer what a guest call did is unsound in general: any frame in
the unwind can alias a resume point. Other sites that guess at control flow from
`ctx->pc` are worth auditing on the same grounds.
