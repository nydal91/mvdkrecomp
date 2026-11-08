// Reconstruction of the parts of sead (Nintendo EAD's in-house framework) that the
// recompiled code uses. Every layout here was read off the binary (field offsets are in
// the comments and checked with static_assert). Names follow sead conventions where the
// usage matches; `unk_<offset>` marks fields whose purpose isn't known yet.
//
// Virtual functions are declared in vtable order. Slots we have not identified are
// `vslot_<byteoffset>` placeholders so the vtable index of every named method matches the
// binary (Itanium ABI: a virtual destructor occupies two slots).
#pragma once

#include <cstddef>
#include <cstdint>

#include "recomp/nn.hpp"

// Layout checks on polymorphic classes: offsetof is conditionally supported there, and
// both clang and GCC compute it as expected for these single-inheritance chains.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#define RECOMP_VSLOT(off) virtual void vslot_##off() {}

namespace sead {

// ---------------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------------
// In this build a SafeString is passed as a pointer to one `const char*` slot
// (`str x8,[sp]; mov x1,sp`), with no vtable written first. Names are stored as the raw
// `const char*` (Heap +0x20 holds `*name`).
struct SafeString {
    const char* str;
};

struct Vector2f {
    float x, y;
};

// Empty string literal at 0x012C2FB7; used to default-initialize name fields.
inline constexpr const char* cEmptyString = "";

// ---------------------------------------------------------------------------------
// Intrusive lists
// ---------------------------------------------------------------------------------
struct ListNode {
    ListNode* prev = nullptr;  // +0x00
    ListNode* next = nullptr;  // +0x08
};

// sead::OffsetList<T>: sentinel node + count + offset of the ListNode inside T.
// Constructed as {self, self, 0, -1}, and the owner sets `offset` afterwards.
struct OffsetList {
    ListNode start_end;     // +0x00 sentinel, prev/next both point to itself when empty
    int32_t count;          // +0x10
    int32_t offset;         // +0x14

    void init() {
        start_end.prev = &start_end;
        start_end.next = &start_end;
        count = 0;
        offset = -1;
    }
};
static_assert(sizeof(OffsetList) == 0x18);

// sub_42f95c — link `node` immediately before `self`. Called with a list sentinel this
// appends to the tail:  old = self->prev; self->prev = node; node->{prev,next} = {old,self};
// if (old) old->next = node;
void ListNode_insertBefore(ListNode* self, ListNode* node);
// sub_42f944 — link `node` immediately after `self` (push-front when self is a sentinel).
void ListNode_insertAfter(ListNode* self, ListNode* node);
// sub_42f974 — unlink `node` and clear its pointers.
void ListNode_erase(ListNode* node);

class Heap;

// ---------------------------------------------------------------------------------
// IDisposer — 0x20 bytes. Registers itself with the heap that contains it, so that the
// object is destroyed when that heap is destroyed.
// ---------------------------------------------------------------------------------
class IDisposer {
public:
    enum class HeapNullOption : uint32_t {
        AlwaysUseSpecifiedHeap = 0,
        UseSpecifiedOrContainHeap = 1,
        DoNotAppendDisposerIfNoHeapSpecified = 2,
        UseSpecifiedOrCurrentHeap = 3,
    };

    IDisposer(Heap* disposer_heap, HeapNullOption option);  // sub_444690
    virtual ~IDisposer();

    Heap* disposer_heap_;   // +0x08
    ListNode list_node_;    // +0x10 (link in Heap::disposer_list_, which uses offset 0x10)
};
static_assert(sizeof(IDisposer) == 0x20);

// ---------------------------------------------------------------------------------
// CriticalSection — 0x40 bytes: IDisposer + nn::os::MutexType (recursive).
// ---------------------------------------------------------------------------------
class CriticalSection : public IDisposer {
public:
    explicit CriticalSection(Heap* disposer_heap);  // sub_45b710
    ~CriticalSection() override;
    void lock() { nn::os::LockMutex(&mutex_); }      // sub_45b7fc
    void unlock() { nn::os::UnlockMutex(&mutex_); }  // sub_45b80c

    nn::os::MutexType mutex_;  // +0x20
};
static_assert(sizeof(CriticalSection) == 0x40);

// ---------------------------------------------------------------------------------
// Heap — 0xCC bytes of state (ExpHeap starts its own fields at +0xCC)
// ---------------------------------------------------------------------------------
class Heap : public IDisposer {
public:
    enum HeapDirection : int32_t { cHeapDirection_Reverse = -1, cHeapDirection_Forward = 1 };

