#pragma once

#include <cstdint>

// The host mesh cache: RndMesh Vert[] and Face[] copied out at PsMesh::Sync,
// keyed by the PsMesh, because the guest frees its own vectors right after and
// DrawFaces has nothing else to read. Filled and read by the Sync and
// DrawFaces overrides, both on the guest thread.
//
// It lives in the runtime rather than in the Sync override so a save state can
// carry it. It is host state the guest's RAM implies but does not hold: a
// state restored without it resumes after every Sync has already run, so the
// cache stays empty and the Rnd seam serves no draws at all.
//
// No STL here, for the same reason as ghpc_scene.h: generated translation
// units include it.
namespace ghpc
{
    namespace meshcache
    {
        // Replaces the entry for `self`. `verts` is vertCnt * 64 bytes in the
        // guest's layout, `faces` faceCnt * 6 bytes, three u16 each.
        void store(uint32_t self, const uint8_t *verts, uint32_t vertCnt,
                   const uint8_t *faces, uint32_t faceCnt);

        // True on a hit. Out pointers may be null, and stay valid until the
        // next store for the same `self`.
        bool lookup(uint32_t self, uint32_t *vertCnt, uint32_t *faceCnt,
                    const uint8_t **verts, const uint8_t **faces);

        // Changes on every store, for any key. `self` is a guest address, and
        // a menu that unloads frees it for the next asset: anything cached
        // downstream by `self` alone serves the old mesh under the new one's
        // transform. Downstream caches key on (self, generation) instead.
        // 0 when there is no entry.
        uint32_t generation(uint32_t self);

        // Drops the entry for `self`, true when there was one. Called when a
        // new PsMesh is constructed at `self`: a Sync only stores for an
        // owner that is not mutable and has verts, so without this a mutable
        // mesh, an instance or an empty mesh at a reused address was served
        // the previous occupant's geometry.
        bool erase(uint32_t self);
    }
}
