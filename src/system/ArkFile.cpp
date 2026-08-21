// ArkFile, a read cursor over one entry in the ARK archive.
// Addresses 0x2f8070..0x2f80e8 and 0x427a30.

#include "gh2/inferred_types.h"

// The compiler folded the three cases into one tail. kSeekCur enters the shared
// "add to the cursor" tail with the cursor preloaded, kSeekEnd enters the same
// tail one instruction later with the length preloaded instead, and kSeekSet
// stores the offset and jumps past the add. An out-of-range type falls through
// every test and leaves the cursor alone, which is why the default case is a
// plain return rather than an assert.
int ArkFile::Seek(int offset, SeekType type) { // 0x2f8070
    switch (type) {
    case kSeekSet:
        mPos = offset;
        break;
    case kSeekCur:
        mPos = mPos + offset;
        break;
    case kSeekEnd:
        mPos = mSize + offset;
        break;
    default:
        break;
    }
    return mPos;
}

// xor then sltiu against 1, which is the idiom for an equality test that has to
// produce a 0/1 value rather than set a branch.
bool ArkFile::Eof() { return mPos == mSize; } // 0x2f80c8

// sltu $v0, $zero, $v0 normalises any nonzero error code to 1.
bool ArkFile::Fail() { return mError != 0; } // 0x2f80e0

// Reads never buffer, so there is nothing to push.
void ArkFile::Flush() {} // 0x427a30