    // Flag word at +0xC8: bit 0 = enable_lock, bit 2 = always set, bits 16..31 = heap ID.
    static constexpr uint32_t cFlag_EnableLock = 1u << 0;
    static constexpr uint32_t cFlag_Bit2 = 1u << 2;

    // sub_448224. `fallback_disposer_heap` (x7) is the disposer heap used when `parent` is
    // null (a root heap). Every call site we've seen passes nullptr.
    Heap(const SafeString& name, Heap* parent, void* start, size_t size,
         HeapDirection direction, bool enable_lock, Heap* fallback_disposer_heap);

    // vtable, byte offsets in the comments. Slots 0x00/0x08 are the IDisposer
    // destructors. Matches stock sead's order except for the extra slot at +0x40.
    virtual bool checkDerivedRuntimeTypeInfo(const void* type) const = 0;    // +0x10
    virtual const void* getRuntimeTypeInfo() const = 0;                      // +0x18
    virtual void destroy() = 0;                                              // +0x20
    virtual size_t adjust() = 0;                                             // +0x28
    virtual void* tryAlloc(size_t size, int32_t alignment) = 0;              // +0x30
    virtual void free(void* ptr) = 0;                                        // +0x38
    virtual size_t freeAndGetAllocatableSize(void* ptr, int32_t alignment) = 0;  // +0x40
    virtual void* resizeFront(void* ptr, size_t new_size) = 0;               // +0x48
    virtual void* resizeBack(void* ptr, size_t new_size) = 0;                // +0x50
    virtual void* tryRealloc(void* ptr, size_t new_size, int32_t alignment) = 0;  // +0x58
    virtual void freeAll() = 0;                                              // +0x60
    virtual const void* getStartAddress() const = 0;                         // +0x68
    virtual const void* getEndAddress() const = 0;                           // +0x70
    virtual size_t getSize() const = 0;                                      // +0x78
    virtual size_t getFreeSize() const = 0;                                  // +0x80
    virtual size_t getMaxAllocatableSize(int32_t alignment) const = 0;       // +0x88
    virtual bool isInclude(const void* ptr) const = 0;                       // +0x90
    virtual bool isEmpty() const = 0;                                        // +0x98
    virtual bool isFreeable() const = 0;                                     // +0xa0
    virtual bool isResizable() const = 0;                                    // +0xa8
    virtual bool isAdjustable() const = 0;                                   // +0xb0
    virtual void dump() const = 0;                                           // +0xb8
    virtual void vslot_c0() = 0;                                             // +0xc0
    virtual void vslot_c8() = 0;                                             // +0xc8
    virtual void genInformation_(void* node);                                // +0xd0 sub_448ca4
    virtual void pushBackChild_(Heap* child);                                // +0xd8 sub_4487e4
    virtual void vslot_e0() = 0;                                             // +0xe0
    virtual void vslot_e8() = 0;                                             // +0xe8

    void appendDisposer_(IDisposer* disposer);  // sub_44862c
    void removeDisposer_(IDisposer* disposer);  // sub_4486a4

    bool isLockEnabled() const { return (flag_ & cFlag_EnableLock) != 0; }
    HeapDirection getDirection() const { return direction_; }

    const char* name_;          // +0x20  (INamable)
    void* start_;               // +0x28
    size_t size_;               // +0x30
    Heap* parent_;              // +0x38
    OffsetList child_list_;     // +0x40  offset 0x58 (= &Heap::list_node_)
    ListNode list_node_;        // +0x58  link in parent's child_list_
    OffsetList disposer_list_;  // +0x68  offset 0x10 (= &IDisposer::list_node_)
    HeapDirection direction_;   // +0x80
    uint32_t pad_84_;           // +0x84
    CriticalSection cs_;        // +0x88
    uint32_t flag_;             // +0xC8  (atomic)

