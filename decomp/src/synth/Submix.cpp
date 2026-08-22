// Submix::GetNumSlots. Address 0x2869c0.
//
// The call site only says "vtable slot +0x20 on the object at this + 0x04".
// Scanning every vtable in the ELF for that slot turns up exactly three
// entries, MassChannelMapping, MultiChannelMapping and SingleSlotChannelMapping,
// all of them GetNumSlots. That names both the slot and the member's type.
//
// Confidence for every function in this file, with the evidence behind it,
// is in docs/confidence.md.

#include "gh2/inferred_types.h"

int Submix::GetNumSlots() const { // 0x2869c0
    return mMapping->GetNumSlots();
}
