# Audio path

Where GH2's sound goes in the port. As of 2026-09-21 songs, menu music and
sample SFX play through `ps2x::iop::synth` (`ps2xIOP/src/modules/synth_engine.cpp`)
and out of an SDL3 device. The hardware IRX that does this job on a PS2 is
not loaded; the engine stands in for it.

## EE to IOP

GH2's audio sits behind `SynthEE`, two SIF RPC sids:

- `0x75433178`, EE to IOP. `CtlClientCall` appends packed
  `{cmd:u32, len:u32, data[len]}` records to a buffer at 0x4FA6B0 (cursor
  0x444D70). `CtlClientPoll` 0x269bd0 flushes the queue with one
  `sceSifCallRpc(rpc=0, NOWAIT)` and no receive buffer, **and only when the
  queue is non-empty**.
- `0x75433179`, IOP to EE, the EE's own RPC server: `CtlDispatch__7SynthEE`
  0x268760 `(cmd, data, len)`, into `CtlDispatch_impl` 0x3eb828, jump table
  0x4EB680, entries 0..14.

## Commands, EE to IOP

Every number read off the `addiu $a0, $zero, N` ahead of the `CtlClientCall`
in the named body (`ctlscan.py`-style scan over `ps2xRuntime/src/runner`):

    0x0    SynthEE ctor         SynthConfig            len 24
    0x1    Terminate
    0x3-4  SetFX*               sceSdEffectAttr        reverb, not modelled
    0x5    SetFXChain           0x6 SetInputFX
    0xc8   SPUSendPoll          {spu dest}
    0xc9   SPUSendPoll          raw bytes, <= 0x5000
    0x133-136  mic volume, suspend, resume, FX
    0x190  StreamEE::Poll       StreamInfoArg {id, short nch, freq[15], fx[15]}
    0x191  UpdateFXCore         0x196 UpdateFX, 0x19f UpdateADSR: not modelled
    0x192  Play {id}            0x193 Stop {id}      0x194 Destroy {id}
    0x195  UpdateVolumePan      {id, short ch, short vol, short pan}, packed
    0x197  UpdateSpeed          {id, short speed*1000}
    0x198  SendToIOP            {id, short ch}, then
    0x199  SendToIOP            that channel's raw PS-ADPCM, <= 0x1000, then
    0x19a  SendToIOP            {id}, once its request queue is drained
    0x19b-19d  slip offset/stop/speed      0x19e stereo pair {id, a, b}
    0x258  EnableLevels
    0x2bc  SampleInstEE ctor    {handle, GetSPUAddr, GetSampleRate}
    0x2bd  dtor  0x2be Start  0x2bf Stop  0x2c0 Pause {handle, bool}
    0x2c1  UpdateVolPan {handle, short vol, short pan}
    0x2c2  speed*1000   0x2c3 ADSR   0x2c4 FX core   0x2c5 start offset

Volume is `DbToRatio(fader) * 32767`; pan is `VolumePan::SetFloatPan`'s
signed count of 1/127ths, past +-127 into the rear positions.

## Replies, IOP to EE

`CtlDispatch_impl`'s table, read from the disassembly:

    0   StreamEE::DispatchStreamData({id, ch, bytes})   the IOP asks for data
    1,2 StreamEE::Dispatch(id, op, 0)                   2 = ready (state 3)
    3   StreamEE::Dispatch(id, 3, pos)                  play position
    4   DispatchSlipOffset       5-8, 10, 12  mics, levels, HUD text
    9   SampleInstEE::UpdateAddr(handle, addr)
    11  SampleInstEE::SetDone(handle)                    one-shot finished
    13  SPUSetSendDone           14  terminated

**Reply 3 is the audio sync.** `StreamEE::Poll` turns `pos * 28` samples into
ms against channel 0's rate, adds the start offset, and snaps the stream
timer if it is more than 100 ms out. Song time is otherwise the EE-side
`VarTimer`, which is why the chart always played without audio.
`GHPC_SYNTH_SYNC=0` stops the reports.

## How a stream flows

1. EE sends 0x190. The service replies 2 (ready) and the engine creates the
   stream.
2. Every `SynthEE::Poll` the engine asks for data, reply 0 in 0x1000 steps,
   until each channel holds 0x8000 bytes of ADPCM (about 1.8 s at 32 kHz).
3. `StreamEE::PollAll` answers in `SendToIOP` with 0x198 + 0x199 per channel
   and closes with 0x19a. A request for a channel past the reader's own count
   is skipped silently; a channel that stays unanswered while its siblings get
   data is marked dead after three batches.
4. 0x192 starts it. The mixer waits until every live channel holds 64 blocks,
   then consumes all channels together; an underrun stalls the stream rather
   than skewing channels.

## How a sample flows

`SPUSendPoll` uploads ADPCM into SPU RAM (0xC8 destination, 0xC9 bytes, one
ack each, reply 13). 0x2bc names an instance by handle, address and rate;
0x2be plays it as an SPU2 voice would, walking SPU RAM with the block flags
(0x04 loop start, 0x01 loop end, 0x02 repeat). A one-shot that runs off its
last block sends reply 11. No ADSR: a stop cuts.

## Reverb

Two SPU2 reverb units, one per core, the PS1 algorithm and presets from
psx-spx ("SPU Reverb Formula", "SPU Reverb Examples"), run at 24 kHz with
the output interpolated to 48. The game sets them up once at boot:

    core 0  mode 7 ECHO       depth 0x2d6a (0.355), delay 0x40
    core 1  mode 4 STUDIO_C   depth 0x301b (0.376)
    chain   off (0x5 = 0)

