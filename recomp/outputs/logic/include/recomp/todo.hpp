// Marker for functions whose bodies haven't been recompiled yet. Reaching one at runtime
// logs the original address and aborts, so the next function to port is obvious.
#pragma once

#include <cstdint>

namespace recomp {
[[noreturn]] void todo(uint64_t original_address, const char* function);
}  // namespace recomp

#define RECOMP_TODO(addr) ::recomp::todo((addr), __PRETTY_FUNCTION__)
