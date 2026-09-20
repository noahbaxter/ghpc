# Audio path

Where GH2's sound goes in the port, and where it stops. Mapped 2026-09-11
from the runtime source and the 2026-09-10 logs in `work/`. Nothing here
has been fixed yet; this is the map for whoever does.

## EE to IOP

GH2's audio sits behind `SynthEE`, two SIF RPC sids recorded at
`ps2xIOP/src/modules/synth.cpp:19-32`:

- `0x75433178`, EE to IOP. `CtlClientCall` appends packed
  `{cmd:u32, len:u32, data[len]}` records to a buffer at 0x4FA6B0 (cursor
  0x444D70). `CtlClientPoll` flushes the queue with one
  `sceSifCallRpc(rpc=0, NOWAIT)` and no receive buffer, so replies cannot
  come back that way.
- `0x75433179`, IOP to EE, the EE's own RPC server: `CtlDispatch__7SynthEE`
  at 0x268760, jump table 0x4EB680, entries 0..14.

The SPU send handshake (`notes/ui-draw-hang.md:44-63`): `SPUStartSend`
0x26e3f0 sets `gSpuPending` (0x444d90); `SPUSendPoll` 0x26e4c8 ships one
chunk of at most 0x5000 bytes and sets `gSpuInFlight` (0x444d94);
`SPUSetSendDone` 0x26e4b8 is the only clear and its only caller is
`CtlDispatch` message 13 arriving over 0x75433179.

## What the runtime has

- No SPU2 at all. No SPU RAM, no voice registers, no ADPCM decode of
  uploaded data, no key-on. Grep for SPU2, 0x1f900, KON over
  `ps2xRuntime/src` and `ps2xIOP/src` finds nothing.
- The IOP is HLE keyed on sid. The GH2 profile registers `fileio`, `usbkb`,
  `SYNTH` (`ps2xIOP/src/builtin_profiles.cpp:117-129`); `libsd`
  (0x80000701) is added for every game. `SYNTH` handles two commands, 1
  (Terminate, reply 14) and 0x190 (StreamInfo, reply 2); everything else
  goes to `noteUnknown` and is dropped (`synth.cpp:36-38, 166-171`). It
  never sends reply 13.
- A host output device exists: raylib `InitAudioDevice` at
  `ps2_runtime.cpp:885`, fed by `PS2AudioBackend` (`ps2_audio.cpp`), which
  decodes VAG to PCM and calls `PlaySound` (`ps2_audio.cpp:306`). Its two
  feeds are the `libsd` RPC (never used by GH2: zero 0x80000701 lines in a
  2.6 GB log) and a `VAGp` sniffer on `fioClose`
  (`Kernel/Syscalls/FileIO.cpp:145-158`) with no log evidence it fires.
- EE-side `sceSdRemote` stubs are bookkeeping only
  (`Kernel/Stubs/Audio.cpp:102-168`); nothing is forwarded.

## The SPU upload protocol, read off the ELF

2026-09-19. Both commands come from `SPUSendPoll` 0x26e4c8 rather than from
the 0x5000 length, which is what the earlier note inferred.

`SPUStartSend(src, len, dst)` 0x26e3f0 stores the EE source at 0x444D90, the
byte count at 0x51A9A0 and the SPU RAM destination at 0x51A99C.

`SPUSendPoll` 0x26e4c8 returns early unless 0x444D90 is set and 0x444D94
(`gSpuInFlight`) is clear. Otherwise it sets inflight, takes
`min(0x5000, remaining)` and issues two records:

    CtlClientCall<int>(0xC8, *(int*)0x51A99C)   the SPU RAM destination
    CtlClientCall(0xC9, src, chunkLen)          the bytes

then advances src and dst by the chunk and subtracts it from remaining,
clearing 0x444D90 when remaining hits zero. **The advance is unconditional**,
so the acknowledgement paces the upload and never gates the data.

`SPUSetSendDone` 0x26e4b8 is two instructions, `sw $zero, 0x4D94($v0)`, and
it is reached only from `CtlDispatch_impl` reply **13**. Confirmed by reading
the jump table at 0x4EB680 (.rodata, file offset 0x3EC680) rather than
inferring it: entry 13 is 0x3eba00, which is the `jal func_26E4B8`.

## The ack is implemented, and it is not sufficient

`synth.cpp` now handles 0xC8 (keeps the destination) and 0xC9 (counts, then
replies 13). Control arm on one binary is `GHPC_SPU_ACK=0`.

