#version 450
// Snapdragon GSR 2, 2-pass fragment variant: Convert (sgsr2_convert.fs). Copyright (c) 2024, Qualcomm Innovation
// Center, Inc. SPDX-License-Identifier: BSD-3-Clause. Changed for SavageWind: third_party/sgsr2/VENDORED.md.
// t0: the scene depth ([0, 1], not inverted), t1: the motion vectors (.xy: this frame's NDC minus the previous
// frame's, y up). a = (renderSize, renderSizeRcp), b.x = cameraFovAngleHor.
layout(set = 0, binding = 0) uniform sampler2D InputDepth;
layout(set = 0, binding = 1) uniform sampler2D InputVelocity;
layout(push_constant) uniform P { vec4 a; vec4 b; } pc;
layout(location = 0) out vec4 MotionDepthClipAlphaBuffer;
float dep(ivec2 p) { return texelFetch(InputDepth, clamp(p, ivec2(0), ivec2(pc.a.xy) - 1), 0).r; }
float quad(ivec2 p) { return min(min(dep(p), dep(p + ivec2(1, 0))), min(dep(p + ivec2(0, 1)), dep(p + ivec2(1, 1)))); }
void main() {
    ivec2 InputPos = ivec2(gl_FragCoord.xy);
    float maxC = quad(InputPos);
    float depthclip = 0.0;
    if (maxC < 1.0 - 1.0e-05) {
        float btmLeft4 = quad(InputPos + ivec2(-1, -1));
        float btmRight4 = quad(InputPos + ivec2(1, -1));
        float topLeft4 = quad(InputPos + ivec2(-1, 1));
        float topRight4 = quad(InputPos + ivec2(1, 1));
        float Wdepth = 0.0;
        float Ksep = 1.37e-05;
        float Kfov = pc.b.x;
        float diagonal_length = length(pc.a.xy);
        float Ksep_Kfov_diagonal = Ksep * Kfov * diagonal_length;
        float Depthsep = Ksep_Kfov_diagonal * (1.0 - maxC);
        float EPSILON = 1.19e-07;
        Wdepth += clamp((Depthsep / (abs(maxC - btmLeft4) + EPSILON)), 0.0, 1.0);
        Wdepth += clamp((Depthsep / (abs(maxC - btmRight4) + EPSILON)), 0.0, 1.0);
        Wdepth += clamp((Depthsep / (abs(maxC - topLeft4) + EPSILON)), 0.0, 1.0);
        Wdepth += clamp((Depthsep / (abs(maxC - topRight4) + EPSILON)), 0.0, 1.0);
        depthclip = clamp(1.0 - Wdepth * 0.25, 0.0, 1.0);
    }
    vec2 motion = texelFetch(InputVelocity, InputPos, 0).xy;
    MotionDepthClipAlphaBuffer = vec4(motion, depthclip, 0.0);
}
