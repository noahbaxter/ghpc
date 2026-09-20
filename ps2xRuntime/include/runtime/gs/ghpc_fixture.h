#ifndef GHPC_FIXTURE_H
#define GHPC_FIXTURE_H

// Capture real draws to a file so the transform can be iterated in a
// standalone binary instead of a 300s game run. A fixture carries both the
// inputs a native draw reads and the primitives VU1 produced for the same
// draw, so the replay tool has an oracle to diff against rather than only a
// picture to squint at.
//
// Captured under GHPC_NATIVE_DRAW=2, which runs the host transform, submits
// nothing and lets VU1 draw. That is the only mode where both sides of the
// comparison exist in one run.
//
//     GHPC_NATIVE_DRAW=2 GHPC_FIXTURE=/tmp/gh2.fix GHPC_COUNTIN=0.5 ./run
//     ghpc/scripts/fixreplay.sh /tmp/gh2.fix
//
//   GHPC_FIXTURE_DRAWS=N   how many draws to keep, default 8
//   GHPC_FIXTURE_SKIP=N    skip the first N draws first
//   GHPC_FIXTURE_STRIDE=N  then keep one draw in every N
//
// Skip and stride exist because the draws right after StartGame are close-up
// character meshes and are not the frame: measured, they reject 4% of their
// triangles offscreen where a whole run rejects 72%. A contiguous burst off
// the front tells you about the cheapest draws in the frame.
//
// Layout: FixFileHeader, then drawCount records. Each record is a FixDrawHead
// followed by vertCount*64 bytes of Vert, faceCount*6 bytes of Face, and
// refCount FixRefVert. Little-endian host, same machine writes and reads.

#include <cstdint>

struct GhpcNativeDrawArgs;
struct GSPrimitiveBatch;

constexpr char kGhpcFixMagic[8] = {'G', 'H', 'P', 'C', 'F', 'I', 'X', '1'};

struct FixFileHeader
{
    char magic[8];
    uint32_t version;
    uint32_t drawCount;
};

struct FixDrawHead
{
    uint32_t mesh;
    uint32_t vertCount;
    uint32_t faceCount;
    uint32_t skinned;
    float bone[4][4][4];
    float world[4][4];
    float cam[8][4];
    float visX0, visX1, visY0, visY1;
    uint32_t refCount;
    uint32_t pad;
};

// One vertex as the PS2 path submitted it, read off the GS primitive batch.
struct FixRefVert
{
    float x, y;
    double z;
    float q, s, t;
    uint32_t rgba;
    uint32_t prim;
};

// True when GHPC_FIXTURE is set and the capture has not filled up.
bool ghpcFixtureActive();

// Called from ghpcNativeDrawMscal once the draw's inputs are known. Closes
// the previous record and opens a new one.
void ghpcFixtureBeginDraw(const GhpcNativeDrawArgs &a,
                          float visX0, float visX1, float visY0, float visY1);

// Called from the GS frontend for every primitive VU1 kicks while a record is
// open. These are the oracle.
void ghpcFixtureNoteSubmit(const GSPrimitiveBatch &b);

#endif
