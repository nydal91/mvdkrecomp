// Nintendo SDK surface used by recompiled code. Signatures are taken from the mangled
// import names in the NSO's dynamic symbol table. On Android these are provided by the
// HLE/shim layer (recompilation/outputs/logic/shim/), not by Nintendo code.
#pragma once

#include <cstddef>
#include <cstdint>

namespace nn::os {

// Opaque, sized like the SDK's (0x20 bytes: the CriticalSection is 0x40 = IDisposer 0x20
// + MutexType).
struct MutexType {
    uint8_t storage[0x20];
};
static_assert(sizeof(MutexType) == 0x20);

struct TlsSlot {
    uint32_t value;
};

using Tick = int64_t;

void InitializeMutex(MutexType* mutex, bool recursive, int lock_level);
void FinalizeMutex(MutexType* mutex);
void LockMutex(MutexType* mutex);
void UnlockMutex(MutexType* mutex);
void* GetTlsValue(TlsSlot slot);
Tick GetSystemTick();
void SetMemoryHeapSize(size_t size);
int AllocateMemoryBlock(uintptr_t* out_address, size_t size);

}  // namespace nn::os

namespace nn::oe {

enum class OperationMode : int32_t {};
enum class PerformanceMode : int32_t {};
enum class FocusState : int32_t {};

void Initialize();
void SetResumeNotificationEnabled(bool enabled);
void SetOperationModeChangedNotificationEnabled(bool enabled);
void SetPerformanceModeChangedNotificationEnabled(bool enabled);
OperationMode GetOperationMode();
PerformanceMode GetPerformanceMode();
FocusState GetCurrentFocusState();

}  // namespace nn::oe
