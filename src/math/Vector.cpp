// Vector and transform math. Addresses 0x1e41e0, 0x1e4210, 0x32dbf0, 0x32ece8,
// 0x32eec8 and 0x32f188.
//
// Everything past the first two runs in VU0 macro mode. That turned out to be
// far easier to read than the ranking predicted: macro mode is just extra
// instructions in the normal MIPS stream, one whole quadword per operation, and
// the register file is small enough to follow by eye. See docs/decomp-log.md.
//
// A running caveat: several of these depend on VU0's vf00 reading as
// (0, 0, 0, 1). That register is hardwired on real hardware. ghpc had a bug
// where it was writable and got clobbered, fixed in commit b2bfc48. Anything
// here that uses $vf0w as a literal 1.0 would silently produce garbage against
// the unfixed runtime, so these are good regression targets.

#include "gh2/inferred_types.h"

#include <math.h>

// Adds a two component vector into the x and z lanes of a three component one.
// y is loaded and stored back untouched, so the Vector2 is a ground-plane
// offset, not an xy offset. That is unambiguous in the disassembly: 0x04 of the
// source goes straight to 0x04 of the destination with no arithmetic on it.
void Add(const Vector3 &a, const Vector2 &b, Vector3 &out) { // 0x1e41e0
    out.x = a.x + b.x;
    out.y = a.y;
    out.z = a.z + b.y;
}

void Subtract(const Vector3 &a, const Vector2 &b, Vector3 &out) { // 0x1e4210
    out.x = a.x - b.x;
    out.y = a.y;
    out.z = a.z - b.y;
}

// Quaternion normalize, entirely in VU0 macro mode.
//
//   vmul.xyzw  vf5, vf4, vf4    componentwise square
//   vaddy.x / vaddz.x / vaddw.x fold y, z and w into the x lane
//   vrsqrt Q, vf0w, vf5x        Q = 1.0 / sqrt(lengthSquared)
//   vwaitq                      the divide unit is asynchronous
//   vmulq.xyzw vf4, vf4, Q      scale
//
// The vf0w operand is the hardwired 1.0, which is what makes vrsqrt compute a
// reciprocal square root rather than a plain one.
void Normalize(const Hmx::Quat &q, Hmx::Quat &out) { // 0x32dbf0
    float lenSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    float inv = 1.0f / sqrtf(lenSq);
    out.x = q.x * inv;
    out.y = q.y * inv;
    out.z = q.z * inv;
    out.w = q.w * inv;
}

// Rotate a vector by a quaternion. The quaternion is expanded to a rotation
// matrix on the stack first, then it is a plain 3x3 multiply. Only the xyz
// lanes are written, so the destination's w padding survives.
void Multiply(const Vector3 &v, const Hmx::Quat &q, Vector3 &out) { // 0x32ece8
    Hmx::Matrix3 rot;
    MakeRotMatrix(q, rot);
    out.x = rot.x.x * v.x + rot.y.x * v.y + rot.z.x * v.z;
    out.y = rot.x.y * v.x + rot.y.y * v.y + rot.z.y * v.z;
    out.z = rot.x.z * v.x + rot.y.z * v.y + rot.z.z * v.z;
}

// Affine transform composition. The three rotation rows go through an unrolled
// count-down loop, then the translation row is handled once more outside it with
// an extra vmaddw against vf0w, which is the implicit 1.0 in the homogeneous
// fourth component.
//
// This variant writes xyz only, so each destination row keeps whatever was in
// its w padding.
void Multiply(const Transform &m, const Transform &n, Transform &out) { // 0x32eec8
    for (int row = 0; row < 3; ++row) {
        const Vector3 &r = (&m.m.x)[row];
        Vector3 &o = (&out.m.x)[row];
        o.x = n.m.x.x * r.x + n.m.y.x * r.y + n.m.z.x * r.z;
        o.y = n.m.x.y * r.x + n.m.y.y * r.y + n.m.z.y * r.z;
        o.z = n.m.x.z * r.x + n.m.y.z * r.y + n.m.z.z * r.z;
    }
    out.v.x = n.m.x.x * m.v.x + n.m.y.x * m.v.y + n.m.z.x * m.v.z + n.v.x;
    out.v.y = n.m.x.y * m.v.x + n.m.y.y * m.v.y + n.m.z.y * m.v.z + n.v.y;
    out.v.z = n.m.x.z * m.v.x + n.m.y.z * m.v.y + n.m.z.z * m.v.z + n.v.z;
}

// Byte-for-byte the same code as Multiply with every field mask widened from
// xyz to xyzw, so the w padding is carried through the arithmetic instead of
// being left alone. The two bodies differ only in the mask bits of the VU0
// opcodes.
void Multiply2(const Transform &m, const Transform &n, Transform &out) { // 0x32f188
    for (int row = 0; row < 3; ++row) {
        const Vector3 &r = (&m.m.x)[row];
        Vector3 &o = (&out.m.x)[row];
        o.x = n.m.x.x * r.x + n.m.y.x * r.y + n.m.z.x * r.z;
        o.y = n.m.x.y * r.x + n.m.y.y * r.y + n.m.z.y * r.z;
        o.z = n.m.x.z * r.x + n.m.y.z * r.y + n.m.z.z * r.z;
        o.w = n.m.x.w * r.x + n.m.y.w * r.y + n.m.z.w * r.z;
    }
    out.v.x = n.m.x.x * m.v.x + n.m.y.x * m.v.y + n.m.z.x * m.v.z + n.v.x;
    out.v.y = n.m.x.y * m.v.x + n.m.y.y * m.v.y + n.m.z.y * m.v.z + n.v.y;
    out.v.z = n.m.x.z * m.v.x + n.m.y.z * m.v.y + n.m.z.z * m.v.z + n.v.z;
    out.v.w = n.m.x.w * m.v.x + n.m.y.w * m.v.y + n.m.z.w * m.v.z + n.v.w;
}
