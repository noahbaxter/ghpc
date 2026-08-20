# GH2 boot sequence reference

What the retail PS2 game shows, in order, versus what the recompiled build
currently produces. Used to tell "wrong pixels" apart from "wrong sequence".

Reference stills of the real game are in `reference/` at the repo root, in boot
order (keep them local, do not commit disc rips):

    1_redoctane.png  2_activision.png  3_harmonix.png
    4_loadingsquash.png  5_loadingstretch.png
    6_introvid_1.png  7_introvid_2.png

Images 1-3 are cropped to the drawn pixels, so their aspect ratios are uneven.

| # | Screen | Source | Real appearance | ghpc status |
|---|--------|--------|-----------------|-------------|
| 1 | RedOctane logo | `ui/splash.dtb` | Flame logo, "redoctane", black field | **correct** |
| 2 | Activision logo | splash | White "ACTIVISION" wordmark on black | green fill |
| 3 | Harmonix logo | splash | Chrome "HARMONIX" with blue orb on black | green/blue garbage |
| 4 | Memory-card LOADING | `ui/mem_card.dtb` (`bootup_load`) | Red/cream poster art, brick wall bg, "LOADING... Checking status of memory card" | green/blue garbage, and the sequence STALLS here |
| 5 | Intro cutscene | movie | Line-art hand holding a blue pick | not reached |
| 6 | Main menu | `ui/main.dtb` | - | not reached |

## Why 2-4 look wrong

Not the IPU. The game never drives it: zero IPU DMA channel starts and zero
game-originated IPU register writes over 90 s. The full-screen `(0, 128, 0)`
green matching YCbCr->RGB of all-zero input is a coincidence, not evidence.

Nor is it a flat fill. Frame dumps report `otherColors=199876` over 229376
pixels, so the buffer is high-entropy garbage that only reads as flat blocks at
a glance. Dumped frames show a vertical stripe pattern with a period of exactly
8 pixels, alternating between two colours, phase-aligned to x=0 and spanning
every region a textured draw covers.

The texture bindings themselves are correct. Uploads and the TEX0 in effect
line up exactly: a 512x256 PSMT4 upload at DBP 6432 is sampled by 13421 draws
with TBP0 6432, TBW 8, PSM 0x14, TW 9, TH 8, and the 32x32 PSMT4 at 6956 and
the PSMT8 pages at 5408/5664/5920/6976 match the same way. No upload is dropped
for an unhandled DPSM.

What is wrong is that a large share of TEX0 writes carry garbage: TBP0 of 0, 1,
3, 4, 5, 7 with TBW 0, TW 0, TH 0 and PSMs like 0x04, 0x08, 0x3f that are not
valid pixel-storage formats. Attributing single framebuffer pixels to their
last writer shows one full-screen sprite (PRIM 6, TME 1) with
`tex0(tbp0=3, tbw=0, psm=0x0, tw=0, th=0)` painting a constant colour over
40 of 48 sampled pixels. A TEX0 of literal `3` is a wrong operand, not a
degenerate texture the game asked for.

Those garbage operands come from a desynced VIF1 stream. The unrecognised VIF
commands are float vertex data read as commands: `0x3f800000` (1.0f),
`0x3fc00000` (1.5f), `0x41680000` (14.5f), arriving at a regular 8-byte
spacing because the interleaved zero words parse as NOP. About 0.9% of all VIF
commands are invalid opcodes, and each desync corrupts a whole packet.

Tracing the same 2464-byte packet across runs shows why. Bytes 0x0 to 0x130
are identical every time and parse cleanly: groups of `NOP, NOP, FLUSHE,
UNPACK/DIRECT` on quadword boundaries. From 0x130 on, the content *differs
between runs at the same offsets*. In a clean run 0x13c holds `0x6c0102a8`
(UNPACK V4-32, num=1); in a desynced run it holds zero, and at 0x164 a bogus
`0xffff0000` is accepted as UNPACK V4-5 num=255, consuming 516 bytes and
landing at 0x368 in the middle of float vertex data.

Same packet, same offsets, different bytes, so this is stale data, not a parser
fault. The chain flattener is reading MFIFO ring memory the EE has not written
yet. The bound in `ps2_memory.cpp` is

    if (mfifoDrain && tagAddr == mfifoFill)
        break;

which fails two ways. It is an exact-equality test, so a tag whose QWC steps
`tagAddr` over the fill pointer never matches and the walk continues into
unwritten ring. And it bounds only the tag walk: a payload range
(`dataAddr .. dataAddr + qwc*16`) is never checked against the fill pointer at
all, so even a tag located before the fill can pull payload from beyond it.

