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
// std430: the four mat4s at 0, the colour at 256, the count at 272, the mode
// at 276, the UV scale at 280, then the lighting inputs from 288, for a
// stride of 512. The colour lives here rather than in the push
// constants because those are already at 116 of the 128 bytes every
// implementation guarantees, and the camera has the better claim on them.
struct DrawData
{
    mat4 m[4];
    vec4 color; // PsMat mColor at +0x30
    uint matrixCount;
    uint mode;      // texture and alpha-test state, packed; see ghpc_mesh.frag
    vec2 uvScale;   // the GS's 2^TW x 2^TH texture space into the image's
    mat4 lightMat;  // qw676..679
    vec4 lights[7]; // qw681..687, PsEnviron::Select's block
    vec4 rgbaScale; // qw695
    vec4 vuColor;   // qw690
    uint lightMode; // 0 unlit (mColor), 1 passthrough, 2 ambient, 3 directional, 4 point
    uint lightFlags;// bit 0 prelit, bit 1 mUseEnviron
    uint pad0;
    uint pad1;
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

// VU1's vertex colour, in its units (1.0 = the GS's 0x80 after qw695),
// reproducing the three lighting overlays qw688.x selects:
//
//   0x7C5 (overlay 0x3e28) ambient: base only
//   0x6EC (overlay 0x3760) three directional lights
//   0x436 (overlay 0x21b0) one point light with a range
//
// where base is mColor (qw690), times ambient (qw681) under mUseEnviron, or
// for a prelit material the vertex's own colour times ambient (or 1). Every
// program clamps to 1 with MINIw before the T&L loop multiplies by qw695.
// Row-major matrices read as GLSL mat4 come out as their rows, so lm[i] is
// row i: the object normal goes to world as n.x*lm[0] + n.y*lm[1] + n.z*lm[2].
vec4 vuLitColour(DrawData d)
{
    bool prelit = (d.lightFlags & 1u) != 0u;
    bool env = (d.lightFlags & 2u) != 0u;
    vec4 ambient = d.lights[0];

    if (d.lightMode == 1u)
    {
        return aWeights;
    }

    vec4 base;
    if (prelit)
    {
        // 0x3760's prelit path: ambient, or (1, 1, 1, ambient.w) without the
        // environment, times the vertex colour.
        vec4 scale = env ? ambient : vec4(1.0, 1.0, 1.0, ambient.w);
        base = scale * aWeights;
    }
    else
    {
        base = env ? d.vuColor * ambient : d.vuColor;
    }

    if (d.lightMode == 2u)
    {
        return min(base, vec4(1.0));
    }

    mat4 lm = d.lightMat;
    if (d.lightMode == 3u)
    {
        vec3 worldN = aNorm.x * lm[0].xyz + aNorm.y * lm[1].xyz + aNorm.z * lm[2].xyz;
        // qw685..687 hold the three directions as columns.
        vec3 dots = worldN.x * d.lights[4].xyz + worldN.y * d.lights[5].xyz + worldN.z * d.lights[6].xyz;
        dots = max(dots, vec3(0.0));
        vec4 c = base;
        c += d.lights[1] * d.vuColor * dots.x;
        c += d.lights[2] * d.vuColor * dots.y;
        c += d.lights[3] * d.vuColor * dots.z;
        return min(c, vec4(1.0));
    }

    // Point: the light position is column 0 of qw685..687, taken relative to
    // the object origin qw679 and into object space through the rows. No
    // clamp at zero on the dot in the overlay; the final clamp is here.
    vec3 rel = vec3(d.lights[4].x, d.lights[5].x, d.lights[6].x) - lm[3].xyz;
    vec3 p = vec3(dot(lm[0].xyz, rel), dot(lm[1].xyz, rel), dot(lm[2].xyz, rel));
    vec3 toLight = p - aPos;
    float d2 = dot(toLight, toLight);
    vec4 c = base;
    if (d2 <= d.lights[5].w && d2 > 0.0)
    {
        float nd = dot(toLight, aNorm);
        c += d.lights[1] * d.vuColor * nd * (d.lights[4].w + inversesqrt(d2));
    }
    return clamp(c, vec4(0.0), vec4(1.0));
}

layout(location = 0) out vec3 vNorm;
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec4 vColor;
layout(location = 3) flat out uint vMode;

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
    // Scaled here rather than per fragment because it is linear, so it
    // interpolates the same either way.
    vUv = aUv * d.uvScale;
    if (d.lightMode != 0u)
    {
        // FTOI0(qw695 * colour), which the GS then takes as 8 bits, and the
        // fragment shader wants it back with 0x80 as 1.0.
        vec4 gs = min(vuLitColour(d) * d.rgbaScale, vec4(255.0));
        vColor = gs / 128.0;
    }
    else
    {
        vColor = d.color;
    }
    vMode = d.mode;
}
