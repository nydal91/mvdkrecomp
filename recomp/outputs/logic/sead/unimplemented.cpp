// Bodies not recompiled yet. Each traps with its original address (see recomp/todo.hpp).

#include "recomp/sead.hpp"
#include "recomp/todo.hpp"

namespace sead {


void Heap::genInformation_(void*) { RECOMP_TODO(0x00448ca4); }
Heap* HeapMgr::findContainHeap(const void*) const { RECOMP_TODO(0x00448fe4); }

bool ExpHeap::checkDerivedRuntimeTypeInfo(const void*) const { RECOMP_TODO(0x00447130); }
const void* ExpHeap::getRuntimeTypeInfo() const { RECOMP_TODO(0x00447230); }
void ExpHeap::destroy() { RECOMP_TODO(0x00444ccc); }
void* ExpHeap::resizeFront(void*, size_t) { RECOMP_TODO(0x004461b8); }
void* ExpHeap::tryRealloc(void*, size_t, int32_t) { RECOMP_TODO(0x00446474); }
void ExpHeap::freeAll() { RECOMP_TODO(0x00444db8); }
void ExpHeap::dump() const { RECOMP_TODO(0x00446fd4); }
void ExpHeap::vslot_c0() { RECOMP_TODO(0x00446b78); }
void ExpHeap::vslot_c8() { RECOMP_TODO(0x00447104); }
void ExpHeap::vslot_e0() { RECOMP_TODO(0x00444cdc); }
void ExpHeap::vslot_e8() { RECOMP_TODO(0x00446fcc); }

}  // namespace sead