Measured over 45 s:

    MFIFO tagsChecked=3403 tagPastFill=166
           payloadPastFill=56 payloadBytesPastFill=16225456
    chunks=1805 dirtyChunks=35 bySource chain=35/1804 dirtyBytes=17148752

16.2 MB read past the fill pointer against 17.1 MB of bytes sitting in chunks
that contain a desync, and every dirty chunk comes from the flattened-chain
feed. Stale ring content is the previous wrap's vertex data, which is why the
garbage parses as floats.

A second, smaller defect is real but not the main driver: a command whose
payload overruns the end of a chunk is discarded with no residual carried to
the next `processVIF1Data` call, so the next chunk starts mid-payload. That
fires 25 times per 45 s and accounts for 19 of the 35 dirty chunks (`DIRECT`
sets `pos = sizeBytes; break`, UNPACK breaks on `pos > sizeBytes`).

## Second MFIFO bug: the end tag never advanced TADR (patch 0019)

Compare the tag cases in the chain walker:

    case 0 (refe): tagAddr = tagAddr + 16; endChain = true;   // advances
    case 7 (end):                          endChain = true;   // does not

So every MFIFO drain that reached an `end` tag left TADR sitting on that tag.
The next drain re-read the same tag, ended immediately, and consumed nothing,
so the ring never drained. `case 6` (ret) had the same hole on its
chain-ending branch. Both now advance past the tag and its payload.

This was what made the ring bounds look destructive. With bounds on but the end
tag not advancing, the drain wedged and the GS starved:

    uploads h2l    7  -> 25
    tme draws      4  -> 1099
    notme draws  256  -> 24798

with dirtyBytes staying at 1.09 MB. Both properties at once: clean stream and
full data flow.

## State after 0019

RedOctane renders correctly (`reference/1_redoctane.png`), 9639 non-black
pixels over 9491 distinct colours.

The GS texture path is proven correct end to end. The CLUT resolves 256
distinct indices to 256 distinct colours (`cbp=5376 cpsm=0x0 csm=0 csa=0`),
index 0 mapping to `0x80000000` as a transparent background should. Uploads,
TEX0 binding, the CT16 framebuffer read and the present path all round-trip:
a swizzled read and a raw read of fbp 56 agree exactly.

## VU1 was never broken: garbage MSCALs (patch 0022)

The "family of programs at 0x34xx-0x3fxx that never produce a visible vertex"
does not exist. Every one of those addresses is the masked form of an
out-of-range MSCAL target:

    0x7f420 & 0x3FFF = 0x3420      0x7fc58 & 0x3FFF = 0x3c58
    0x7fac8 & 0x3FFF = 0x3ac8      0x7fd28 & 0x3FFF = 0x3d28
    0x7fb20 & 0x3FFF = 0x3b20      0x7fd88 & 0x3FFF = 0x3d88
    0x7f5f0 & 0x3FFF = 0x35f0      0x7fba0 & 0x3FFF = 0x3ba0

`startPC = imm * 8`, so those come from `imm` near 65535. VU1 micro memory holds
2048 instruction pairs, so a real MSCAL always has `imm < 2048`. These are fake
VIFcodes decoded from the residual desynced stream, which `microAddressMask()`
folded back into range and executed as whatever happened to live there. What
gave it away: the per-program UNPACK feed tally listed the 0x7fxxx addresses but
never the 0x3xxx ones, because the VIF side logs the raw target and the VU side
logs the masked one.

Patch 0022 rejects MSCAL when `startPC >= PS2_VU1_CODE_SIZE`. Result:

    fake programs           19 -> 0  (only the real 0xcd8 remains)
    MSCALs rejected                60
    PSMT4 texels sampled     0 -> 170841320
    dirtyChunks             44 -> 18
    invalidOps           21797 -> 4370

So the VU1 interpreter, its addressing, its double buffering and XGKICK
(`qwordAddress * 16`, correct) are all fine. There is no VU1 arithmetic bug.


### Patch 0022 is essential (an earlier retraction here is itself retracted)

At one point this file recorded that 0022 blocked a legitimate instruction path,
because `GHPC_ALLOW_MASKED_MSCAL=1` let one more VF2 writer execute. That test
ran while patch 0023's regression was active, so it was measured on a build
where the game barely rendered. Re-run against the healthy baseline:

    GHPC_ALLOW_MASKED_MSCAL=1 : tme=4     notme=256    broken
    default, 0022 active      : tme=17430 notme=2933   healthy

