// Does the Vulkan vertex shader compute the transform that was measured?
//
// The transform in ghpc_transform.h is calibrated to GS subpixel over 171
// matched pairs (evidence/2026-09-12-bone-palette.txt) and divides by w on the
// host. The shader in src/lib/gs/shaders/ghpc_mesh.vert deliberately does not:
// it hands the rasteriser a homogeneous coordinate so the hardware does the
// near-plane clipping and the perspective-correct interpolation that a host
// divide throws away. That is a different expression of the same transform,
// and "the same" is a claim that should be checked rather than asserted.
//
// So this reimplements the shader's arithmetic in C++, including the two
// things easiest to get wrong by reasoning alone:
//
//   - the matrix layout. The seam hands over row-major float[16] and the
//     shader reads it as a GLSL mat4, which is column major. That transposes
//     it, which is exactly what turns `M * v` into the row-vector `v * M` the
//     measurement calls for. No transpose appears in the shader because the
//     layout mismatch IS the transpose.
//   - the screen mapping. The measurement lands in GS pixels, 2048-centred,
//     and the rasteriser subtracts XYOFFSET before it tests the scissor. The
//     shader folds that subtraction and the NDC scale into the homogeneous
//     coordinate, so it has to come back out to the same pixel.
//
// Then it divides the shader's result and compares it to the host transform's,
// on random inputs shaped like the real ones. A sign error, a transposed
// matrix or a dropped offset all fail; agreement to float precision is the
// pass.
//
//   c++ -std=c++17 -O2 -I ps2xRuntime/include -o /tmp/vkxform ghpc/tools/vkxform.cpp
//   /tmp/vkxform

#include "runtime/gs/ghpc_transform.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>

namespace
{
    struct Vec4
    {
        float x, y, z, w;
    };

    // What `mat4(data) * vec4(v)` evaluates to in GLSL when `data` is a
    // row-major float[16] sitting in a std430 buffer. GLSL reads column c of
    // the mat4 from data[c*4 .. c*4+3], so component r of the product is
    // sum over c of data[c*4+r] * v[c]. That is the row-vector product
    // `v * data` with data read row major, which is the measured convention.
    Vec4 glslMul(const float *data, const Vec4 &v)
    {
        Vec4 out{0.0f, 0.0f, 0.0f, 0.0f};
        float *o = &out.x;
        const float *vp = &v.x;
        for (int r = 0; r < 4; ++r)
        {
            float acc = 0.0f;
            for (int c = 0; c < 4; ++c)
            {
                acc += data[c * 4 + r] * vp[c];
            }
            o[r] = acc;
        }
        return out;
    }

    struct ShaderResult
    {
        // Screen pixels, after the divide the rasteriser would do. This is
        // what has to match the host transform minus XYOFFSET.
        float px, py;
        float ndcZ;
        float w;
    };

    // ghpc_mesh.vert, line for line, with the GLSL semantics spelled out.
    ShaderResult runShader(const uint8_t *vert, const GhpcNativeDrawArgs &a,
                           float halfW, float halfH, float ofx, float ofy)
    {
        float f[16];
        std::memcpy(f, vert, sizeof(f));

        const Vec4 p{f[0], f[1], f[2], 1.0f};

        Vec4 world{0.0f, 0.0f, 0.0f, 0.0f};
        if (a.skinned)
        {
            for (int b = 0; b < 4; ++b)
            {
                const float wt = f[8 + b];
                if (wt == 0.0f)
                {
                    continue;
                }
                const Vec4 wp = glslMul(&a.bone[b][0][0], p);
                world.x += wt * wp.x;
                world.y += wt * wp.y;
                world.z += wt * wp.z;
            }
        }
        else
        {
            const Vec4 wp = glslMul(&a.world[0][0], p);
            world = Vec4{wp.x, wp.y, wp.z, 0.0f};
        }

        const Vec4 clip = glslMul(&a.cam[4][0], Vec4{world.x, world.y, world.z, 1.0f});

        const float scaleX = a.cam[0][0];
        const float scaleY = a.cam[0][1];
        const float scaleZ = a.cam[0][2];
        const float offsetX = a.cam[1][0];
        const float offsetY = a.cam[1][1];
        const float offsetZ = a.cam[1][2];

        const float screenWx = clip.x * scaleX + (offsetX - ofx - halfW) * clip.w;
        const float screenWy = clip.y * scaleY + (offsetY - ofy - halfH) * clip.w;
        const float depthW = clip.z * scaleZ + offsetZ * clip.w;

        // gl_Position, then the rasteriser's own divide and viewport map.
        const float gx = screenWx / halfW;
        const float gy = screenWy / halfH;
        const float gz = depthW * (1.0f / 16777216.0f);
        const float gw = clip.w;

        ShaderResult r{};
        r.w = gw;
        r.px = (gx / gw + 1.0f) * halfW;
        r.py = (gy / gw + 1.0f) * halfH;
        r.ndcZ = gz / gw;
        return r;
    }

