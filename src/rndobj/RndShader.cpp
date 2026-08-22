// RndShader. Addresses 0x205c28 and 0x205c30.
//
// Two functions, and between them they document the Milo message protocol's
// dead end, which is worth having written down once.
//
// Confidence for every function in this file, with the evidence behind it,
// is in docs/confidence.md.

#include "gh2/inferred_types.h"

// No copyable state.
void RndShader::Copy(const Hmx::Object *, Hmx::Object::CopyType) {} // 0x205c28

// The end of the Handle chain. Node 1 of a message array is the message name,
// so this pulls that symbol out, complains when the caller asked to be warned,
// and returns integer zero.
//
// The format string at 0x49c420 is "Unhandled msg: %s", which names the whole
// pattern. The returned node is stored as value 0 at +0x00 and tag 6 at +0x04,
// and 6 is kDataInt, so the fallback answer is the integer 0 rather than the
// kDataUnhandled tag one might expect.
DataNode RndShader::Handle(DataArray *msg, bool warn) { // 0x205c30
    Symbol name = msg->Node(1)->Sym(msg);
    (void)name;
    if (warn)
        MILO_WARN("Unhandled msg: %s", name);
    return DataNode(0);
}