Release, `GHPC_PAD_DRIVE=cross GHPC_COUNTIN=0.5`, reaching `game_screen` at
t=25.3: **`chunks=1 bytes=20480` in both arms.** 20480 is exactly 0x5000, the
per-chunk cap, so a single chunk either completed a 0x5000-byte transfer or
the send never restarted. Acknowledging the chunk did not make a second one
ship.

Two candidates, and they are distinguishable:

1. **The ack is being dropped before it is sent.** Only one guest call fits
   in an RPC result, and the dispatch ranks StreamInfo above the SPU ack. The
   observed record order in one flush is 0xC8, 0xC9, then `StreamInfoArg`,
   so if those share a flush the SPU ack is exactly what gets discarded.
   Test: count dispatched replies by rank, or rank the SPU ack highest for
   one run and see whether `chunks` moves.
2. **The game only ever calls `SPUStartSend` once** at this point, so there
   is no second transfer to pace. Test: a counter on 0x26e3f0.

Do not conclude between them from the current run. It distinguishes neither.

**Measurement defect, introduced with the fix.** `kSpuChunkLogEvery` is 32,
so chunks 2 through 31 print nothing. The count is still visible as the
`spu_chunks` debug metric, but the log line alone cannot tell 1 from 31.
Lower it before the next measurement.

## The rest of the command surface, measured

Release, one run to `game_screen`, the `[SYNTH:unhandled]` census (capped at
16 lines, so this is the set seen early, not the whole census):

    0x0    len 24     0x3    len 20     0x4    len 20     0x5    len 4
    0x133  len 4      0x136  len 4      0x191  len 8      0x195  len 12
    0x197  len 8      0x258  len 4

The `CtlClientCall` template instantiations in `work/output/` name the
argument types and are the way to map these: `CreateSampleArg`,
`SampleAddrArg`, `SampleADSRArg`, `SampleVolPanArg`, `SampleSpeedArg`,
`StreamInfoArg`, `StreamDataInfoArg`, `StreamSlipOffsetArg` and the rest.
0x3 and 0x4 at len 20 repeating in pairs look like per-voice setup.

## Where the evidence stops

From `work/r6.log` and `work/r6long.log` (2026-09-10):

    [ps2xIOP] [SYNTH:unhandled] cmd=0xc9 len=20480    once
    [ghpc/spu] pending=0 inflight=1                   51 lines
    [ghpc/spu] pending=26802576 inflight=1            14 lines

20480 is 0x5000, `SPUSendPoll`'s maximum chunk. One sample chunk reaches
the IOP `SYNTH` service and is discarded. No reply 13 is sent, so
`gSpuInFlight` latches at 1 and no second chunk ships. The unknown-command
log is capped at 16 lines, so a second 0xc9 could not have printed anyway;
that cap is why audio was unmeasured.

Whether 0xc9 is the sample command is inferred from the length, not read
from `SPUSendPoll`'s disassembly. Confirm at 0x26e4c8 before building on it.

## Metric

`[ghpc/spu2] chunks=N bytes=B records=R`, printed from `synth.cpp` on every
0xc9 record, uncapped. `progress.py` reads `spu_chunks` and `spu_bytes` and
scores `spu_chunks` as a span like `song_tick`. It is the literal record
header the EE wrote, counted where it arrives, upstream of everything
unimplemented, so a fix cannot fake it. Baseline on the current build is
`chunks=1 bytes=20480`, flat. Anything that acknowledges the chunk must make
it climb; the control arm is the same build with the acknowledgement off.

Not the metric: a counter on `PlaySound`. That path is never reached in a
GH2 run and would read 0 forever, which is a floor, not a signal.

## Knobs

- `GHPC_SYNTH_ACK`: `Kernel/Syscalls/RPC.cpp:660-679` pokes `gSpuInFlight`
  to 0 on an unhandled call to 0x75433178. Gated on `!handled`, and the
  SYNTH service now marks every call handled, so the probe is probably
  dead. Unverified.
- `[ghpc/spu] pending= inflight=` is the debug-build stall census
  (`EeScheduler.cpp:247`), not a steady counter.
- `SYNTH` keeps `terminate_requests`, `stream_info_requests`,
  `replies_dispatched`, `records_left_unanswered` (`synth.cpp:181-189`), but
  only the ImGui panel reads them.
