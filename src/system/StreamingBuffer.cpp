// StreamingBuffer, the circular byte buffer behind the streaming audio path.
// Addresses 0x326640 and 0x326688.
//
// These two are the strongest single piece of evidence in this tree so far,
// because they are exact mirrors. Each one branches the same three ways on the
// same two cursors and reaches the same two fallback fields, with the read and
// write roles swapped and the empty/full answer inverted. Getting the layout
// wrong would break the symmetry, and it does not break.
//
// Confidence for every function in this file, with the evidence behind it,
// is in docs/confidence.md.

#include "gh2/inferred_types.h"

int StreamingBuffer::BytesReadable() const { // 0x326640
    if (mReadPos < mWritePos)
        return mWritePos - mReadPos;
    if (mWritePos < mReadPos)
        return mSize + mWritePos - mReadPos; // the writer has wrapped
    // Cursors coincide, so the buffer is either completely full or empty.
    return mFull ? mSize : 0;
}

int StreamingBuffer::BytesWriteable() const { // 0x326688
    if (mWritePos < mReadPos)
        return mReadPos - mWritePos;
    if (mReadPos < mWritePos)
        return mSize + mReadPos - mWritePos; // the reader has wrapped
    return mFull ? 0 : mSize;
}
