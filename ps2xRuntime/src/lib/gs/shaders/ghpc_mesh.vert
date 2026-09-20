#version 450

// The Rnd seam's vertex transform, measured rather than derived.
// ghpc/notes/evidence/2026-09-12-bone-palette.txt, 171 matched pairs, inside
// GS subpixel:
//
//     world = sum over b of weight[b] * (pos * Bone[b])   skinned, b = 0..3
//     world = pos * World                                 unskinned
//     clip  = world * M              M = qw700..703 as rows, row vectors
//     q     = 1 / clip.w
//     x,y,z = clip.xyz * q * qw696.xyz + qw697.xyz         GS pixels
//
// Row vectors, and the matrices arrive row major. A row-major float[16] read
// as a GLSL mat4 comes out transposed, because GLSL reads a mat4 column major,
// so `M * v` here is exactly `v * M` in the row-vector convention above. That
// is not a coincidence to be tidied up: it is why no transpose appears.
//
// The divide by w is deliberately NOT done here. Handing the rasteriser the
// homogeneous coordinate instead gets hardware near-plane clipping and
// perspective-correct interpolation for free, where the C++ transform in
// ghpc_transform.h has to drop any vertex with w <= 0 because it cannot
// generate one.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNorm;
layout(location = 2) in vec4 aWeights; // RndMesh::Vert's Hmx::Color at +0x20
layout(location = 3) in vec2 aUv;

// One entry per draw. Four matrices always: a skinned draw uses all of them,
// an unskinned draw uses matrix 0 and reports matrixCount 1.
// std430: the four mat4s at 0, the colour at 256, the count at 272, and the
// tail pads the stride to 288. The colour lives here rather than in the push
// constants because those are already at 116 of the 128 bytes every
// implementation guarantees, and the camera has the better claim on them.
struct DrawData
{
    mat4 m[4];
    vec4 color; // PsMat mColor at +0x30
    uint matrixCount;
    uint pad0;
    uint pad1;
    uint pad2;
};

layout(std430, set = 0, binding = 0) readonly buffer Draws
{
    DrawData draws[];
};

layout(push_constant) uniform Push
{
    mat4 cam;      // qw700..703
    vec4 scale;    // qw696, w unused
    vec4 offset;   // qw697, w unused
    vec4 viewport; // xy = half the framebuffer in pixels, zw = GS XYOFFSET
    uint drawIndex;
} push;

layout(location = 0) out vec3 vNorm;
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec4 vColor;

void main()
{
    DrawData d = draws[push.drawIndex];

    vec4 p = vec4(aPos, 1.0);
    vec3 world;
    vec3 worldNorm;
    if (d.matrixCount > 1u)
    {
        world = vec3(0.0);
        worldNorm = vec3(0.0);
        for (int b = 0; b < 4; ++b)
        {
            float wt = aWeights[b];
            if (wt == 0.0)
            {
                continue;
            }
            world += wt * (d.m[b] * p).xyz;
            // The normal goes through the rotation only, so w is zero. The
            // matrices are not orthonormal in general and this is not the
            // inverse transpose, which is wrong for a non-uniform scale; it
            // is good enough to shade a shape and step 4 replaces it with
            // the material.
            worldNorm += wt * (d.m[b] * vec4(aNorm, 0.0)).xyz;
        }
    }
    else
    {
        world = (d.m[0] * p).xyz;
        worldNorm = (d.m[0] * vec4(aNorm, 0.0)).xyz;
    }

    vec4 clip = push.cam * vec4(world, 1.0);

    // GS pixel coordinates, before the divide:
    //     gsXy * clip.w = clip.xy * scale.xy + offset.xy * clip.w
    // and the rasteriser subtracts XYOFFSET before it tests the scissor
    // (gs_frontend.cpp:2137), so the same subtraction happens here. Then to
    // NDC: ndc = screen / half - 1, which is the same as
    //     (screen - half) / half
    // and multiplying through by clip.w keeps it homogeneous.
    vec2 screenW = clip.xy * push.scale.xy +
                   (push.offset.xy - push.viewport.zw - push.viewport.xy) * clip.w;

    // Depth keeps the GS's own sense: z counts up towards the viewer, so the
    // pipeline compares GREATER and clears to zero. 24-bit Z is the GS's
    // range, and 1/2^24 is what normalises it into Vulkan's [0, w].
    float depthW = clip.z * push.scale.z + push.offset.z * clip.w;

    gl_Position = vec4(screenW / push.viewport.xy,
                       depthW * (1.0 / 16777216.0),
                       clip.w);

    vNorm = worldNorm;
    vUv = aUv;
    vColor = d.color;
}