Allowing the masked MSCALs breaks boot progression outright. Those MSCALs are
garbage decoded from the desynced stream, so 0022 is required rather than a
trade-off, and the extra VF2 writer at 0x35b0 seen under the flag was a
garbage-program artifact, not a real initialiser.

This also undermines the idea that the 0x3xxx VU1 code is a legitimate overlay
that ought to run: it is reachable only from garbage MSCAL targets. Its six VF2
writers are therefore not the intended initialisers, and why VF2 is never
initialised remains genuinely open rather than explained by a missed overlay.

## Desync hunt: two hypotheses tested and rejected

**GIF tag chain scan.** `gifImageQwcFromTag` only inspected the *first* GIFtag
in a DIRECT payload and returned 0 unless it was IMAGE. A texture upload is
PACKED first (the A+D writes for BITBLTBUF/TRXPOS/TRXREG/TRXDIR) and IMAGE
second, so a trailing image spilling past the DIRECT would have been parsed as
VIFcode. The helper now walks the whole chain and returns the spill directly.
Measured: `DIRECT scanned=1181 withImageSpill=0`. The case never occurs here, so
this fixed no observed symptom. Kept because the old code was plainly wrong.

**TADR arithmetic.** Ruled out by the numbers. At the failure the chain reads

    tag@0xb11150 id=3 qwc=19  addr=0x19f4300   (ref: data outside ring, TADR +16)
    tag@0xb11160 id=1 qwc=24  addr=0x0         (cnt: TADR = 0xb11160+16+24*16)
    tag@0xb112f0 id=0 qwc=32770 addr=0x10000000  <== not a tag

0xb11160 + 0x10 + 0x180 = 0xb112f0 exactly, so the walk computes the right next
address. The ring *content* at that address is wrong: `addr=0x10000000` is the
EE I/O register base and `qwc=32770` exceeds the whole 512 KB ring.

So the chain walker is correct and the ring itself holds something other than a
DMAtag where one is due. The stall then never clears
(`STALL count=27 sameAddrRepeat=24 resumed=0`, `lastNeed=640128`), because a
640 KB payload can never become available in a 512 KB ring.

Next hypothesis to test: whether the ring is being filled correctly. The
fromSPR path (channel 8) copies qwc quadwords with
`destination = rbor | ((destination + 16) & rbsr)`; if the guest also writes the
ring by ordinary EE stores, or if a fill is short or long by a quadword, every
later packet in the ring is misaligned and this is exactly what it would look
like.

## Desync localised to the ring fill carrying too much

Fills mostly build a well-formed ring: walking DMAtags from each fill's start
tiles it exactly for 1984 of 2000 fills. The 16 ragged ones are the desync, and
they match the ~18 dirty chunks.

Dumping one ragged fill (start=0xb11160, 0x250 bytes = 37 qw) shows the packet
itself is entirely correct:

    +0x0   DMAtag id=1 (cnt) qwc=24
    +0x10  NOP NOP FLUSHE UNPACK num=4   -> 4 qw   (0x20..0x60)
    +0x60  NOP NOP FLUSHE DIRECT imm=5   -> 5 qw   (0x70..0xc0)
    +0xc0  NOP NOP FLUSHE UNPACK num=8   -> 8 qw   (0xd0..0x150)
    +0x150 UNPACK num=1 -> 1 qw
    +0x170 UNPACK num=1 -> 1 qw
                                          = 24 qw, ending exactly at 0x190

The VIF stream and the DMAtag QWC agree exactly. The problem is that the fill
carried 37 quadwords, 12 more than the 25 the packet occupies. The trailing 12
starting at 0x190 are GIF data: `1000000000008002` decodes as a valid GIFtag
(NLOOP=2, EOP=1, PACKED, NREG=1, REGS=0xE A+D), the same shape as the GIFtag at
+0x70 that sits inside the DIRECT payload. Read as a DMAtag it yields
`id=0 qwc=32770 addr=0x10000000`, which is the garbage tag the walker chokes on
and then stalls forever waiting for a 640 KB payload in a 512 KB ring.

So the chain walk, the tag arithmetic and the packet are all correct. The defect
is that the channel-8 fromSPR fill transfers more quadwords than the packet
occupies, leaving trailing data the chain then walks into.

