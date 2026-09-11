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
