#!/usr/bin/env python3
"""Minimal VU1 microcode disassembler.

Encodings taken from the repo's own interpreter:
  ps2xRuntime/src/lib/vu/ps2_vu1_upper.cpp  (upper opcode = instr & 0x3F,
      special group 0x3C..0x3F selected by (instr & 3) | ((instr >> 4) & 0x7C))
  ps2xRuntime/src/lib/vu/ps2_vu1_lower.cpp  (lower opcode = (instr >> 25) & 0x7F,
      0x40 is the lower1 group with sub-op instr & 0x3F, whose 0x3F nests again)
"""
import struct
import sys

UPPER = {0x00: "ADDbc", 0x01: "ADDbc", 0x02: "ADDbc", 0x03: "ADDbc",
         0x04: "SUBbc", 0x05: "SUBbc", 0x06: "SUBbc", 0x07: "SUBbc",
         0x08: "MADDbc", 0x09: "MADDbc", 0x0A: "MADDbc", 0x0B: "MADDbc",
         0x0C: "MSUBbc", 0x0D: "MSUBbc", 0x0E: "MSUBbc", 0x0F: "MSUBbc",
         0x10: "MAXbc", 0x11: "MAXbc", 0x12: "MAXbc", 0x13: "MAXbc",
         0x14: "MINIbc", 0x15: "MINIbc", 0x16: "MINIbc", 0x17: "MINIbc",
         0x18: "MULbc", 0x19: "MULbc", 0x1A: "MULbc", 0x1B: "MULbc",
         0x1C: "MULq", 0x1D: "MAXi", 0x1E: "MULi", 0x1F: "MINIi",
         0x20: "ADDq", 0x21: "MADDq", 0x22: "ADDi", 0x23: "MADDi",
         0x24: "SUBq", 0x25: "MSUBq", 0x26: "SUBi", 0x27: "MSUBi",
         0x28: "ADD", 0x29: "MADD", 0x2A: "MUL", 0x2B: "MAX",
         0x2C: "SUB", 0x2D: "MSUB", 0x2E: "OPMSUB", 0x2F: "MINI"}

USPEC = {0x00: "ADDAbc", 0x01: "ADDAbc", 0x02: "ADDAbc", 0x03: "ADDAbc",
         0x04: "SUBAbc", 0x05: "SUBAbc", 0x06: "SUBAbc", 0x07: "SUBAbc",
         0x08: "MADDAbc", 0x09: "MADDAbc", 0x0A: "MADDAbc", 0x0B: "MADDAbc",
         0x0C: "MSUBAbc", 0x0D: "MSUBAbc", 0x0E: "MSUBAbc", 0x0F: "MSUBAbc",
         0x10: "ITOF0", 0x11: "ITOF4", 0x12: "ITOF12", 0x13: "ITOF15",
         0x14: "FTOI0", 0x15: "FTOI4", 0x16: "FTOI12", 0x17: "FTOI15",
         0x18: "MULAbc", 0x19: "MULAbc", 0x1A: "MULAbc", 0x1B: "MULAbc",
         0x1C: "MULAq", 0x1D: "ABS", 0x1E: "MULAi", 0x1F: "CLIP",
         0x20: "ADDAq", 0x21: "MADDAq", 0x22: "ADDAi", 0x23: "MADDAi",
         0x24: "SUBAq", 0x25: "MSUBAq", 0x26: "SUBAi", 0x27: "MSUBAi",
         0x28: "ADDA", 0x29: "MADDA", 0x2A: "MULA", 0x2B: "?2B",
         0x2C: "SUBA", 0x2D: "MSUBA", 0x2E: "OPMULA", 0x2F: "?2F",
         0x30: "NOP"}

LOWER = {0x00: "LQ", 0x01: "SQ", 0x04: "ILW", 0x05: "ISW",
         0x08: "IADDIU", 0x09: "ISUBIU",
         0x10: "FCEQ", 0x11: "FCSET", 0x12: "FCAND", 0x13: "FCOR",
         0x14: "FSEQ", 0x15: "FSSET", 0x16: "FSAND", 0x17: "FSOR",
         0x18: "FMEQ", 0x1A: "FMAND", 0x1B: "FMOR", 0x1C: "FCGET",
         0x20: "B", 0x21: "BAL", 0x24: "JR", 0x25: "JALR",
         0x28: "IBEQ", 0x29: "IBNE", 0x2C: "IBLTZ", 0x2D: "IBGTZ",
         0x2E: "IBLEZ", 0x2F: "IBGEZ"}