    // Bit 1 of flag_: freeing is refused while set (checked by freeAndGetAllocatableSize).
    static constexpr uint32_t cFlag_DisableFree = 1u << 1;
    uint16_t heapId() const { return static_cast<uint16_t>(flag_ >> 16); }
};
static_assert(offsetof(Heap, name_) == 0x20);
static_assert(offsetof(Heap, child_list_) == 0x40);
static_assert(offsetof(Heap, list_node_) == 0x58);
static_assert(offsetof(Heap, disposer_list_) == 0x68);
static_assert(offsetof(Heap, direction_) == 0x80);
static_assert(offsetof(Heap, cs_) == 0x88);
static_assert(offsetof(Heap, flag_) == 0xC8);

// RAII lock that only takes the critical section when the heap has locking enabled.
// Matches the inline pattern `ldr w8,[x,#0xc8]; tbz w8,#0,skip; bl lock`.
class ConditionalScopedLock {
public:
    // const so it can be used from const query methods; the lock itself is mutable state.
    explicit ConditionalScopedLock(const Heap& heap)
        : cs_(heap.isLockEnabled() ? const_cast<CriticalSection*>(&heap.cs_) : nullptr) {
        if (cs_) cs_->lock();
    }
    ~ConditionalScopedLock() {
        if (cs_) cs_->unlock();
    }
    ConditionalScopedLock(const ConditionalScopedLock&) = delete;
    ConditionalScopedLock& operator=(const ConditionalScopedLock&) = delete;

private:
    CriticalSection* cs_;
};

// ---------------------------------------------------------------------------------
// ExpHeap — 0x100 bytes; managed blocks follow it (forward) or precede it (reverse).
// ---------------------------------------------------------------------------------
class ExpHeap : public Heap {
public:
    // Header placed in front of every block. 0x20 bytes.
    struct MemBlock {
        ListNode list_node;   // +0x00 (free/use lists use offset 0)
        // 0xFFFF while free; the owning heap's ID (Heap::heapId()) while allocated.
        uint16_t magic;       // +0x10
        // Gap between the end of this header and the user pointer (alignment padding).
        // When non-zero, the qword just before the user pointer holds `this | 1`.
        uint16_t pad;         // +0x12
        uint32_t unk_14;      // +0x14
        size_t size;          // +0x18 bytes after header+pad

        uint8_t* data() { return reinterpret_cast<uint8_t*>(this) + sizeof(MemBlock) + pad; }
        uint8_t* end() { return data() + size; }
    };
    static_assert(sizeof(MemBlock) == 0x20);

    enum class AllocMode : uint8_t { FirstFit = 0, BestFit = 1, WorstFit = 2 };
    enum class FreeMergeMode : uint8_t { Auto = 0, ListWalk = 1, AutoIfFew = 2 };

    // sub_4448c0 (masks enable_lock to one bit) -> sub_4448c8
    static ExpHeap* create(size_t size, const SafeString& name, Heap* parent, int32_t alignment,
                           HeapDirection direction, bool enable_lock);
    // sub_444b74: root heap placed directly on a raw memory range (no parent).
    static ExpHeap* tryCreate(void* address, size_t size, const SafeString& name,
                              bool enable_lock, Heap* fallback_disposer_heap);

    ExpHeap(const SafeString& name, Heap* parent, void* start, size_t size,
            HeapDirection direction, bool enable_lock, Heap* fallback_disposer_heap = nullptr);

    // ExpHeap vtable 0x016AFBE0. Implemented in sead/exp_heap.cpp; the rest are
    // RECOMP_TODO stubs in sead/unimplemented.cpp.
    bool checkDerivedRuntimeTypeInfo(const void* type) const override;       // sub_447130
    const void* getRuntimeTypeInfo() const override;                         // sub_447230
    void destroy() override;                                                 // sub_444ccc
    size_t adjust() override;                                                // sub_444eb0
    void* tryAlloc(size_t size, int32_t alignment) override;                 // sub_445084
    void free(void* ptr) override;                                           // sub_445cdc
    size_t freeAndGetAllocatableSize(void* ptr, int32_t alignment) override; // sub_445cec
    void* resizeFront(void* ptr, size_t new_size) override;                  // sub_4461b8
    void* resizeBack(void* ptr, size_t new_size) override;                   // sub_44636c
    void* tryRealloc(void* ptr, size_t new_size, int32_t alignment) override;  // sub_446474
    void freeAll() override;                                                 // sub_444db8
    const void* getStartAddress() const override;                            // sub_4466d8
    const void* getEndAddress() const override;                              // sub_4466e0
    size_t getSize() const override;                                         // sub_4466ec
    size_t getFreeSize() const override;                                     // sub_4466f4
    size_t getMaxAllocatableSize(int32_t alignment) const override;          // sub_44679c
    bool isInclude(const void* ptr) const override;                          // sub_444d5c
    bool isEmpty() const override;                                           // sub_447108
    bool isFreeable() const override;                                        // sub_447118
    bool isResizable() const override;                                       // sub_447120
    bool isAdjustable() const override;                                      // sub_447128
    void dump() const override;                                              // sub_446fd4
    void vslot_c0() override;                                                // sub_446b78
    void vslot_c8() override;                                                // sub_447104
    void vslot_e0() override;                                                // sub_444cdc
    void vslot_e8() override;                                                // sub_446fcc
    // Introduced by ExpHeap (Heap's vtable ends at +0xe8). Return the free block adjust()
    // may trim off the tail/head, or nullptr.
    virtual void* findAdjustableTailBlock_();                                // +0xf0 sub_446958
    virtual void* findAdjustableHeadBlock_();                                // +0xf8 sub_446a54

