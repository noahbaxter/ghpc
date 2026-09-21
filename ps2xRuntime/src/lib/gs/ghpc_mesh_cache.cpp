#include "ghpc_mesh_cache.h"

#include "runtime/ghpc_state.h"

#include <cstdio>
#include <unordered_map>
#include <vector>

namespace ghpc
{
    namespace meshcache
    {
        namespace
        {
            struct Entry
            {
                std::vector<uint8_t> verts;
                std::vector<uint8_t> faces;
                uint32_t vertCnt = 0u;
                uint32_t faceCnt = 0u;
            };

            std::unordered_map<uint32_t, Entry> &cache()
            {
                static std::unordered_map<uint32_t, Entry> s_map;
                return s_map;
            }
        }

        void store(uint32_t self, const uint8_t *verts, uint32_t vertCnt,
                   const uint8_t *faces, uint32_t faceCnt)
        {
            Entry e;
            e.verts.assign(verts, verts + (size_t)vertCnt * 64u);
            if (faces != nullptr && faceCnt != 0u)
            {
                e.faces.assign(faces, faces + (size_t)faceCnt * 6u);
            }
            e.vertCnt = vertCnt;
            e.faceCnt = (faces != nullptr) ? faceCnt : 0u;
            cache()[self] = std::move(e);
        }

        bool lookup(uint32_t self, uint32_t *vertCnt, uint32_t *faceCnt,
                    const uint8_t **verts, const uint8_t **faces)
        {
            auto &m = cache();
            auto it = m.find(self);
            const bool hit = it != m.end();
            if (vertCnt) *vertCnt = hit ? it->second.vertCnt : 0u;
            if (faceCnt) *faceCnt = hit ? it->second.faceCnt : 0u;
            if (verts) *verts = hit ? it->second.verts.data() : nullptr;
            if (faces) *faces = hit ? it->second.faces.data() : nullptr;
            return hit;
        }
    }
}

// Entry count, then per entry: key, vertCnt, faceCnt, the vert bytes and the
// face bytes. Sizes are implied by the counts.
void ghpcMeshCacheSerializeState(GhpcStateWriter &w)
{
    const auto &m = ghpc::meshcache::cache();
    w.u32((uint32_t)m.size());
    for (const auto &kv : m)
    {
        w.u32(kv.first);
        w.u32(kv.second.vertCnt);
        w.u32(kv.second.faceCnt);
        w.raw(kv.second.verts.data(), kv.second.verts.size());
        w.raw(kv.second.faces.data(), kv.second.faces.size());
    }
}

bool ghpcMeshCacheDeserializeState(GhpcStateReader &r)
{
    auto &m = ghpc::meshcache::cache();
    m.clear();
    uint32_t count = 0u;
    if (!r.u32(count))
    {
        return false;
    }
    for (uint32_t i = 0u; i < count; ++i)
    {
        uint32_t key = 0u;
        ghpc::meshcache::Entry e;
        if (!r.u32(key) || !r.u32(e.vertCnt) || !r.u32(e.faceCnt))
        {
            return false;
        }
        e.verts.resize((size_t)e.vertCnt * 64u);
        e.faces.resize((size_t)e.faceCnt * 6u);
        if (!r.raw(e.verts.data(), e.verts.size()) ||
            (!e.faces.empty() && !r.raw(e.faces.data(), e.faces.size())))
        {
            return false;
        }
        m[key] = std::move(e);
    }
    std::fprintf(stderr, "[ghpc/state] mesh cache restored, %u meshes\n", count);
    return true;
}
