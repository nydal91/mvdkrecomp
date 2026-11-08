#version 450
// Snapdragon GSR 2, 2-pass fragment variant: Upscale (sgsr2_upscale.fs). Copyright (c) 2024, Qualcomm Innovation
// Center, Inc. SPDX-License-Identifier: BSD-3-Clause. Changed for SavageWind: third_party/sgsr2/VENDORED.md.
// t0: the previous output (the history, linear), t1: Convert's output, t2: the scene (linear, render size).
// a = (renderSize, renderSizeRcp), b = (outputSize, outputSizeRcp), c = (jitterOffset, scaleRatio),
// d = (minLerpContribution, reset, -, -).
layout(set = 0, binding = 0) uniform sampler2D PrevOutput;
layout(set = 0, binding = 1) uniform sampler2D MotionDepthClipAlphaBuffer;
layout(set = 0, binding = 2) uniform sampler2D InputColor;
layout(push_constant) uniform P { vec4 a; vec4 b; vec4 c; vec4 d; } pc;
layout(location = 0) out vec4 Output;
float FastLanczos(float base) {
    float y = base - 1.0;
    float y2 = y * y;
    float y_temp = 0.75 * y + y2;
    return y_temp * y2;
}
vec3 tap(ivec2 p) { return sqrt(max(texelFetch(InputColor, clamp(p, ivec2(0), ivec2(pc.a.xy) - 1), 0).rgb, vec3(0.0))); }
void main() {
    vec2 renderSize = pc.a.xy, renderSizeRcp = pc.a.zw, outputSize = pc.b.xy, jitterOffset = pc.c.xy;
    float Biasmax_viewportXScale = pc.c.z;
    float scalefactor = pc.c.w;
    vec2 Hruv = gl_FragCoord.xy * pc.b.zw;
    vec2 Jitteruv = clamp(Hruv + jitterOffset * renderSizeRcp, 0.0, 1.0);
    ivec2 InputPos = ivec2(Jitteruv * renderSize);
    vec3 mda = textureLod(MotionDepthClipAlphaBuffer, Jitteruv, 0.0).xyz;
    vec2 Motion = mda.xy;
    vec2 PrevUV = clamp(vec2(-0.5 * Motion.x, 0.5 * Motion.y) + Hruv, 0.0, 1.0);  // (REQUEST_NDC_Y_UP)
    float depthfactor = mda.z;
    vec3 HistoryColor = sqrt(max(textureLod(PrevOutput, PrevUV, 0.0).rgb, vec3(0.0)));

    vec4 Upsampledcw = vec4(0.0);
    float biasmax = Biasmax_viewportXScale;
    float biasmin = max(1.0, 0.3 + 0.3 * biasmax);
    float biasfactor = 0.25 * depthfactor;
    float kernelbias = mix(biasmax, biasmin, biasfactor);
    float motion_viewport_len = length(Motion * outputSize);
    float curvebias = mix(-2.0, -3.0, clamp(motion_viewport_len * 0.02, 0.0, 1.0));
    vec3 rectboxcenter = vec3(0.0), rectboxvar = vec3(0.0);
    float rectboxweight = 0.0;
    vec2 srcpos = vec2(InputPos) + vec2(0.5) - jitterOffset;
    kernelbias *= 0.5;
    float kernelbias2 = kernelbias * kernelbias;
    vec2 srcpos_srcOutputPos = srcpos - Hruv * renderSize;
    vec3 rectboxmin = vec3(1e30), rectboxmax = vec3(-1e30);
    // (the original's five taps, unrolled there: top, right, left, centre, bottom)
    const ivec2 offs[5] = ivec2[5](ivec2(0, 1), ivec2(1, 0), ivec2(-1, 0), ivec2(0, 0), ivec2(0, -1));
    for (int i = 0; i < 5; i++) {
        vec3 samplecolor = tap(InputPos + offs[i]);
        vec2 baseoffset = srcpos_srcOutputPos + vec2(offs[i]);
        float baseoffset_dot = dot(baseoffset, baseoffset);
        float base = clamp(baseoffset_dot * kernelbias2, 0.0, 1.0);
        float weight = FastLanczos(base);
        Upsampledcw += vec4(samplecolor * weight, weight);
        float boxweight = exp(baseoffset_dot * curvebias);
        rectboxmin = min(rectboxmin, samplecolor);
        rectboxmax = max(rectboxmax, samplecolor);
        vec3 wsample = samplecolor * boxweight;
        rectboxcenter += wsample;
        rectboxvar += (samplecolor * wsample);
        rectboxweight += boxweight;
    }
    rectboxweight = 1.0 / rectboxweight;
    rectboxcenter *= rectboxweight;
    rectboxvar *= rectboxweight;
    rectboxvar = sqrt(abs(rectboxvar - rectboxcenter * rectboxcenter));

    Upsampledcw.xyz = clamp(Upsampledcw.xyz / Upsampledcw.w, rectboxmin - vec3(0.075), rectboxmax + vec3(0.075));
    Upsampledcw.w = Upsampledcw.w * (1.0 / 3.0);

    float baseupdate = 1.0 - depthfactor;
    baseupdate = min(baseupdate, mix(baseupdate, Upsampledcw.w * 10.0, clamp(10.0 * motion_viewport_len, 0.0, 1.0)));
    baseupdate = min(baseupdate, mix(baseupdate, Upsampledcw.w, clamp(motion_viewport_len * 0.05, 0.0, 1.0)));
    float basealpha = baseupdate;

    const float EPSILON = 1.192e-07;
    float boxscale = max(depthfactor, clamp(motion_viewport_len * 0.05, 0.0, 1.0));
    float boxsize = mix(scalefactor, 1.0, boxscale);
    vec3 sboxvar = rectboxvar * boxsize;
    vec3 boxmin = rectboxcenter - sboxvar;
    vec3 boxmax = rectboxcenter + sboxvar;
    rectboxmax = min(rectboxmax, boxmax);
    rectboxmin = max(rectboxmin, boxmin);

    vec3 clampedcolor = clamp(HistoryColor, rectboxmin, rectboxmax);
    float startLerpValue = pc.d.x;
    if ((abs(mda.x) + abs(mda.y)) > 0.000001) startLerpValue = 0.0;
    float lerpcontribution = (any(greaterThan(rectboxmin, HistoryColor)) || any(greaterThan(HistoryColor, rectboxmax))) ? startLerpValue : 1.0;

    HistoryColor = mix(clampedcolor, HistoryColor, clamp(lerpcontribution, 0.0, 1.0));
    float basemin = min(basealpha, 0.1);
    basealpha = mix(basemin, basealpha, clamp(lerpcontribution, 0.0, 1.0));

    float alphasum = max(EPSILON, basealpha + Upsampledcw.w);
    float alpha = clamp(Upsampledcw.w / alphasum + pc.d.y, 0.0, 1.0);
    vec3 col = pc.d.y > 0.5 ? Upsampledcw.xyz : mix(HistoryColor, Upsampledcw.xyz, alpha);
    Output = vec4(col * col, 1.0);
}
