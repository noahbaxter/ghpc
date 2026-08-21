// Rand, the engine's random number generator, plus two small utilities that
// live nearby. Addresses 0x32d980, 0x32da88, 0x32f2e0 and 0x104ba8.

#include "gh2/inferred_types.h"

// R249: xor the entry at one cursor with the entry at the other, keep the
// result in place, then advance both cursors modulo 249. The wrap constant
// 0xf9 appears twice, once per cursor, which is what fixes the table length.
//
// Both stores to the cursors sit in branch delay slots that are not annulled,
// so the increment always lands and the wrap is a separate conditional store.
// Written out, that is a post-increment with a wrap test.
int Rand::Int() { // 0x32da88
    int r = mTable[mI] ^ mTable[mJ];
    mTable[mI] = r;
    if (++mI >= 249)
        mI = 0;
    if (++mJ >= 249)
        mJ = 0;
    return r;
}

// Half-open range. The assert is one-sided, so lo == hi is rejected but nothing
// checks the width.
//
// Note that this inherits a real quirk: MIPS div puts the remainder in HI with
// the sign of the dividend, and Int() spans the full signed range, so a negative
// draw yields a result below lo. That is the original behaviour, not a
// translation artifact.
int Rand::Int(int lo, int hi) { // 0x32d980
    MILO_ASSERT(lo < hi, 40);
    return lo + Int() % (hi - lo);
}

// Walks a zero-terminated table of primes in the data segment at 0x445240 and
// returns the first one that is not smaller than the request. Falls through to
// the request itself when the table runs out, so a very large hash table just
// keeps whatever size it asked for.
//
// The leading load of the first entry is a separate early-out for an empty
// table, ahead of the loop proper.
int NextHashPrime(int atLeast) { // 0x32f2e0
    extern const int kHashPrimes[]; // 0x445240
    for (const int *p = kHashPrimes; *p != 0; ++p) {
        if (*p >= atLeast)
            return *p;
    }
    return atLeast;
}

// Standard unsigned LEB128. The first byte is peeled out of the loop so the
// common one-byte case costs no shift.
const unsigned char *decode_uleb128(const unsigned char *p, unsigned int *out) { // 0x104ba8
    unsigned char b = *p++;
    unsigned int value = b & 0x7f;
    int shift = 0;
    while (b & 0x80) {
        b = *p++;
        shift += 7;
        value |= (unsigned int)(b & 0x7f) << shift;
    }
    *out = value;
    return p;
}
