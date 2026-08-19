# M4 fileio service, in progress

Goal: answer SID 0x80000001 so the game can read the ARK.

## Approach

PS2Recomp does not emulate the IOP CPU or load IRX binaries. It high-level
emulates services per game. Three profiles existed (RE Code Veronica X, LOTR Two
Towers, Fatal Frame); GH2 now has a fourth.

`patches/0004-instrumented-fileio-service.patch` adds
`ps2xIOP/src/modules/fileio.cpp`, registers `createFileioService`, and adds a
`guitar-hero-ps2` profile matched on entry point `0x00100D68`. GH2 and 80s debug
builds share that entry point.

The service logs function number, buffer addresses and the send payload as hex
and ASCII, capped at 6 logs per function and 64 payload bytes. It acknowledges
each call and zeroes the receive buffer. It does not return success codes, since
the protocol was not established at the time of writing.

ps2sdk's fileio function table was consulted and does not match what GH2 sends.
ps2sdk is a homebrew reimplementation; GH2 links Sony's official SDK.

## Observed protocol

```
fn=0xff  send 8   recv 8   two guest pointers
fn=0x0c  send 39  recv 4   header + "cdrom0:\GEN\MAIN.HDR;1"
fn=0x0c  send 24  recv 4   header + "host0:"
```

Request layout for fn=0x0c:

| offset | bytes | value seen | note |
|---|---|---|---|
| 0 | 4 | 0x06, 0x07 | command or mode selector, differs per call |
| 4 | 4 | 0x01F7F000, 0x01F7D480 | guest pointer |
| 8 | 4 | 0x04 | constant across both calls |
| 12 | 4 | 0x01F7F0A0, 0x01F7D520 | guest pointer |
| 16 | n | path | null-terminated, inline |

Sizes confirm the layout: 39 = 16 + 23 for `cdrom0:\GEN\MAIN.HDR;1` plus
terminator, 24 = 16 + 8 for `host0:`.

Reply buffer is 4 bytes for fn=0x0c, consistent with a single int result.

`fn=0xff` carries no path and is called first, consistent with an init
handshake.

## Unresolved

- Meaning of the leading command word. 6 and 7 do not correspond to ps2sdk's
  6=ioctl and 7=remove for a call carrying a path to open.
- Whether fn=0x0c is open specifically or a general request wrapper.
- Purpose of the two guest pointers in the header.
- Meaning of the constant 0x04 at offset 8.

## Next

Implement fn=0x0c as open: parse the inline path, translate `cdrom0:\GEN\MAIN.HDR;1`
to `work/GEN/MAIN.HDR` (strip device prefix, convert backslashes, drop the `;1`
version suffix), open via `IopHost::openHostFile`, return a descriptor in the
4-byte reply. Then run and observe the next call to identify read, lseek and
close.

`IopHost` provides `translateGuestPath`, `openHostFile`, `hostFileSize`,
`readHostFile`, `closeHostFile`, `readGuest`, `writeGuest`, `zeroGuest`.

Each iteration is one runtime relink, roughly 3 minutes.