    AllocMode alloc_mode_;          // +0xCC
    FreeMergeMode free_merge_mode_; // +0xCD
    uint16_t pad_ce_;
    OffsetList free_list_;          // +0xD0  address-ordered
    OffsetList use_list_;           // +0xE8

private:
    void createFirstFreeBlock_();
    MemBlock* findFreeBlockHead_(size_t size) const;                       // sub_4456e4
    MemBlock* findFreeBlockHeadAligned_(size_t size, int32_t align) const; // sub_4457f8
    MemBlock* findFreeBlockTail_(size_t size) const;                       // sub_4459a8
    MemBlock* findFreeBlockTailAligned_(size_t size, int32_t align) const; // sub_445ac8
    MemBlock* allocFromHead_(size_t size);                                 // sub_445248
    MemBlock* allocFromHeadAligned_(size_t size, int32_t align);           // sub_445374
    MemBlock* allocFromTail_(size_t size);                                 // sub_445514
    MemBlock* allocFromTailAligned_(size_t size, int32_t align);           // sub_4455e0
    MemBlock* recycleBlock_(MemBlock* block);                              // sub_445f14
    void sortUseListByAddress_();                                          // inline in 446958
    void pushUsedBlock_(MemBlock* block);
};
static_assert(offsetof(ExpHeap, alloc_mode_) == 0xCC);
static_assert(offsetof(ExpHeap, free_merge_mode_) == 0xCD);
static_assert(offsetof(ExpHeap, free_list_) == 0xD0);
static_assert(offsetof(ExpHeap, use_list_) == 0xE8);
static_assert(sizeof(ExpHeap) == 0x100);

// ---------------------------------------------------------------------------------
// Threads (only what HeapMgr::getCurrentHeap touches)
// ---------------------------------------------------------------------------------
class Thread {
public:
    uint8_t unk_00[0x88];
    Heap* current_heap_;  // +0x88
};
static_assert(offsetof(Thread, current_heap_) == 0x88);

class ThreadMgr {
public:
    static ThreadMgr* sInstance;  // 0x01758750
    Thread* getCurrentThread() const {
        return static_cast<Thread*>(nn::os::GetTlsValue(thread_ptr_tls_));
    }
    uint8_t unk_00[0x68];
    nn::os::TlsSlot thread_ptr_tls_;  // +0x68
};
static_assert(offsetof(ThreadMgr, thread_ptr_tls_) == 0x68);

// ---------------------------------------------------------------------------------
// HeapMgr
// ---------------------------------------------------------------------------------
struct HeapPtrArray {  // sead::PtrArray<Heap>
    int32_t size;
    int32_t capacity;
    Heap** ptrs;
};
static_assert(offsetof(HeapPtrArray, ptrs) == 0x8);

// Memory backing the root heap. 0x18 bytes.
struct Arena {
    // sub_4495e0: reserves `size` rounded up to 2 MiB from the OS.
    void initialize(size_t size);
    uint8_t* start = nullptr;  // +0x00
    size_t size = 0;           // +0x08
    uint64_t unk_10 = 0;       // +0x10
};

// Arguments passed to the HeapMgr's alloc-failed callback (built on tryAlloc's stack).
struct AllocFailedCallbackArg {
    Heap* heap;                 // +0x00
    size_t request_size;        // +0x08
    int32_t request_alignment;  // +0x10
    size_t alloc_size;          // +0x18
    int32_t alloc_alignment;    // +0x20
};

class IAllocFailedCallback {
public:
    virtual void invoke(const AllocFailedCallbackArg* arg) = 0;  // slot 0
};

class HeapMgr {
public:
    static HeapMgr sInstance;           // 0x017584F0
    static HeapMgr* sInstancePtr;       // 0x01758508
    static Arena* sArena;               // 0x01758510
    static Arena sDefaultArena;         // 0x01758518
    static HeapPtrArray sRootHeaps;     // 0x01758530
    static CriticalSection sHeapTreeLockCS;  // 0x01758590
    static int64_t sTicksPer10us;       // 0x017585E0

