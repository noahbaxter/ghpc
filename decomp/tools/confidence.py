#!/usr/bin/env python3
"""Emit docs/confidence.md, one row per decompiled function.

The assessments live in this file. They are my reasoned judgement, not the
output of any check: nothing in this repo has been executed. Each row carries
the concrete evidence that backs its level.

GROUPS gives the default level and evidence for every function in a source file.
OVERRIDES pins individual functions that differ from their file's default.
"""

import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEF = re.compile(r"^[A-Za-z_].*[){].*//\s*(0x[0-9a-f]{5,8})")

GROUPS = {
    "src/synth/ADSR.cpp": (
        "high",
        "Save (0x2576d8) and Load (0x2577e0) serialize the same eight offsets in "
        "ascending order at 4 bytes each; assert strings recovered from rodata "
        "give the original parameter names and range bounds; RB3's ADSR has "
        "byte-identical offsets."),
    "src/synth/MicNull.cpp": (
        "high",
        "One or two instruction bodies with no state; every constant is a "
        "literal in the delay slot."),
    "src/synth/StreamNull.cpp": (
        "high",
        "Inert overrides are single jr $ra; the five timer-backed ones are tail "
        "calls to named VarTimer symbols with this + 8 as the receiver."),
    "src/synth/Synth.cpp": (
        "high",
        "Abstract base defaults, all neutral literals; RB3's Synth declares the "
        "same virtual set with the same returns."),
    "src/synth/Submix.cpp": (
        "high",
        "Vtable slot +0x20 resolves to GetNumSlots in all three ChannelMapping "
        "subclasses, which names the slot and types the member."),
    "src/game/Sequence.cpp": (
        "high",
        "Save (0x259e48) writes the same six offsets in ascending order at 4 "
        "bytes each."),
    "src/game/TrackConfig.cpp": (
        "high",
        "Float literals decoded exactly (0.06f, and the (i-2)*4 slot spacing); "
        "assert strings recovered as TrackConfig.cpp and trackNum >= 0."),
    "src/game/Performer.cpp": (
        "high",
        "Direct field reads with no ambiguity; virtual slots resolved against "
        "_vt$9Performer at 0x449e10."),
    "src/game/StarPower.cpp": (
        "high",
        "Eleven bodies share the same lw 0x28 enable-gate prologue; clamp bounds "
        "decoded from 0x3f800000 and zero."),
    "src/game/TrackWatcherImpl.cpp": (
        "high",
        "Element size fixed by the >> 4 on the vector byte difference; symmetric "
        "window fixed by abs.s."),
    "src/game/PlayerMatcher.cpp": (
        "high",
        "Constructor at 0x117338 identifies the members; vtable slot +0x78 is "
        "Disable(bool) in all three concrete BeatMatchController subclasses."),
    "src/game/BankLoader.cpp": (
        "high",
        "Symbol literal recovered as \"reset\" and the warning format as "
        "\"Unhandled msg: %s\"; both return tags read directly off the stores."),
    "src/system/StreamingBuffer.cpp": (
        "high",
        "The two functions are exact mirrors of each other; a wrong layout would "
        "break the symmetry and it does not."),
    "src/system/ArkFile.cpp": (
        "high",
        "Eof xors +0x1c against +0x0c, naming both the cursor and the length."),
    "src/system/Rand.cpp": (
        "high",
        "The 0xf9 wrap appears once per cursor, fixing the table length; assert "
        "string recovered as \"high > low\" in Rand.cpp; prime table dumped from "
        "0x445240 and matches the expected shape."),
    "src/math/Vector.cpp": (
        "high",
        "VU0 macro mode is one whole quadword per instruction with an explicit "
        "field mask, so each body reads off directly; Multiply and Multiply2 "
        "differ only in mask bits, which cross-checks both."),
    "src/rndobj/RndDrawable.cpp": (
        "high",
        "sqc2 $vf0 stores the hardwired (0,0,0,1); the $a0 hidden return-slot "
        "pointer is confirmed by the two zero stores at the end."),
    "src/rndobj/RndShader.cpp": (
        "high",
        "Format string recovered as \"Unhandled msg: %s\"; return tag 6 is "
        "kDataInt."),
    "src/ui/UIList.cpp": (
        "high",
        "Eleven identical six-instruction forwarders with the subobject offset "
        "in the delay slot, pinning both +0x150 and +0x1c8."),
}

