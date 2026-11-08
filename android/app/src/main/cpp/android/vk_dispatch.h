// android/vk_dispatch.c: the Vulkan library in use and GPU max clocks
#pragma once
#include <stdbool.h>
const char *vk_driver_name(void);  // "system", or the installed driver's name (its meta.json)
bool vk_driver_custom(void);
void vk_gpu_turbo(bool on);        // hold the GPU at its top clock (KGSL power control), or let it go

// android/android_bridge.c: Video > GPU driver > Install driver from .zip (the system file picker, MainActivity)
#include <stddef.h>
bool android_pick_driver_zip(void);
bool android_driver_install_poll(char *name, size_t name_cap, char *error, size_t error_cap);
