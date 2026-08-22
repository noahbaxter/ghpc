// BankLoader::Handle. Address 0x124e68.
//
// The canonical shape of a Milo message handler, small enough to read whole.
// Node 1 of the message array is the message name. It is compared against a
// function-local static Symbol, built once behind a bool guard in .bss at
// 0x4edac4 and cached at 0x4edac0. tools/rodata.py reads the literal behind the
// Symbol constructor as "reset", and the warning format at 0x463aa8 as
// "Unhandled msg: %s".
//
// The two returns differ, which is worth noting because it is easy to
// normalise away: the handled path stores 0 to both words of the return slot,
// which is tag 0, kDataUnhandled. The fallback stores value 0 and tag 6, which
// is kDataInt. So a handled "reset" answers with nothing and an unhandled
// message answers with the integer zero.
//
// Confidence for every function in this file, with the evidence behind it,
// is in docs/confidence.md.

#include "gh2/inferred_types.h"

DataNode BankLoader::Handle(DataArray *msg, bool warn) { // 0x124e68
    Symbol name = msg->Node(1)->Sym(msg);

    static Symbol reset("reset"); // cached at 0x4edac0, guard at 0x4edac4
    if (name == reset) {
        Reset();
        return DataNode();
    }

    if (warn)
        MILO_WARN("Unhandled msg: %s", name);
    return DataNode(0);
}
