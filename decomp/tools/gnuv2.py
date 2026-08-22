"""Minimal GNU v2 (g++ 2.x) name demangler.

Only covers what the GH2 symbol table actually uses. Good enough to recover the
class name, the method name and a rough parameter list. Anything it cannot parse
comes back as None so callers fall back to the raw symbol.
"""

import re

BUILTIN = {
    "v": "void", "c": "char", "s": "short", "i": "int", "l": "long",
    "x": "long long", "f": "float", "d": "double", "r": "long double",
    "b": "bool", "w": "wchar_t", "e": "...",
}

OPERATORS = {
    "ct": "{ctor}", "dt": "{dtor}",
    "as": "operator=", "eq": "operator==", "ne": "operator!=",
    "lt": "operator<", "gt": "operator>", "le": "operator<=", "ge": "operator>=",
    "pl": "operator+", "mi": "operator-", "ml": "operator*", "dv": "operator/",
    "md": "operator%", "apl": "operator+=", "ami": "operator-=",
    "amu": "operator*=", "adv": "operator/=",
    "nw": "operator new", "dl": "operator delete",
    "vn": "operator new[]", "vd": "operator delete[]",
    "rf": "operator->", "vc": "operator[]", "cl": "operator()",
    "aa": "operator&&", "oo": "operator||", "nt": "operator!",
    "adr": "operator&", "ind": "operator*",
    "pp": "operator++", "mm": "operator--",
    "or": "operator|", "ad": "operator&", "er": "operator^",
    "ls": "operator<<", "rs": "operator>>",
    "aor": "operator|=", "aad": "operator&=", "aer": "operator^=",
    "als": "operator<<=", "ars": "operator>>=",
    "cm": "operator,", "co": "operator~",
}


class _Cur:
    def __init__(self, s):
        self.s = s
        self.i = 0

    def peek(self):
        return self.s[self.i] if self.i < len(self.s) else ""

    def eof(self):
        return self.i >= len(self.s)


def _read_len_name(c):
    """Read a <digits><name> run, including Qn nested qualified names."""
    if c.peek() == "Q":
        c.i += 1
        n = int(c.s[c.i])
        c.i += 1
        parts = [_read_len_name(c) for _ in range(n)]
        if any(p is None for p in parts):
            return None
        return "::".join(parts)
    m = re.match(r"\d+", c.s[c.i:])
    if not m:
        return None
    n = int(m.group(0))
    c.i += len(m.group(0))
    name = c.s[c.i:c.i + n]
    if len(name) != n:
        return None
    c.i += n
    return name


def _read_type(c):
    """Read one mangled type. Returns a display string, or None on give-up."""
    prefix = ""
    while c.peek() in ("P", "R", "C", "U", "S", "V"):
        ch = c.peek()
        c.i += 1
        if ch == "P":
            prefix = "*" + prefix
        elif ch == "R":
            prefix = "&" + prefix
        elif ch == "C":
            prefix = prefix + " const"
        elif ch == "U":
            prefix = prefix + " unsigned"
        elif ch == "S":
            prefix = prefix + " signed"
        elif ch == "V":
            prefix = prefix + " volatile"
    ch = c.peek()
    if ch == "":
        return None
    if ch in BUILTIN:
        c.i += 1
        return (BUILTIN[ch] + " " + prefix).strip()
    if ch.isdigit() or ch == "Q":
        name = _read_len_name(c)
        if name is None:
            return None
        return (name + " " + prefix).strip()
    if ch == "F":  # function pointer, bail out after consuming crudely
        return None
    if ch == "A":  # array
        c.i += 1
        m = re.match(r"\d+", c.s[c.i:])
        if not m:
            return None
        n = m.group(0)
        c.i += len(n)
        if c.peek() == "_":
            c.i += 1
        inner = _read_type(c)
        if inner is None:
            return None
        return "%s[%s] %s" % (inner, n, prefix).strip()
    if ch == "T":  # back-reference to an earlier argument
        c.i += 2
        return "T?"
    return None


def _read_args(rest):
    c = _Cur(rest)
    args = []
    while not c.eof():
        t = _read_type(c)
        if t is None:
            args.append("?")
            break
        args.append(t)
    return args


def demangle(sym):
    """Return (class_name, method_name, [arg types]) or None.

    A trailing " const" is appended to the method name for const members, since
    GNU v2 encodes that as a C before the class name and it matters when the
    class has both overloads.
    """
    if not sym or sym.startswith("_ZN") or "__" not in sym:
        return None

    # Special member / operator form: __ct__6RndCam..., __dt__..., __as__...
    m = re.match(r"^__(\w{2,3})__(.*)$", sym)
    if m and m.group(1) in OPERATORS:
        op = OPERATORS[m.group(1)]
        rest = m.group(2)
        c = _Cur(rest)
        cls = _read_len_name(c) if (rest[:1].isdigit() or rest[:1] == "Q") else None
        tail = rest[c.i:] if cls else rest
        if tail[:1] == "C" and tail[1:2] == "F":
            tail = tail[2:]
        elif tail[:1] == "F":
            tail = tail[1:]
        args = _read_args(tail)
        if op == "{ctor}":
            op = (cls.split("::")[-1] if cls else "ctor")
        elif op == "{dtor}":
            op = "~" + (cls.split("::")[-1] if cls else "dtor")
        return (cls, op, args)

    # Global operator without class: __nw__FUi etc, handled above. Now methods.
    idx = sym.find("__")
    while idx != -1:
        name = sym[:idx]
        rest = sym[idx + 2:]
        is_const = rest[:1] == "C" and (rest[1:2].isdigit() or rest[1:2] == "Q")
        if is_const:
            rest = rest[1:]
            name = name + " const"
        if name and (rest[:1].isdigit() or rest[:1] == "Q" or rest[:1] == "F"):
            if rest[:1] == "F":  # free function
                return (None, name, _read_args(rest[1:]))
            c = _Cur(rest)
            cls = _read_len_name(c)
            if cls is not None:
                tail = rest[c.i:]
                if tail[:1] == "F":
                    tail = tail[1:]
                elif tail[:1] == "C" and tail[1:2] == "F":
                    tail = tail[2:]
                return (cls, name, _read_args(tail))
        idx = sym.find("__", idx + 1)
    return None


def pretty(sym):
    d = demangle(sym)
    if not d:
        return sym
    cls, name, args = d
    suffix = ""
    if name.endswith(" const"):
        name, suffix = name[:-6], " const"
    full = ("%s::%s" % (cls, name)) if cls else name
    return "%s(%s)%s" % (full, ", ".join(args), suffix)


if __name__ == "__main__":
    import sys
    for line in sys.stdin:
        print(pretty(line.strip()))
