# Decisions

Settled choices and the reasoning that settled them, so they do not get
re-litigated. Current-phase decisions live in `NEXT.md`; this file is for the
ones that are not expected to change again.

## PS2 rather than Xbox 360

GH2 also shipped on 360, and XenonRecomp is more mature than PS2Recomp
(Unleashed Recompiled shipped on it; PS2Recomp has no completed game). PS2 was
chosen on:

- All three titles have symbolized debug builds. Only GH2 exists on 360.
- One pipeline covers all three. Each reports 18 VU overlays, indicating a
  shared engine and VU path.
- GS and VU1 are the usual PS2 obstacles. GH2's VU microcode is ~28 KB across 18
  named indexed overlays, under 1% of `.text`, and the game has a focus mode
  that disables world rendering.

A 360 copy is useful as a second build to diff against.