**Only Star Power uses it.** `PlayerMatcher::OnStartStarpower` calls
`SetReverb(true)`, which is `SetFX(kFXCore1, on)` on the guitar track; the
guitar channels already sit on core 1 (0x191) with the send off (0x196).
Crowd, SFX and the song are dry on hardware as well: every sample gets core
-1 (0x2c4) and every other channel's send stays off. Core 0 is presumably
the mic's echo (0x6 InputFX), unmodelled.

Checked offline: a click through STUDIO_C at the game's depth decays about
29 dB/s, and the same click with the send off is silent after it.
libsd's delay and feedback tweaks for ECHO and DELAY are not applied, and
PIPE is assumed to be psx-spx's Half Echo.

## Menu music start

`MetaMusic` fades its stream in on a `LinearInterpolator` keyed on
`TaskMgr::UISeconds`, and `UIManager::Poll` zeroes that clock on every screen
change. The port's instant disc reads let the fade start just before the
first screen change, so it waited until the UI clock climbed back to where
it had been: the UI time spent on App::App's 3000 ms boot splashes, about
6.3 s every boot. `ghpc/override/Poll__9MetaMusic_0x258e88.cpp` shifts the
interpolator by however far the clock jumps back. The music now comes up
under `bootup_load` and 0.6 s into `main_screen` with boot skip.

## Delivering replies

Only one guest call fits in an RPC result, and `CtlClientPoll` flushes only
when it has something to send, so the IOP cannot rely on a flush to talk.
`ghpc/override/Poll__7SynthEE_0x2685a8.cpp` calls
`ghpc::synth::deliverReplies` on fresh entry: queued replies are copied into
a guest ring and run as one `invokeCurrentSequence` of `CtlDispatch` calls,
then Poll re-enters. Poll goes on to `StreamEE::PollAll` and `CtlClientPoll`,
so a request raised this frame is answered this frame.

Replies earned on a flush but outranked for the RPC result are queued rather
than dropped. Dropping one was never harmless: a lost SPU ack leaves
`gSpuInFlight` set and nothing retries it.

## Measured, 2026-09-21

Release, `--native --ingame`, 75 s, into `lose_screen`:

    streams=11 bytesIn=4610720 underruns=0 samples=25 starts=25
    [ghpc/clock] rate=1.000 uirate=1.000 screen=game_screen

WAV tap RMS 3-8k during the song, 27 clipped samples in the hottest second.
Confirmed by ear on the song.

The old "chunks=1" result was the log, not the upload:
`kSpuChunkLogEvery=32` hid chunks 2 through 31. With them printed, every
`SPUStartSend` ships its chunks, the ack rides every flush, and `pending`
clears at the end of each sample. Neither candidate from the earlier note
held.

What a song mixes, from `GHPC_SYNTH_DUMP` on `--ingame`:

    song stream   5 ch: song L/R 32250 Hz, guitar L/R 32250 Hz (vol 0 while
                  notes are missed), one mono 16125 Hz channel
    crowd loops   2 ch 22050 Hz streams from CrowdAudio, a new one about
                  every 10 s, vol 0.24-0.28
    crowd one-offs  22050 Hz voices at random speed 0.84-1.06

Crowd alone measures RMS ~550 against the song's ~6600. The volume is the
game's own: `CrowdAudio::UpdateVolume` sets `base + master + excitement + FX`
dB on the stream, and a failing run sits at low excitement.

## Knobs and lines

    GHPC_AUDIO=0          no output device
    GHPC_AUDIO_WAV=path   16-bit stereo 48 kHz tap of the mix
    GHPC_SYNTH_SYNC=0     no position reports (reply 3)
    GHPC_SPU_ACK=0        leave SPU chunks unanswered, the old control arm
    GHPC_SYNTH_SOLO=x     song | crowd | voices: mix only that part. Soloing
                          drops voice completion replies, diagnostic only
    GHPC_SYNTH_DUMP=1     reverb modes, every stream channel's vol, pan,
                          buffer and FX send, and every sounding voice, with
                          each [ghpc/audio] line
    GHPC_SYNTH_TRACE=1    every non-bulk synth record with its payload and
                          seconds since start; =2 adds volume updates
    GHPC_SYNTH_REVERB_ALL=n  send every stream channel to core n, to hear a
                          reverb without earning Star Power
    [ghpc/audio]          every 300 polls: streams, bytes, underruns, voices
    [ghpc/spu2]           the first 64 SPU chunks, then every 32nd

Output is SDL3 only, so it needs `PS2X_ENABLE_VULKAN`; the raylib window's
own audio device is unused. Position reports stop while nothing pulls
samples, so a run with no device cannot pin the chart at zero.

## Not done

- ADSR envelopes, mics and core 0's echo, slip streaming (0x19b-d), levels
  (0x258), sample progress (reply 9).
- The intro movie: `\VIDEOS\INTRO.PSS` is not staged in `work/` (only the
  ARK, the ELF and the IOP modules are), so `--intro` stops on
  `cut_scene_screen`. Whether PSS playback works once it is staged is
  untested.
- Save states: the engine is host state the SYNTH chunk does not carry, so a
  resumed song has no audio until its streams are prepared again.
- Prepared streams that never play keep asking for data once the EE buffer
  runs dry (about one unanswered request a frame). Harmless, not tidy.