Next: compare the ring bytes against the scratchpad source for a ragged fill. If
they match, the source buffer already held the trailing data and the fill QWC is
the guest's; if they differ, our fromSPR copy is over-copying or reading from
the wrong SADR.

### Ruled out this pass

- **Fill vs TADR misalignment.** `equal=1909 fillAhead=91 fillBehind=0` over 2000
  fills. A fill never lands behind the read pointer; "ahead" only means the
  drain is lagging, which is normal for a ring queue.
- **Impossible-tag recovery.** Tried ending the chain when a tag's in-ring
  payload exceeds the ring size, so the drain could resynchronise instead of
  stalling forever. It never fired (`impossible tags ended: 0`) and changed no
  frame, so it was removed rather than left in as speculative code.

### Where the trailing data sits

In the ragged fill the 12 trailing quadwords are themselves well formed: a
GIFtag (NLOOP=2, EOP=1, PACKED, NREG=1, REGS=0xE A+D) at +0x190, two A+D
entries at +0x1a0 and +0x1b0 (one writing GS register 0x06, TEX0_1), then the
VIF stream resumes at +0x1c0 with `NOP NOP FLUSHE UNPACK num=8`. So it is a
valid continuation that simply has no DMAtag in front of it, which is why the
chain walker reads the GIFtag as a tag and gets `qwc=32770 addr=0x10000000`.

Open question for the next pass: whether the guest's channel-8 QWC genuinely
covers all 37 quadwords, and if so where the DMAtag for the trailing 12 is
meant to come from. Comparing the scratchpad source against the ring for a
ragged fill is the way to settle it.

## The flat logos are a PSMT4 texture that reads back empty

Going straight at the visible symptom rather than the last of the desync.

The later screens bind a 32x32 PSMT4 texture at TBP0 6956 and sample it
170841320 times, yet paint flat. Splitting the CLUT stats by source PSM shows
why:

    [gs/t4] tbp0=6956 cbp=5390 cpsm=0x0 csa=0
            uvDistinct=1024 idxDistinct=1 colorDistinct=1
            topColors: 0x40000000 x170841320

`uvDistinct=1024` is exactly the whole 32x32 texture, so the texture
coordinates are perfect. Every one of those 1024 distinct coordinates returns
the same index, which maps to a single colour. That is the flat fill.

Established so far, each by measurement:

- The upload runs to completion. T4 upload write count is 164352, which is
  exactly `512*256/2 * 2 + 512*128/2 + 32*32/2` for the four T4 uploads.
- TRXPOS is `dsax=0 dsay=0 rrw=32 rrh=32 dbp=6956 dbw=2`, matching the
  sampler's `tbp0=6956 tbw=2` and the rescan's (0..31, 0..31) exactly.
- No other transfer overlaps the region. Arms near it are 6944, 6952, 6960,
  6964, 6968, 6972 and 6976, none of which cover blocks 6956-6958.
- Re-reading the 32x32 grid at sample time through the sampler's own addressing
  gives `distinctIdx=1 values: 0`.

Two measurement traps hit and corrected along the way:

- A raw VRAM dump at block 6956 read all zeros and looked like proof the upload
  was broken. It was not evidence: the PSMT4 swizzle does not place texels at
  the block's leading bytes.
- A write/read round-trip at upload time reported `roundTripOk=164352
  roundTripBad=0`, which looked like proof the write path works. It is far
  weaker than it appears, since writing 0 and reading 0 also counts as a pass.

The live discrepancy: the source image data for that transfer is
`srcBytes=560 srcNonZero=34`. A 32x32 PSMT4 texture needs exactly 512 bytes, so
48 bytes arrive beyond what is consumed, and 34 non-zero source bytes went in
while the sampled grid comes back entirely zero. Next step is to resolve that
contradiction: check the ordering between the upload and the sampled rescan,
and whether the non-zero bytes fall in the consumed 512 or the trailing 48.

### Resolved: the flat logos are the desync, not a GS bug

Logging every image chunk for that transfer, tagged by which code path fed it:

    chunk site=1 bytes=512 nonZero=0 armed=1 copied=0/1024   GIF IMAGE tag
    chunk site=3 bytes=8   nonZero=6 armed=0 copied=1024/0   HWREG write, dropped
    ... 5 more 8-byte HWREG chunks, all dropped

    totals: hwreg=6 chunks/48B   image=1 chunk/512B

