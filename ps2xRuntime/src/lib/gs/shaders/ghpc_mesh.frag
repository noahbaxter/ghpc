#version 450

// The GS's texture function and alpha test, on the material and texture the
// Rnd seam hands over.
//
// Arithmetic stays in the GS's own units, 0..255 with 0x80 meaning 1.0, so
// the TFX equations and the alpha test read exactly as the GS manual writes
// them. The texture carries raw GS alpha for the same reason.
//
// The vertex colour is VU1's lit colour, reproduced in ghpc_mesh.vert. Only
// the unlit control arm (GHPC_VK_LIGHT=0) falls back to mColor, and there an
// untextured draw keeps the legibility shade step 3 had, since without it a
// stack of overlapping untextured meshes is one silhouette.

layout(location = 0) in vec3 vNorm;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec4 vColor;
layout(location = 3) flat in uint vMode;

layout(set = 1, binding = 0) uniform sampler2D uTex;

layout(location = 0) out vec4 outColor;

// vMode, packed by the backend from scene::Material:
//   bit  0      textured
//   bits 1..2   TFX: 0 modulate, 1 decal, 2 highlight, 3 highlight2
//   bit  3      ATE
//   bits 4..6   ATST: 0 never, 1 always, 2 less, 3 lequal, 4 equal,
//               5 gequal, 6 greater, 7 notequal
//   bits 7..14  AREF
//   bits 15..16 AFAIL: 0 keep, 1 fb only, 2 zb only, 3 rgb only
//   bit  17     CLAMP_1 WMS clamp
//   bit  18     CLAMP_1 WMT clamp
//   bit  19     blended
//   bit  20     draw-ID output, GHPC_VK_MESH_ID
//   bit  21     vertex colour is VU1's lit colour, not mColor
bool alphaPasses(uint atst, float a, float aref)
{
    switch (atst)
    {
    case 0u: return false;
    case 1u: return true;
    case 2u: return a < aref;
    case 3u: return a <= aref;
    case 4u: return a == aref;
    case 5u: return a >= aref;
    case 6u: return a > aref;
    default: return a != aref;
    }
}

void main()
{
    // The vertex colour in GS units. mColor's 1.0 is the GS's 0x80, which is
    // what makes modulate by a white material the identity.
    vec3 cv = vColor.rgb * 128.0;
    float av = vColor.a * 128.0;

    vec3 c;
    float a;
    if ((vMode & 1u) != 0u)
    {
        vec2 uv = vUv;
        // Repeat is the sampler's; clamp is done here, to the texel centres,
        // so one sampler serves every material.
        vec2 size = vec2(textureSize(uTex, 0));
        vec2 lo = 0.5 / size;
        if ((vMode & (1u << 17)) != 0u)
        {
            uv.x = clamp(uv.x, lo.x, 1.0 - lo.x);
        }
        if ((vMode & (1u << 18)) != 0u)
        {
            uv.y = clamp(uv.y, lo.y, 1.0 - lo.y);
        }
        vec4 t = texture(uTex, uv) * 255.0;

        switch ((vMode >> 1) & 3u)
        {
        case 0u: // modulate
            c = t.rgb * cv / 128.0;
            a = t.a * av / 128.0;
            break;
        case 1u: // decal
            c = t.rgb;
            a = t.a;
            break;
        case 2u: // highlight
            c = t.rgb * cv / 128.0 + av;
            a = t.a + av;
            break;
        default: // highlight2
            c = t.rgb * cv / 128.0 + av;
            a = t.a;
            break;
        }
        c = min(c, vec3(255.0));
        a = min(floor(a + 0.5), 255.0);
    }
    else if ((vMode & (1u << 21)) != 0u)
    {
        // Lit and untextured: the GS writes the vertex colour as it is, so
        // 0x80 is half intensity here, not 1.0.
        c = min(cv, vec3(255.0));
        a = av;
    }
    else
    {
        vec3 n = normalize(vNorm);
        // abs() rather than max(..., 0): the normal is in world space and the
        // direction here is arbitrary, and a one-sided product put 87% of the
        // first R2 frame at the floor value, which read as missing geometry.
        float lambert = abs(dot(n, vec3(0.0, 0.0, -1.0)));
        c = vColor.rgb * 255.0 * (0.25 + 0.75 * lambert);
        a = av;
    }

    if ((vMode & (1u << 3)) != 0u)
    {
        uint atst = (vMode >> 4) & 7u;
        float aref = float((vMode >> 7) & 0xFFu);
        if (!alphaPasses(atst, a, aref))
        {
            // A failed pixel still writes colour under fb-only and rgb-only,
            // and only depth under zb-only. A discard is exact for keep. For
            // the colour-writing modes it is exact when the draw blends and
            // the source alpha is zero, since the blend then leaves the
            // destination as it was; otherwise the pixel is drawn, and it
            // writes depth the GS would not have. Zb-only is discarded,
            // losing a depth write the GS would have made.
            uint afail = (vMode >> 15) & 3u;
            bool blended = (vMode & (1u << 19)) != 0u;
            if (afail == 0u || afail == 2u || (blended && a == 0.0))
            {
                discard;
            }
        }
    }

    // GHPC_VK_MESH_ID: the draw's index, exactly, after the alpha test so a
    // punched-out texel does not claim a pixel it would not have drawn.
    if ((vMode & (1u << 20)) != 0u)
    {
        outColor = vec4(vColor.rgb, 1.0);
        return;
    }

    outColor = vec4(c / 255.0, min(a / 128.0, 1.0));
}