    // sub_448e30: reserve `size` bytes, build "RootHeap" on them, publish sInstancePtr.
    static void initialize(size_t size);
    // sub_448f24: same, on a caller-provided arena, without taking the tree lock.
    static void initialize(Arena* arena);

    Heap* getCurrentHeap() const;               // sub_449428
    Heap* findContainHeap(const void* ptr) const;  // sub_448fe4

    // Inlined at every call site as:
    //   ldr w8,[x21] ; cbz w8,null ; ldr x8,[x21,#8] ; ldr xN,[x8]
    // Only index 0 is ever used, so this matches the binary exactly.
    static Heap* getRootHeap(int32_t index) {
        return sRootHeaps.size > index ? sRootHeaps.ptrs[index] : nullptr;
    }

    uint64_t unk_00;
    Heap* no_thread_current_heap_;            // +0x08 used when the caller isn't a sead::Thread
    IAllocFailedCallback* alloc_failed_cb_;   // +0x10
};
static_assert(sizeof(HeapMgr) == 0x18);

// ---------------------------------------------------------------------------------
// HeapArray / tasks
// ---------------------------------------------------------------------------------
struct HeapArray {
    Heap* heaps[4];
    uint32_t unk_20;
    int32_t primary_index;  // +0x24
    Heap* getPrimaryHeap() const { return heaps[primary_index]; }
};
static_assert(offsetof(HeapArray, primary_index) == 0x24);

struct TaskConstructArg {
    HeapArray* heap_array;  // +0x00
};

class TaskBase;
using TaskFactory = TaskBase* (*)(const TaskConstructArg&);

struct TaskClassID {
    enum Type : uint32_t { cInvalid = 0, cInt = 1, cFactory = 2, cString = 3 };
    Type type;             // +0x00
    TaskFactory factory;   // +0x08 (union with int / const char*)
};
static_assert(sizeof(TaskClassID) == 0x10);

class TaskParameter {
public:
    virtual ~TaskParameter() = default;
};

class TaskBase {
public:
    // 0xB8 bytes; sub_43b7c8.
    struct CreateArg {
        explicit CreateArg(const TaskClassID& id);
        TaskClassID class_id;     // +0x00
        uint8_t unk_10[0x70];     // +0x10
        TaskParameter* param;     // +0x80
        uint8_t unk_88[0x20];     // +0x88
        int32_t unk_a8;           // +0xA8  = -1
        int32_t unk_ac;           // +0xAC  = 1
        uint64_t unk_b0;          // +0xB0
    };
};
static_assert(offsetof(TaskBase::CreateArg, param) == 0x80);
static_assert(sizeof(TaskBase::CreateArg) == 0xB8);

class Framework;
class TaskMgr {
public:
    // 0x28 bytes; sub_43bde8.
    struct InitializeArg {
        explicit InitializeArg(const TaskBase::CreateArg& root_task_arg);
        uint32_t unk_00;                               // +0x00 = 0x20
        uint32_t prepare_stack_size;                   // +0x04 = 0x10000
        int32_t prepare_priority;                      // +0x08 = -1
        uint32_t pad_0c;
        const TaskBase::CreateArg* root_task_arg;      // +0x10
        Heap* heap;                                    // +0x18
        Framework* parent_framework;                   // +0x20
    };
    static_assert(sizeof(InitializeArg) == 0x28);

    static TaskMgr* initialize(const InitializeArg& arg);   // sub_43c170
    explicit TaskMgr(const InitializeArg& arg);             // sub_43be08