512 bytes is exactly a 32x32 PSMT4 texture, so the GIF IMAGE tag is the right
delivery mechanism and is sized correctly. It simply arrives **all zeros**,
which satisfies the whole 1024-pixel transfer and leaves the texture blank. The
HWREG writes that follow carry only 48 bytes (34 of them non-zero) and are
discarded because the transfer is already exhausted, so they are incidental
rather than the missing texture.

This unifies the two threads. The flat Activision and Harmonix screens are not
a GS, CLUT or PSMT4 defect. Every part of that path is correct: uvDistinct=1024
covers the whole texture, TRXPOS matches TEX0 exactly, no transfer overlaps the
region, and the write/read round trip is exact. The texture payload simply never
arrives with real content, which is the same VIF1/DMA data-integrity failure
that produces the ragged fills and the residual desync.

So the remaining work is a single root cause, not two: find why some payload
bytes reach the ring as zeros. The 512-byte all-zero IMAGE payload is a much
better handle on it than the 0.8% ragged-fill statistic, because it is a
specific, repeatable transfer with a known correct size.

### The blank logo texture is a data problem, not a rendering one

Tracing the 512-byte payload back to its GIF packet settles it. The packet is
608 bytes: 96 bytes of setup then 512 bytes of image data, and every part of the
header is correct.

    tagLo=0x800000000008020  -> NLOOP=32 (512 bytes), EOP=1, FLG=2 (IMAGE)
    packetBytes=608, tag at offset 80, payload 96..608 = exactly 512 bytes

The A+D writes before the tag decode cleanly too: register 0x52 (TRXREG) with
RRW=0x20 RRH=0x20 for 32x32, 0x51 (TRXPOS) at 0,0, and 0x53 (TRXDIR) = 0 for
host-to-local. A desync would have corrupted the header; this header is perfect
and only the payload is blank.

Counting every image payload in a run:

    [gs/imgstat] payloads=25 allZero=1 zeroBytes=512 nonZeroBytes=570624

Exactly one payload of 25 is all-zero, and it is precisely this 512-byte
texture. The other 24 carry 570 KB of real pixels. So the upload path works and
this particular texture's source buffer in RAM was already blank when the game
sent it.

That moves the remaining work off the GS, VU1 and DMA entirely and onto the data
pipeline: why that texture's bytes were never in RAM. The CD trace is the place
to look next. It is short (17 lines) and the argument logging does not line up:

    sceCdRead a0=2483968 a1=32 a2=0x524b80
    CDRead    lba=0 n=1435392 a2=32 buf=0x524b80

`a0` (the LBA) is 2483968 while `CDRead` reports `lba=0`, and `n` reports
1435392 where 32 sectors were asked for. That may be nothing more than a
mislabelled log, since 24 of 25 textures do load, but it needs confirming before
being dismissed.

### The art is loaded. The draws bind the wrong texture.

Reporting distinct texel values at the end of every completed upload settles
what is actually in VRAM. Note a PSMT4 texture can only hold 16 distinct values,
so 16 means fully populated, not sparse:

    5408 T8 256x128  distinct=256   real art
    5408 T4 512x128  distinct=16    FULL
    6432 T4 512x256  distinct=16    FULL
    6688 T4 512x256  distinct=16    FULL
    6976 T8 256x256  distinct=122   real art
    5920 T8 256x512  distinct=241   real art
    6956 T4  32x32   distinct=1     the only blank one

Dumping textures straight out of VRAM through their CLUTs confirms it: 5408
decodes to 256 colours including the flame logo's reds and oranges, while 6956
decodes to a single black.

But the draws bind the blank one:

    (6956, T4 32x32, blank)  x8281 draws
    (6976, T8 256x256, real) x615 draws
    (6432 / 6688, T4 512x256, FULL) -> ZERO draws

So the ARK loading, the CD path, the image uploads and the GS are all fine. The
real logo textures reach VRAM intact and are simply never selected. Meanwhile
144291804 texels are still sampled at PSM 0x4, which is not a valid pixel
storage format, so TEX0 is still receiving garbage.

That returns the remaining work to the residual VIF1 desync, which was already
the last known defect, but with a far sharper success criterion than a 0.8%
ragged-fill rate: **draws should bind 6432 and 6688**. That is a direct,
binary check on whether the desync fix works.

### Workflow gotcha

`scripts/build.sh --from=build --fast --debug` does not always pick up an edit
on the first invocation. Verified: source contained a new string, the first
build reported success without it reaching the binary, and a second build
included it. Check a new probe's output actually appears before concluding a
change had no effect. Where a counter is added in the same edit as the change,
the counter printing is proof the binary was fresh.

