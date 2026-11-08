# Snapdragon Game Super Resolution 1 (SGSR 1)

From https://github.com/SnapdragonStudios/snapdragon-gsr (commit d926f07, 2025-10-16),
`sgsr/v1/include/glsl/sgsr1_shader_mobile_edge_direction.frag`: Qualcomm's single-pass spatial upscaler (12-tap
Lanczos-like filter with adaptive sharpening), in RGBA mode with UseEdgeDirection on. BSD-3-Clause (LICENSE,
Qualcomm Innovation Center).

`sgsr1_glsl.h` holds it as a C string for the runtime's post passes (runtime/nvn/vulkan/shaders.c, exec.c
scene_post), changed from the original only where the runtime differs:

- the input is a combined image sampler at binding 0 and ViewportInfo is a push constant (with the output size: the
  texture coordinate comes from gl_FragCoord, the post passes have no varyings);
- the 3D scene is linear light, so the taps are taken to gamma 2.0 (sqrt) and the result squared back (b.z = 1): the
  edge threshold and the clamps are in 8-bit perceptual steps, as FSR 1's EASU is treated here.
