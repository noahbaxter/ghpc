# M4 fileio service, in progress

Goal: answer SID 0x80000001 so the game can read the ARK.

## Approach

PS2Recomp does not emulate the IOP CPU or load IRX binaries. It high-level
emulates services per game. Three profiles existed (RE Code Veronica X, LOTR Two
Towers, Fatal Frame); GH2 now has a fourth.

`patches/0004-fileio-service.patch` adds
`ps2xIOP/src/modules/fileio.cpp`, registers `createFileioService`, and adds a
`guitar-hero-ps2` profile matched on entry point `0x00100D68`. GH2 and 80s debug
builds share that entry point.

## Protocol

Read out of the game's own statically linked libfileio rather than inferred from
traffic. The ELF is symbolized, so every call site is legible. All addresses are
in the GH2 debug build.

Each entry point fills one packet at `0x005b4dc0` and passes it to
`sceFsSifCallRpc` (`0x0034f248`), which is a retry wrapper over `sceSifCallRpc`.
The function number is `$a1`, the send size `$a3`.

| entry point | address | fn | send size |
|---|---|---|---|
| `sceOpen` | 0x0034fb10 | 0x00 | 0x418 |
| `sceClose` | 0x0034fda0 | 0x01 | 0x14 |
| `sceRead` | 0x00350158 | 0x02 | 0x20 |
| `sceWrite` | 0x003503c8 | 0x03 | 0x30 |
| `sceLseek` | 0x0034ff18 | 0x04 | 0x1c |
| `sceIoctl` | 0x00350688 | 0x05 | 0x420 |
| `sceMkdir` | 0x00350a18 | 0x07 | strlen+0x11 |
| `sceGetstat` | 0x00350bf8 | 0x0c | strlen+0x11 |
| `sceLseek64` | 0x00350dd0 | 0x16 | 0x20 |

This is ps2sdk's numbering after all. The earlier reading of this file claimed
otherwise because it misidentified the header.

Header, common to every function, from `sceGetstat+0xf4..0x100`:

| offset | bytes | meaning |
|---|---|---|
| 0x00 | 4 | EE semaphore to signal on completion |
| 0x04 | 4 | EE address the `int` result is written to |
| 0x08 | 4 | result size, always 4 |

Payloads from 0x0c:

- open: flags `& 0x7fffffff` at 0x0c, fifth argument mode at 0x10, path at 0x14
  (0x400 bytes), iob index at 0x414
- close: descriptor at 0x0c, iob index at 0x10
- read and write: descriptor at 0x0c, guest buffer at 0x10, length at 0x14
- lseek: descriptor at 0x0c, offset at 0x10, whence at 0x14
- lseek64: descriptor at 0x0c, 64 bit offset at 0x10, whence at 0x18
- getstat: `sceStat` pointer at 0x0c, path at 0x10

Sizes confirm it: getstat sends `strlen + 0x11`, so 39 bytes for the 22
character `cdrom0:\GEN\MAIN.HDR;1`.

## Completion

`sceGetstat+0x150` is the part that matters, and every other entry point ends
the same way.

The four byte reply buffer is only an accepted flag. Zero means the IOP never
took the request, and the caller returns -11 immediately without waiting. That
was the retry loop: the instrumented service zeroed the reply, so no call could
ever get past the check.

A non-zero reply arms a `WaitSema` on the semaphore named in the header. The
actual result is not in the reply buffer at all, it is written to the EE address
in the header. On hardware the IOP delivers both through a SIF command handled
by `_sceFs_Rcv_Intr` (`0x0034f3a8`).

Emulating it synchronously works: write a non-zero reply, write the result to
the header address, signal the semaphore. The semaphore starts at count 0 with
max 1, so signalling before the guest reaches `WaitSema` leaves the count at 1
and the wait falls straight through.

The runtime could not signal that semaphore. `signalRpcCompletionSema` in
`Kernel/Syscalls/RPC.cpp` only knows the SIF client's own `hdr.sema_id`, not one
carried inside a packet payload. `IopHost` gained `signalGuestSemaphore`, backed
by `eeScheduler().signalSemaphore`.

## Version handshake

`sceFsInit+0x154` issues fn 0xff with an 8 byte payload of two EE receive-buffer
pointers and reads an 8 byte reply: an interface version word, then a flag the
library stores as `_fs_rcv_bufdbl` when it equals 2.

`_fs_version` (`0x0034fa48`) memcmps that version against the `"3000"` field of
`__ps2_klibinfo__` (`0x00447420`, `PsIIlibkernl3000`) and against the `"...."`
wildcard at `*_fswildcard`. It issues no RPC of its own, which is why the first
reading of this file could not place it.

`sceOpen+0x74` calls it and returns `0xfffefffc` before sending anything if
neither matches. `sceGetstat` has no such check. That asymmetry is exactly what
was observed once the denylist was widened: getstat reached the service and open
failed silently without ever building a packet. The service now answers fn 0xff
with `"3000"`.

## Result

```
[FILEIO] fn=0xff status=0     version="3000"
[FILEIO] fn=0x0  status=3     path="cdrom0:\GEN\MAIN.HDR;1"
[FILEIO] fn=0x1  status=0     fd=3
[FILEIO] fn=0xc  status=0     path="cdrom0:\GEN\MAIN.HDR;1"
[FILEIO] fn=0x0  status=4     path="cdrom0:\GEN\MAIN.HDR;1"
[FILEIO] fn=0x2  status=16384 fd=4
```

The game opens the ARK header, closes it, stats it, reopens it and reads 16 KB.
Open, close, read and getstat all work end to end, so M4's goal is met: a service
answers SID 0x80000001 and the game reads real data off the disc image.

Write, ioctl and mkdir return -EINVAL. The disc is read-only, and a fabricated
success would be acted on.

## Next

Execution stalls after the first 16384 byte read. No second read is attempted,
and the log cap is 8 per function, so this is a stall and not a truncated log.
The EE thread is in `Timer::Sleep` with the scheduler still servicing VSync, so
it is waiting rather than deadlocked or crashed.

The likely cause is the asynchronous path in `sceRead`. `sceRead+0xb0` tests
`iob->flags & 0x8000` (SCE_NOWAIT) and, when set, enqueues the request semaphore
into `_sceFs_q` (`0x00447448`) instead of waiting inline. Completion is then
delivered by `_sceFs_Rcv_Intr` (`0x0034f3a8`), which dequeues and signals. The
service currently signals synchronously and never touches that queue, so a
NOWAIT read would be reported complete without the queue entry ever being
retired. Confirm which flags the game opened the ARK with before changing
anything.