### Bad TEX0: one garbage GIFtag amplifies into thousands of bad writes

Classifying every TEX0 write by whether its PSM is a real storage format, and
tagging each by the GIF path that delivered it:

    [gs/tex0] validPsm=31014 invalidPsm=8264
              byPath good/bad: p1=29488/8246  p2=1526/18  p3=0/0

Path 1 is VU1's XGKICK output and carries the bulk of the garbage. Path 2, the
VIF DIRECT route, is 99% clean. (An earlier reading of mine attributed all of it
to path 2; that came from sampling only the first few bad writes, which happened
to land in one early path-2 event. Both paths produce some, VU1 dominates.)

The mechanism matters more than the split. Dumping the enclosing GIFtag for a
bad TEX0 gives:

    tagLo=0x20420210040f809  nloop=30729  nreg=16  flg=0 (PACKED)

`nloop=30729` with `nreg=16` claims 491664 quadwords, about 7.8 MB. That is a
garbage tag. `processGIFPacket` then walks it, consuming everything left in the
packet as register writes until the data runs out. So the 8246 bad TEX0 writes
are not 22% of legitimate writes being corrupted. A handful of garbage tags each
blast thousands of bogus register writes out of what is really vertex data.

That reframes the severity: the residual desync is still roughly 1% of commands,
but each bad tag amplifies enormously. It also suggests a cheap containment
independent of fixing the desync, since a tag whose declared size exceeds the
bytes remaining in its packet is provably not a real tag.

### Patch 0023 was a regression, now removed

The oversized-GIFtag guard rejected any PACKED or REGLIST tag declaring more
data than remained in its packet. It was reported here as a win because Path 2
bad TEX0 went 18 to 0. That was true and irrelevant: it also broke boot
progression outright.

    with guard:     tme=4     notme=256
    without guard:  tme=17430 notme=2933

Textured draws dropped from 17430 to 4. The guard is rejecting legitimate
packets, and the "improvement" in bad TEX0 counts was simply the game no longer
rendering. The guard has been removed and patch 0023 deleted; the baseline is
restored and verified at tme=17430.

### Re-verification result: the VU1 findings survive

Re-run against the restored baseline, the VU1 measurements are unchanged:

    tme=17430 notme=2933                      healthy progression
    XGKICK: vi1=174824/0  vi11=0/9176
    VF2 writers static=7 executed=1
      NEVER executed: 0x3048 0x35b0 0x3738 0x3740 0x3750 0x3758
    vf2 lanes nonzero x=0 y=0 z=0 w=0

So the concern that the whole trail was an artifact of the regression was itself
wrong. VI11 is genuinely always zero and VF2 genuinely never written, even with
the game progressing fully. The trail stands.

### The branch before the bad kick is not the gate

The kick at 0x11a0 sits inside the main program (0xcd8 + 0x800 covers it), and
0x1198 immediately before it decodes as IBEQ. Measured:

    0x1198 IBEQ vi3=59 vi12=59 | equal(branch)=4277 notEqual(fallthrough)=4723

It branches about 48% of the time and falls through about 52%, with the operands
equal on the taken cases. That is ordinary loop control comparing a counter
against a limit, not a stuck condition, so it is not the gate that should be
skipping the kick.

Note the fallthrough count and the kick count are reported on different modulo
intervals, so they cannot be compared directly across snapshots; do not read a
discrepancy between them as meaningful without aligning the reporting.

### Method note

The regression went unnoticed for roughly eight iterations because each one
compared new probe output against expectations rather than against a known-good
baseline metric. A cheap guard against repeating this: check `tme` and `notme`
in every run and treat any change in them as a regression signal, since they
measure whether the game is still progressing at all, independent of whatever is
being investigated.

## Synthesis retracted: the desync is not hiding overlay entries

Splitting MSCAL targets by whether the chunk carrying them was corrupted:

    MSCAL targets in CLEAN chunks: 0xcd8 x1782
    MSCAL targets in DIRTY chunks: 0x10 0x18 0x28 0x30 0x38 0x48 0x50 ...

Clean, uncorrupted chunks contain only 0xcd8. The dirty ones carry a scatter of
small garbage targets. So the desync is not swallowing MSCALs to other overlays,
because there are none to swallow: the game genuinely runs one VU1 program in
this boot phase. The previous section's story about lost overlay entries is
retracted.

