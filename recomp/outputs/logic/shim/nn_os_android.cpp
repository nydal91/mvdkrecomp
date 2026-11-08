// Android implementation of the nn::os / nn::oe surface used by recompiled code.
// HLE: these replace Nintendo SDK functions, they are not reconstructed from the binary.

#include <android/log.h>
#include <pthread.h>
#include <sys/mman.h>
#include <time.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "recomp/nn.hpp"
#include "recomp/sead.hpp"
#include "recomp/todo.hpp"

namespace {
constexpr const char* kTag = "grande-recomp";
// Switch system tick runs at 19.2 MHz; game code computes frame budgets from it.
constexpr int64_t kTickFrequency = 19'200'000;
}  // namespace

namespace sead {
int64_t gTickFrequency = kTickFrequency;  // set by the SDK at boot on hardware
}  // namespace sead

namespace recomp {
void todo(uint64_t original_address, const char* function) {
    __android_log_print(ANDROID_LOG_FATAL, kTag, "RECOMP_TODO 0x%08llx %s",
                        static_cast<unsigned long long>(original_address), function);
    std::fprintf(stderr, "RECOMP_TODO 0x%08llx %s\n",
                 static_cast<unsigned long long>(original_address), function);
    std::abort();
}
}  // namespace recomp

namespace nn::os {

// MutexType is 0x20 bytes on Switch; a bionic pthread_mutex_t is 40 bytes on arm64, so we
// can't store it inline. Keep a heap-allocated mutex pointer in the first 8 bytes.
static_assert(sizeof(pthread_mutex_t*) <= sizeof(MutexType));

static pthread_mutex_t* mutexOf(MutexType* m) {
    pthread_mutex_t* p;
    std::memcpy(&p, m->storage, sizeof(p));
    return p;
}

void InitializeMutex(MutexType* mutex, bool recursive, int /*lock_level*/) {
    auto* p = new pthread_mutex_t;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, recursive ? PTHREAD_MUTEX_RECURSIVE : PTHREAD_MUTEX_NORMAL);
    pthread_mutex_init(p, &attr);
    pthread_mutexattr_destroy(&attr);
    std::memset(mutex->storage, 0, sizeof(mutex->storage));
    std::memcpy(mutex->storage, &p, sizeof(p));
}

void FinalizeMutex(MutexType* mutex) {
    pthread_mutex_t* p = mutexOf(mutex);
    pthread_mutex_destroy(p);
    delete p;
    std::memset(mutex->storage, 0, sizeof(mutex->storage));
}

void LockMutex(MutexType* mutex) { pthread_mutex_lock(mutexOf(mutex)); }
void UnlockMutex(MutexType* mutex) { pthread_mutex_unlock(mutexOf(mutex)); }

// TLS slots map 1:1 onto pthread keys.
void* GetTlsValue(TlsSlot slot) { return pthread_getspecific(static_cast<pthread_key_t>(slot.value)); }

Tick GetSystemTick() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    // ns -> 19.2 MHz ticks without overflow for ~15 years of uptime.
    return static_cast<Tick>(ts.tv_sec) * kTickFrequency +
           static_cast<Tick>(ts.tv_nsec) * 24 / 1250;
}

void SetMemoryHeapSize(size_t) {}  // Android: the process address space is already there.

int AllocateMemoryBlock(uintptr_t* out_address, size_t size) {
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        *out_address = 0;
        return -1;
    }
    *out_address = reinterpret_cast<uintptr_t>(p);
    return 0;
}

}  // namespace nn::os

namespace nn::oe {
void Initialize() {}
void SetResumeNotificationEnabled(bool) {}
void SetOperationModeChangedNotificationEnabled(bool) {}
void SetPerformanceModeChangedNotificationEnabled(bool) {}
OperationMode GetOperationMode() { return OperationMode{0}; }      // handheld
PerformanceMode GetPerformanceMode() { return PerformanceMode{0}; }  // normal
FocusState GetCurrentFocusState() { return FocusState{1}; }        // in focus
}  // namespace nn::oe
