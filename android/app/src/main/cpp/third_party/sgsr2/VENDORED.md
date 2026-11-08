# Snapdragon Game Super Resolution 2 (SGSR 2)

From https://github.com/SnapdragonStudios/snapdragon-gsr (commit d926f07, 2025-10-16), `sgsr/v2/include/glsl_2_pass_fs`:
the 2-pass fragment variant Qualcomm recommends for mobile. BSD-3-Clause (LICENSE, Qualcomm Innovation Center).

`sgsr2_glsl.h` holds both passes as C strings for the runtime's post passes (runtime/nvn/vulkan/shaders.c,
exec.c sgsr2_scene), changed from the originals only where the runtime differs:

- inputs are combined image samplers at bindings 0-2 and the parameters are push constants (no UBO);
- convert: the motion comes from the runtime's own vectors (vulkan/mvec.c: camera reprojection and per-object
  motion, filled everywhere), so the clipToPrevClip fallback is dropped; the depth taps are texelFetch (a depth view)
  in place of textureGather; NDC y up (REQUEST_NDC_Y_UP);
- upscale: the scene is linear light, so the colour taps and the history are taken to gamma 2.0 (sqrt) for the
  filter and the clamp box (the 0.075 margins assume perceptual values) and squared back on output; the history is
  ignored outright on a reset (it is undefined on the first frame).