    uint8_t storage_[0x298];
};

class MethodTreeMgr;

// ---------------------------------------------------------------------------------
// Framework hierarchy: Framework -> GameFramework -> GameFrameworkNx
// ---------------------------------------------------------------------------------
class Framework {
public:
    struct RunArg {  // sub_4394ec writes 0xFFFFFFFF00000000
        uint32_t prepare_stack_size = 0;
        int32_t prepare_priority = -1;
    };
    static_assert(sizeof(RunArg) == 0x8);

    // Static setup, run once before any Framework object exists: creates the root heaps,
    // HeapMgr, ThreadMgr and "sead::SystemManagers" (sub_439bd4 + sub_439308).
    struct InitializeArg {  // sub_4394e0
        uint64_t heap_size = 0x3000000;
        void* arena = nullptr;
    };
    static_assert(sizeof(InitializeArg) == 0x10);
    static void initialize(const InitializeArg& arg);

    Framework();  // sub_439324

    // vtable 0x016AF058 (Framework), slots 0x00..0x68
    RECOMP_VSLOT(0x00) RECOMP_VSLOT(0x08) RECOMP_VSLOT(0x10) RECOMP_VSLOT(0x18)
    virtual void run(Heap* heap, const TaskBase::CreateArg& root_task_arg,
                     const RunArg& run_arg);                              // +0x20 sub_4395bc
    RECOMP_VSLOT(0x28) RECOMP_VSLOT(0x30) RECOMP_VSLOT(0x38) RECOMP_VSLOT(0x40)
    RECOMP_VSLOT(0x48)
    virtual void initRun_(Heap* heap);                                    // +0x50
    virtual void runImpl_(Heap* heap);                                    // +0x58
    virtual void postTaskMgrInitialize_();                                // +0x60 (name inferred)
    virtual MethodTreeMgr* createMethodTreeMgr_(Heap* heap);              // +0x68

    bool reserve_reset_;                    // +0x08
    uint8_t pad_09_[7];
    uint64_t unk_10;                        // +0x10
    // sead::DelegateEvent<void*> (vtable 0x016AF0E0): slot list + count.
    struct ResetEvent {
        ResetEvent() { slots.prev = slots.next = &slots; }
        virtual ~ResetEvent();
        ListNode slots;          // +0x08
        int32_t count = 0;       // +0x18
        uint32_t pad_1c = 0;
    };
    static_assert(sizeof(ResetEvent) == 0x20);
    ResetEvent reset_event_;                // +0x18
    TaskMgr* task_mgr_;                     // +0x38
    MethodTreeMgr* method_tree_mgr_;        // +0x40
    Heap* method_tree_mgr_heap_;            // +0x48
};
static_assert(offsetof(Framework, task_mgr_) == 0x38);
static_assert(offsetof(Framework, method_tree_mgr_heap_) == 0x48);
static_assert(sizeof(Framework) == 0x50);

class GameFramework : public Framework {
public:
    GameFramework();  // sub_4398d0

    uint32_t unk_50;                    // +0x50
    uint32_t pad_54_;
    const char* unk_58;                 // +0x58  ""
    const char* unk_60;                 // +0x60  ""
    const char* unk_68;                 // +0x68  ""
    uint64_t unk_70;                    // +0x70
    uint64_t unk_78;                    // +0x78
    void (*unk_80_callback)(bool);      // +0x80  sub_439920 by default
};
static_assert(offsetof(GameFramework, unk_80_callback) == 0x80);
static_assert(sizeof(GameFramework) == 0x88);

class GameFrameworkNx : public GameFramework {
public:
    // 0x50 bytes, copied verbatim into +0x88 by the ctor.
    struct CreateArg {
        uint32_t vsync_interval = 1;                      // +0x00 frames per present (1 = 60 Hz)
        float clear_color[4] = {0.5f, 0.5f, 0.5f, 1.0f};  // +0x04 RGBA
        uint32_t display_width = 1920;                    // +0x14
        uint32_t display_height = 1080;                   // +0x18
        bool unk_1c = false;                              // +0x1C
        bool unk_1d = false;                              // +0x1D
        bool unk_1e = false;                              // +0x1E
        bool unk_1f = false;                              // +0x1F
        bool unk_20 = false;                              // +0x20
        uint32_t unk_24 = 0x100000;                       // +0x24
        uint32_t unk_28 = 0x100000;                       // +0x28
        uint32_t unk_2c = 0x1000;                         // +0x2C
        uint32_t unk_30 = 0x1000000;                      // +0x30
        uint32_t unk_34 = 0;                              // +0x34
        uint32_t unk_38 = 0x40000;                        // +0x38
        uint32_t unk_3c = 0x80000;                        // +0x3C
        uint32_t unk_40 = 0x40000;                        // +0x40
        uint32_t unk_44 = 0;                              // +0x44
        uint32_t unk_48 = 0x812C;                         // +0x48
        uint32_t unk_4c = 2;                              // +0x4C
    };
    static_assert(sizeof(CreateArg) == 0x50);
    static_assert(offsetof(CreateArg, display_width) == 0x14);