    void randomMatrix(std::mt19937 &rng, float m[4][4], bool projection)
    {
        std::uniform_real_distribution<float> small(-2.0f, 2.0f);
        std::uniform_real_distribution<float> trans(-400.0f, 400.0f);
        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                m[r][c] = small(rng);
            }
        }
        if (projection)
        {
            // Keep w positive and away from zero, the way a real frustum does
            // in front of the near plane. A vertex behind the eye has no
            // screen position at all and the host transform refuses it, so it
            // is not a case the two can be compared on.
            m[3][3] = 900.0f;
            return;
        }
        // An object transform's rows carry stale packet content in the w lane,
        // which the measured transform never reads. Filling it with garbage is
        // the point: if either side reads it, they disagree.
        m[3][0] = trans(rng);
        m[3][1] = trans(rng);
        m[3][2] = trans(rng);
        for (int r = 0; r < 3; ++r)
        {
            m[r][3] = 12345.0f;
        }
    }
}

int main()
{
    std::mt19937 rng(0x6a2c01u);
    std::uniform_real_distribution<float> pos(-200.0f, 200.0f);
    std::uniform_real_distribution<float> uv(0.0f, 1.0f);
    std::uniform_real_distribution<float> weight(0.0f, 1.0f);

    // The real gameplay camera, from the 56 verified PsCam::Select calls
    // (notes/rnd-seam.md): qw696 viewport scale, qw697 viewport offset.
    const float kScale[3] = {2007.84f, 2007.84f, -29490.8f};
    const float kOffset[3] = {2048.0f, 2048.0f, 29490.8f};

    // A 512x448 framebuffer, which is what gameplay reports, with the
    // XYOFFSET the GS actually carries there. 2048 - 512/2 = 1792 and
    // 2048 - 448/2 = 1824, which is what the seam measures at runtime.
    const float halfW = 256.0f;
    const float halfH = 224.0f;
    const float ofx = 1792.0f;
    const float ofy = 1824.0f;

    double worstXy = 0.0;
    double worstQ = 0.0;
    uint64_t compared = 0;
    uint64_t skippedBehindEye = 0;

    for (int trial = 0; trial < 20000; ++trial)
    {
        GhpcNativeDrawArgs a{};
        a.skinned = (trial & 1) != 0;
        randomMatrix(rng, a.world, false);
        for (int b = 0; b < 4; ++b)
        {
            randomMatrix(rng, a.bone[b], false);
        }

        float camRows[4][4];
        randomMatrix(rng, camRows, true);
        for (int k = 0; k < 3; ++k)
        {
            a.cam[0][k] = kScale[k];
            a.cam[1][k] = kOffset[k];
        }
        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                a.cam[4 + r][c] = camRows[r][c];
            }
        }

        // One RndMesh::Vert: pos at 0x00, norm at 0x10, the four Hmx::Color
        // floats at 0x20 that are bone weights on a skinned mesh, uv at 0x30.
        uint8_t vert[64] = {};
        float f[16] = {};
        f[0] = pos(rng);
        f[1] = pos(rng);
        f[2] = pos(rng);
        float w0 = weight(rng), w1 = weight(rng), w2 = weight(rng), w3 = weight(rng);
        const float sum = w0 + w1 + w2 + w3;
        // Summing to 1.0 is a measured property of every gameplay vert, not a
        // convenience: the weights index the palette in order and a set that
        // does not sum to one is a misread field.
        f[8] = w0 / sum;
        f[9] = w1 / sum;
        f[10] = w2 / sum;
        f[11] = w3 / sum;
        f[12] = uv(rng);
        f[13] = uv(rng);
        std::memcpy(vert, f, sizeof(f));

        const GhpcScreen host = ghpcTransformVert(vert, a);
        if (!host.valid)
        {
            ++skippedBehindEye;
            continue;
        }

        const ShaderResult shader = runShader(vert, a, halfW, halfH, ofx, ofy);

        // The host transform lands in GS pixels; the rasteriser subtracts
        // XYOFFSET before it tests anything. That is the pixel the shader has
        // to produce.
        const double expectedX = (double)host.x - ofx;
        const double expectedY = (double)host.y - ofy;
        const double dx = std::fabs(expectedX - (double)shader.px);
        const double dy = std::fabs(expectedY - (double)shader.py);

        // Relative, because these coordinates run to thousands of pixels off
        // screen and an absolute tolerance would either pass everything or
        // fail on float rounding alone.
        const double denom = std::fmax(1.0, std::fmax(std::fabs(expectedX), std::fabs(expectedY)));
        worstXy = std::fmax(worstXy, std::fmax(dx, dy) / denom);

        const double q = 1.0 / (double)shader.w;
        worstQ = std::fmax(worstQ, std::fabs(q - (double)host.q) * std::fmax(1.0, std::fabs(1.0 / q)));
        ++compared;
    }

    std::printf("compared=%llu skippedBehindEye=%llu\n",
                (unsigned long long)compared, (unsigned long long)skippedBehindEye);
    std::printf("worst relative xy error = %.3e\n", worstXy);
    std::printf("worst relative q  error = %.3e\n", worstQ);

    // 1e-4 relative, which is four orders of magnitude inside GS subpixel at
    // these magnitudes. The two expressions are algebraically identical, so
    // anything left is float association order.
    const bool ok = compared > 0 && worstXy < 1.0e-4 && worstQ < 1.0e-4;
    std::printf("%s\n", ok ? "SHADER MATCHES THE MEASURED TRANSFORM"
                           : "SHADER DISAGREES WITH THE MEASURED TRANSFORM");
    return ok ? 0 : 1;
}