OVERRIDES = {
    # Performer, the band indirection.
    "0x111068": ("medium",
                 "StarPowerMultiplier. The dispatch is certain: slot +0x0b0 on "
                 "player 0's Performer. Read literally that is unbounded "
                 "recursion, and clang says so. My reading, that the concrete "
                 "band class in player 0's slot overrides it, is inference."),
    "0x111028": ("medium",
                 "GetCrowdBoost. Same band indirection as StarPowerMultiplier."),
    "0x110f90": ("medium",
                 "IsUsingStarPower. Same band indirection as "
                 "StarPowerMultiplier."),
    # StarPower params block.
    "0x122bb8": ("medium",
                 "GetMultiplier. Control flow and offsets are certain. The name "
                 "mParams->mMultiplier for +0x6c/+0x14 is read off how the three "
                 "query functions use it, not off any independent evidence."),
    "0x122be8": ("medium",
                 "GetCrowdBoost. Same params-block naming caveat."),
    "0x121de0": ("medium",
                 "OnDownbeat. Same params-block naming caveat for +0x6c/+0x00."),
    # TrackWatcherImpl cheating marker.
    "0x287b38": ("medium",
                 "SetCheating. The stores are certain. Calling +0x64 a "
                 "\"cheating started here\" marker rather than a counter is "
                 "inference from the one-sided update."),
    # Synth bank slots.
    "0x260c98": ("medium",
                 "GetNumBankSlots. The (end - begin) >> 3 is certain, so the "
                 "element is 8 bytes. Nothing pins what the element is."),
    # StreamNull member typing.
    "0x3eb160": ("medium",
                 "ChannelFaders. Indexed load through a base pointer at +0x58 "
                 "with a stride of 4. Calling it a std::vector<Fader *> comes "
                 "from the neighbouring +0x5c end pointer, not from this body."),
    # Vector2 lane mapping.
    "0x1e41e0": ("medium",
                 "Add. The arithmetic is certain: +0x04 of the source is copied "
                 "through untouched and the Vector2's second float lands on z. "
                 "Naming the Vector2 fields x and y rather than x and z is a "
                 "convention choice."),
    "0x1e4210": ("medium", "Subtract. Same lane-naming caveat as Add."),
}


def main():
    done = {}
    for base, _, names in os.walk(os.path.join(ROOT, "src")):
        for n in sorted(names):
            if not n.endswith(".cpp"):
                continue
            path = os.path.join(base, n)
            rel = os.path.relpath(path, ROOT)
            with open(path) as fh:
                for line in fh:
                    m = DEF.match(line)
                    if m:
                        done[m.group(1)] = rel

    with open(os.path.join(ROOT, "data", "ranked.json")) as fh:
        rows = json.load(fh)
    info = {r["addr"]: r for r in rows}
    rank = {r["addr"]: i + 1 for i, r in enumerate(rows)}

    out = []
    out.append("# Confidence per decompiled function\n")
    out.append("Generated by `tools/confidence.py`, which is also where the "
               "assessments are written down.\n")
    out.append("Nothing here has been executed. There is no verification "
               "harness. These levels are my own reasoned judgement about how "
               "much the disassembly constrains the reading, and every row "
               "names the evidence.\n")
    out.append("- **high**: the disassembly admits one reading and at least one "
               "independent check agrees (a mirrored function, a serialization "
               "order, a recovered assert string, a vtable entry, or a matching "
               "RB3 class).")
    out.append("- **medium**: the mechanism is certain, but a name or a "
               "semantic reading is inference. Each medium row says which part.")
    out.append("- **low**: none. Anything that would have landed here was "
               "dropped rather than written down.\n")
    header = list(out)
    out = []

    tally = {}
    for f in sorted(set(done.values())):
        level, evidence = GROUPS[f]
        addrs = sorted(a for a in done if done[a] == f)
        out.append("## %s\n" % f)
        out.append("Default **%s**. %s\n" % (level, evidence))
        out.append("| addr | rank | function | confidence |")
        out.append("|------|------|----------|------------|")
        notes = []
        for a in addrs:
            lvl = level
            if a in OVERRIDES:
                lvl = OVERRIDES[a][0]
                notes.append((a, OVERRIDES[a][1]))
            tally[lvl] = tally.get(lvl, 0) + 1
            r = info.get(a)
            name = r["demangled"].replace("|", "\\|") if r else "(not in ranking)"
            out.append("| %s | %s | %s | %s |"
                       % (a, rank.get(a, "-"), name, lvl))
        out.append("")
        for a, note in notes:
            out.append("- `%s` **medium**: %s" % (a, note))
        if notes:
            out.append("")

    summary = ["## Tally", "",
               "| level | functions |", "|-------|-----------|"]
    for lvl in ("high", "medium", "low"):
        summary.append("| %s | %d |" % (lvl, tally.get(lvl, 0)))
    summary.append("| **total** | **%d** |" % sum(tally.values()))
    summary.append("")

    path = os.path.join(ROOT, "docs", "confidence.md")
    with open(path, "w") as fh:
        fh.write("\n".join(header + summary + out) + "\n")
    print("wrote %s" % path)
    for lvl in ("high", "medium", "low"):
        print("  %-6s %d" % (lvl, tally.get(lvl, 0)))


if __name__ == "__main__":
    main()