L1 = {0x30: "IADD", 0x31: "ISUB", 0x32: "IADDI", 0x34: "IAND", 0x35: "IOR"}

L1S = {0x30: "MOVE", 0x31: "MR32", 0x34: "LQI", 0x35: "SQI", 0x36: "LQD",
       0x37: "SQD", 0x38: "DIV", 0x39: "SQRT", 0x3A: "RSQRT", 0x3B: "WAITQ",
       0x3C: "MTIR", 0x3D: "MFIR", 0x3E: "ILWR", 0x3F: "ISWR",
       0x40: "RNEXT", 0x41: "RGET", 0x42: "RINIT", 0x43: "RXOR",
       0x64: "MFP", 0x68: "XTOP", 0x69: "XITOP", 0x6C: "XGKICK",
       0x70: "ESADD", 0x71: "ERSADD", 0x72: "ELENG", 0x73: "ERLENG",
       0x74: "EATANxy", 0x75: "EATANxz", 0x76: "ESUM", 0x77: "ERSQRT",
       0x78: "ESQRT", 0x79: "ESIN", 0x7A: "ERCPR", 0x7B: "WAITP",
       0x7C: "EATAN", 0x7D: "EEXP"}

DESTC = "wzyx"
BCC = "xyzw"


def dest_str(d):
    s = ""
    if d & 8:
        s += "x"
    if d & 4:
        s += "y"
    if d & 2:
        s += "z"
    if d & 1:
        s += "w"
    return s or "-"


def dis_upper(i):
    op = i & 0x3F
    dest = (i >> 21) & 0xF
    ft = (i >> 16) & 0x1F
    fs = (i >> 11) & 0x1F
    fd = (i >> 6) & 0x1F
    bc = i & 3
    if op >= 0x3C:
        sop = (i & 3) | ((i >> 4) & 0x7C)
        name = USPEC.get(sop, "u?%02X" % sop)
        if name.endswith("bc"):
            return "%s%s.%s ACC, vf%02d, vf%02d%s" % (name[:-2], BCC[bc], dest_str(dest), fs, ft, BCC[bc])
        if name == "CLIP":
            return "CLIP.xyz vf%02d, vf%02dw" % (fs, ft)
        if name in ("ABS", "ITOF0", "ITOF4", "ITOF12", "ITOF15", "FTOI0", "FTOI4", "FTOI12", "FTOI15"):
            return "%s.%s vf%02d, vf%02d" % (name, dest_str(dest), ft, fs)
        return "%s.%s ACC, vf%02d, vf%02d" % (name, dest_str(dest), fs, ft)
    name = UPPER.get(op, "u?%02X" % op)
    if name.endswith("bc"):
        return "%s%s.%s vf%02d, vf%02d, vf%02d%s" % (name[:-2], BCC[bc], dest_str(dest), fd, fs, ft, BCC[bc])
    if name.endswith("q") or name.endswith("i"):
        return "%s.%s vf%02d, vf%02d, %s" % (name, dest_str(dest), fd, fs, name[-1].upper())
    return "%s.%s vf%02d, vf%02d, vf%02d" % (name, dest_str(dest), fd, fs, ft)


def imm11(i):
    v = i & 0x7FF
    return v - 0x800 if v & 0x400 else v