Both remaining ways a GIF packet could be built at VU address 0 are also active,
so neither explains the empty packet:

    UNPACK dest  total=30000   intoQw0-63=12074
    VU stores    total=3560000 intoQw0-63=513998

That region is written constantly by both mechanisms, yet XGKICK from qword 0
still reads float constants rather than a GIFtag.

## Where this stops, honestly

The VU1 thread is not converging. Over roughly a dozen iterations each
hypothesis was refuted by the next measurement: the 0x3xxx overlays turned out
to be garbage-reachable, the lost-MSCAL story turned out to be false, the
read-after-write hazard was disproven, the destination-mask theory was
disproven, and MTIR decodes correctly. What remains is a single well-measured
but unexplained fact:

    program 0xcd8 uses VF2 as a packet pointer, VF2 is never non-zero in any
    lane across 38 million sampled instructions, and no instruction in that
    program's range initialises it

Either the program is genuinely reading an uninitialised register and something
earlier in the frame should have set it, or the static writer scan is missing an
instruction class because it relies on decoder metadata that has already proven
unreliable for opcode and field extraction elsewhere in this work.

The most promising next step is not more VU1 archaeology. It is to compare
against a reference implementation: run the same disc under PCSX2 with VU1
logging, capture what VF2 and VI11 hold at pc 0x1160 and 0x11a0, and diff the
instruction trace of program 0xcd8 against ours. That converts an open-ended
search into a diff, which is what this problem now needs.

## State and what is left

## State and what is left

## State and what is left

RedOctane still renders correctly. Screens after it now draw real geometry with
real positions instead of nothing, but paint as flat colour blocks (a cyan
rectangle over dark grey) rather than art.

The one remaining root cause is the residual VIF1 desync: 18 dirty chunks and
about 1.09 MB per 45 s. It still injects garbage TEX0 (144 M texel fetches at
PSM 0x4, which is not a valid pixel-storage format) and leaves 8504 of 17430
textured draws sampling a TBP no transfer ever wrote.

Two desync sites are known. One recurs at pos 0x368 of a 0x9a0 chunk. The other
sits at pos 0x75000 of a 0x80020 chunk, preceded by a long run of zero words
read as NOPs, which suggests a large `ref` tag payload being concatenated into
the VIF stream when it is not VIF commands.

Measurement caveat recorded for future reference: the runtime's `[frame]`
`otherColors` field counts pixels differing from the first pixel, not distinct
colours. Earlier notes that read it as an entropy measure were wrong.

## VU1 findings (in progress)

VU1 is genuinely emulated (`vu/ps2_vu1_core.cpp`, a real interpreter with
pipeline modelling and an XGKICK unit) and is genuinely kicked: the MSCAL
callback is wired in `ps2_runtime.cpp:686` and calls `m_vu1.execute(...)` with a
65536-cycle budget. MPG and MSCAL agree on address scaling, both `imm * 8`.

Its early output is correct. The first XGKICK packets decode as a textured,
gouraud, alpha-blended tristrip (`prim=0x5c`, regs ST/RGBAQ/XYZF2) with the
first vertex at X=0x880c (2176.75) Y=0x8421 (2114.06), i.e. screen
(384.75, 290.06). ST is normalised 0..1 and varies per vertex. That is the
path that draws RedOctane.

A first measurement that only tested each packet's *first* vertex suggested
96% of packets were off-screen. That was wrong: a tristrip legitimately starts
outside the visible box. Testing every vertex in the packet and counting a
packet bad only when no vertex lands in the visible box gives a very different
split:

    tally good=4546 bad=3954

    pc0xcd8 = 4350/1602 good/bad      <- the main pipeline, ~73% visible
    pc0x3420, 0x35f0, 0x3ac8, 0x3b20, 0x3ba0, 0x3c58, 0x3d28, 0x3d88,
    0x3eb0, 0x3ef8, 0x3f50, 0x3f58, 0x3f60, 0x3f98, 0x3fa0, 0x3fa8,
    0x3fb0, 0x3fc8 (837 packets), 0x3fd8  = 0 good, essentially all bad

So the main geometry path works and its "bad" share is ordinary off-screen
clipping. The real anomaly is narrower: a family of programs in the 0x34xx to
0x3fxx range never produces a single visible vertex, across roughly 1800
packets. `0x3fc8` alone accounts for 837 of them.

Every one of those addresses is inside a range MPG actually loaded
(`0x30b0+0x680`, `0x3760+0x6c8`, `0x3e28+0x1d8`), so these are real programs
executing real microcode, not jumps into unwritten memory. The bug is in
execution or in the data those programs read, not in program placement.

