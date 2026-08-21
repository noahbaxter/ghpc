// RndDrawable bounding sphere handling, plus the two other places that use the
// same VU0 zeroing idiom. Addresses 0x3b5860, 0x1dccd8 and 0x18d570.
//
// sqc2 $vf0, 0(reg) stores VU0's hardwired vf00, which reads as
// (0.0, 0.0, 0.0, 1.0), as one 16 byte quadword. It is the engine's fastest way
// to clear a padded Vector3 while leaving the homogeneous w at 1. It is also a
// direct dependency on vf00 being read-only, so these three are the cheapest
// regression test for the ghpc vf00 fix in commit b2bfc48.

#include "gh2/inferred_types.h"

// Clears the cached bounds. The quadword lands on the centre at +0x10 and the
// following word clears the radius at +0x20.
void RndDrawable::UpdateSphere() { // 0x3b5860
    mSphere.center.x = 0.0f;
    mSphere.center.y = 0.0f;
    mSphere.center.z = 0.0f;
    mSphere.center.w = 1.0f;
    mSphere.radius = 0.0f;
}

// The script-facing version of the same clear.
//
// Worth spelling out because the register assignment looks wrong at a glance:
// the sphere is cleared through $a1, not $a0. This function returns a DataNode
// by value, and the g++ 2.x MIPS ABI passes a hidden pointer to the return slot
// in $a0, which pushes this into $a1 and the DataArray into $a2. So $a1 is this
// and the two zero stores through $a0 at the end are the returned empty node.
DataNode RndDrawable::OnZeroSphere(const DataArray *) { // 0x1dccd8
    UpdateSphere();
    return DataNode();
}