def dis_lower(i, pc):
    if i == 0 or i == 0x8000033C:
        return "NOP"
    op = (i >> 25) & 0x7F
    it = (i >> 16) & 0xF
    isr = (i >> 11) & 0xF
    ftf = (i >> 16) & 0x1F
    fsf = (i >> 11) & 0x1F
    fdf = (i >> 6) & 0x1F
    dest = (i >> 21) & 0xF
    if op == 0x40:
        sub = i & 0x3F
        if sub >= 0x3C:
            s2 = (i & 3) | ((i >> 4) & 0x7C)
            name = L1S.get(s2, "l?%02X" % s2)
            if name in ("LQI", "LQD"):
                return "%s.%s vf%02d, (vi%02d)" % (name, dest_str(dest), ftf, isr)
            if name in ("SQI", "SQD"):
                return "%s.%s vf%02d, (vi%02d)" % (name, dest_str(dest), fsf, it)
            if name == "XGKICK":
                return "XGKICK vi%02d" % isr
            if name == "DIV":
                return "DIV Q, vf%02d%s, vf%02d%s" % (fsf, "xyzw"[(i >> 21) & 3], ftf, "xyzw"[(i >> 23) & 3])
            if name == "MTIR":
                return "MTIR vi%02d, vf%02d%s" % (it, fsf, "xyzw"[(i >> 21) & 3])
            if name == "MFIR":
                return "MFIR.%s vf%02d, vi%02d" % (dest_str(dest), ftf, isr)
            if name in ("MOVE", "MR32"):
                return "%s.%s vf%02d, vf%02d" % (name, dest_str(dest), ftf, fsf)
            if name in ("ILWR", "ISWR"):
                return "%s.%s vi%02d, (vi%02d)" % (name, dest_str(dest), it, isr)
            return "%s.%s vf%02d, vf%02d" % (name, dest_str(dest), ftf, fsf)
        name = L1.get(sub, "l?%02X" % sub)
        if name == "IADDI":
            imm = (i >> 6) & 0x1F
            if imm & 0x10:
                imm -= 0x20
            return "IADDI vi%02d, vi%02d, %d" % (it, isr, imm)
        return "%s vi%02d, vi%02d, vi%02d" % (name, (i >> 6) & 0xF, isr, it)
    name = LOWER.get(op, "L?%02X" % op)
    if name in ("LQ",):
        return "LQ.%s vf%02d, %d(vi%02d)" % (dest_str(dest), ftf, imm11(i), isr)
    if name in ("SQ",):
        return "SQ.%s vf%02d, %d(vi%02d)" % (dest_str(dest), fsf, imm11(i), it)
    if name in ("ILW", "ISW"):
        return "%s.%s vi%02d, %d(vi%02d)" % (name, dest_str(dest), it, imm11(i), isr)
    if name in ("IADDIU", "ISUBIU"):
        imm = (i & 0x7FF) | ((i >> 10) & 0x7800)
        return "%s vi%02d, vi%02d, 0x%x" % (name, it, isr, imm)
    if name in ("FCEQ", "FCSET", "FCAND", "FCOR"):
        return "%s 0x%06x" % (name, i & 0xFFFFFF)
    if name == "FCGET":
        return "FCGET vi%02d" % it
    if name in ("B", "BAL"):
        return "%s 0x%04x" % (name, (pc + 8 + imm11(i) * 8) & 0xFFFF)
    if name in ("JR", "JALR"):
        return "%s vi%02d" % (name, isr)
    if name.startswith("IB"):
        return "%s vi%02d, vi%02d -> 0x%04x" % (name, it, isr, (pc + 8 + imm11(i) * 8) & 0xFFFF)
    return name


def main():
    path = sys.argv[1]
    base = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0
    data = open(path, "rb").read()
    skip_next = False
    for off in range(0, len(data) - 7, 8):
        lo, hi = struct.unpack_from("<II", data, off)
        pc = base + off
        if skip_next:
            skip_next = False
            print("%04x  %08x %08x   <LOI immediate %g>" % (pc, lo, hi, struct.unpack("<f", struct.pack("<I", lo))[0]))
            continue
        flags = ""
        if hi & 0x80000000:
            flags += "I"
            skip_next = False
        if hi & 0x40000000:
            flags += "E"
        if hi & 0x20000000:
            flags += "M"
        if hi & 0x10000000:
            flags += "D"
        if hi & 0x08000000:
            flags += "T"
        up = dis_upper(hi)
        if hi & 0x80000000:
            low = "<LOI %g>" % struct.unpack("<f", struct.pack("<I", lo))[0]
        else:
            low = dis_lower(lo, pc)
        print("%04x  %-28s | %-34s %s" % (pc, up, low, flags))


main()