Separately, a handful of MSCALs target addresses around 0x7f340 to 0x7ffa0,
which is ~520000 and far past the 16 KB of VU1 code memory. Those come from
`imm` near 65535, i.e. fake MSCALs decoded from the residual desynced stream,
which `microAddressMask()` then folds back into range and executes. Low volume
(tens against thousands) but worth eliminating with the remaining desync.

Ruled out for the 0x3xxx family: program placement (all their addresses fall
inside ranges MPG actually loaded), VIF desync (1.09 MB of 31 MB is 3.5% of
bytes and cannot explain a 100% failure rate), and VIF double buffering, which
measures correct at every MSCAL (`base=0 ofst=330`, TOP alternating 0 and 330,
DBF toggling).

Next: find what the 0x34xx-0x3fxx programs have in common that 0xcd8 does not.
They are the candidates for drawing the Activision and Harmonix logos, which
currently paint as a single flat quad with their textures bound but never
sampled.

## Remaining blocker: VU1 emits off-screen vertices

Screens after RedOctane draw as a uniform green fill (184320 pixels,
`otherColors=0`, a single flat quad). Their textures upload and bind, but the
draws bound to them fetch zero texels because their geometry lands outside the
screen.

    tbp0=6976 prim=5 fst=0 scissor=(0,0)-(511,447) ofx=1792 ofy=1824
      v0=(4056, 2767.68) v1=(40.125, 2771.25) v2=(40.125, 2772.31)

XYOFFSET is correct: 1792 = 2048 - 512/2 and 1824 = 2048 - 448/2, so the
visible box in vertex space is x 1792..2304, y 1824..2272. These vertices sit
at y around 2770 and x at 4056 and 40, entirely outside it, and form a nearly
degenerate sliver. The rasterizer is behaving correctly; its input is wrong.

That data is PATH1 output, so the next target is VU1: either the microprogram's
input (the residual 1.09 MB of desync) or VU1 execution itself.

## The MFIFO fix (patch 0019)

Two ring-aware bounds plus stall/resume, in `ps2_memory.cpp`:

- The tag bound now catches a tag that is *past* the fill pointer
  (`distTag == 0 || distTag > mfifoMask/2`), not merely one with fewer than
  16 bytes left. The original exact-equality test caught neither.
- Payload ranges are bounded too, for the tag ids whose data follows the tag
  inside the ring (1, 2, 5, 6, 7). ids 0/3/4 source from ADDR in ordinary
  RDRAM and are correctly exempt.
- Running out of written ring is a stall: TADR stays on the tag that could not
  be completed, STR stays set, and no channel-completion interrupt is raised,
  so the next ring fill resumes the same chain.

Measured before and after, 45 s each:

    tagPastFill      175  -> 0
    dirtyBytes    18.4 MB -> 1.09 MB      (94% down)
    invalidOps      21797 -> 5612         (74% down)
    truncations        22 -> 2
    bytesDiscarded   6212 -> 0

The RedOctane stage is unchanged (`nonBlack=9639 otherColors=9491`, identical
before and after), so the fix costs nothing that was working.

What it does *not* do is make screens 2-4 show art. Those frames now read
near-black instead of noise. That is not lost content: the pre-fix frames were
`nonBlack=227263 otherColors=227262`, one distinct colour per pixel, which is
noise, not a picture. Removing the garbage revealed that nothing real was
underneath. The remaining question for screens 2-4 is therefore why the game
submits no geometry for them, which is likely the same stall that keeps it in
`ui/mem_card.dtb`.

Still open: 41 dirty chunks and 1.09 MB of desynced bytes per 45 s remain, from
a source other than the fill pointer. Note the `payloadPastFill` counter is not
a clean measure of this, since the diagnostic counts every tag id while the fix
deliberately bounds only ids 1/2/5/6/7.

Ruled out along the way: TTE tag splicing (TTE is enabled for 2 tags total,
0 skipped by the id predicate), and any fault in the GS texture path.

## Why it stalls at screen 4

Separate from the video problem. Over 120 s the build issues only 4
`UIManager::GotoScreen` calls and 2 `UIScreen::Enter`, then sits in
`ui/mem_card.dtb`. No memory-card traffic appears at all, and the ThreadCall
worker stays parked on its semaphore (`sema id=16 count=0 max=1 waiters=1`),
which is only ever signalled for `GHMCLoadData` / `GHMCSaveData` / `GHMCFormat`.
