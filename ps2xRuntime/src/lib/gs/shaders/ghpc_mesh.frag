#version 450

// Step 3 has no material and no texture: those are step 4, from the two
// Select hooks already written. What this needs to do is make a shape legible,
// so a look at the frame says whether the venue and the fretboard arrived in
// the right place at the right size.
//
// Shaded off the interpolated normal rather than flat filled, because a flat
// fill turns a stack of overlapping meshes into one silhouette and hides every
// depth error in it.

layout(location = 0) in vec3 vNorm;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 n = normalize(vNorm);
    // Not lighting: a legibility shade, so a stack of overlapping meshes
    // reads as shapes instead of one silhouette.
    //
    // abs() rather than max(..., 0), and that is the fix for a real misread.
    // The normal is in world space and the light direction here is arbitrary,
    // so a surface whose normal points along +Z came out at the floor value
    // and 87% of the first R2 frame was one flat shade. That looked like
    // missing geometry and was a one-sided dot product. Real lighting is the
    // material's business.
    float lambert = abs(dot(n, vec3(0.0, 0.0, -1.0)));
    float shade = 0.25 + 0.75 * lambert;
    // The material's own colour, shaded. Not the texture: VU1 submits
    // per-vertex lit colour with ST/Q and the texture is still ahead, so this
    // is the material's tint standing in for both.
    outColor = vec4(vColor.rgb * shade, vColor.a);
}