    explicit GameFrameworkNx(const CreateArg& arg);  // sub_4375d8

    // sub_4375c4: Framework::initialize(arg) (sub_439bd4), then nn::oe::Initialize()
    // (sub_439308).
    static void initialize(const InitializeArg& arg) {
        Framework::initialize(arg);
        nn::oe::Initialize();
    }

    // sub_437768
    void initializeGraphicsSystem(Heap* heap, Heap* device_heap, const Vector2f& size);

    void initRun_(Heap* heap) override;  // sub_438120

    CreateArg arg_;                        // +0x088
    uint64_t unk_0d8;                      // +0x0D8
    int64_t unk_0e0_tick;                  // +0x0E0
    uint64_t unk_0e8;                      // +0x0E8
    // Polymorphic member (vtable 0x016AEF68), constructed inline. Two {1,1} float pairs
    // look like scale factors; purpose unconfirmed.
    struct Unk0F0 {
        virtual ~Unk0F0();
        float unk_08[2] = {1.0f, 1.0f};    // +0x08
        uint64_t unk_10 = 0;               // +0x10
        float unk_18[2] = {1.0f, 1.0f};    // +0x18
        uint64_t unk_20 = 0;               // +0x20
        uint64_t unk_28 = 0;               // +0x28
    };
    static_assert(sizeof(Unk0F0) == 0x30);
    Unk0F0 unk_0f0;                        // +0x0F0
    int64_t unk_120_tick;                  // +0x120
    int64_t frame_start_tick_;             // +0x128
    int64_t frame_budget_ticks_;           // +0x130 0.985 of a frame
    int64_t frame_margin_ticks_;           // +0x138 0.006 of a frame
    uint8_t unk_140[0x48];                 // +0x140
    uint64_t unk_188;                      // +0x188
    uint32_t unk_190;                      // +0x190
    uint32_t pad_194_;
    uint8_t unk_198[0x40];                 // +0x198
    uint64_t unk_1d8;                      // +0x1D8
    const char* unk_1e0;                   // +0x1E0 ""
    uint64_t unk_1e8;                      // +0x1E8
    uint8_t unk_1f0[0x30];                 // +0x1F0 not touched by the ctor
    uint32_t unk_220;                      // +0x220 0x01000001
    uint16_t unk_224;                      // +0x224 0xFF00
    uint16_t pad_226_;
    // Polymorphic member (vtable 0x016AEFC8); the ctor only writes its vptr.
    struct Unk228 {
        virtual ~Unk228();
        uint8_t unk_08[0x18];
    };
    Unk228 unk_228;                        // +0x228
};
static_assert(offsetof(GameFrameworkNx, arg_) == 0x88);
static_assert(offsetof(GameFrameworkNx, frame_start_tick_) == 0x128);
static_assert(offsetof(GameFrameworkNx, unk_1e0) == 0x1E0);
static_assert(offsetof(GameFrameworkNx, unk_220) == 0x220);
static_assert(offsetof(GameFrameworkNx, unk_0f0) == 0xF0);
static_assert(offsetof(GameFrameworkNx, unk_120_tick) == 0x120);
static_assert(offsetof(GameFrameworkNx, unk_228) == 0x228);
static_assert(sizeof(GameFrameworkNx) == 0x248);

// sead::TickTime frequency (ticks per second), 0x017588A8.
extern int64_t gTickFrequency;

}  // namespace sead

// sead's placement-new on a heap (sub_42f3d0).
// noexcept: it really returns nullptr on failure (every call site in the binary calls it
// explicitly and then constructs in place, so this changes no observable behavior).
void* operator new(size_t size, sead::Heap* heap, int32_t alignment) noexcept;
