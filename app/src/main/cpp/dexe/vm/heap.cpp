// Non-moving mark-sweep garbage collector.
//
// Every object is preceded by a HeapHeader and linked into a list. Runtime::collect_garbage stops all other
// threads (see the safepoint code in runtime.cpp) and then calls Heap::collect, which marks from:
//   - precise roots: interpreter frame registers, static fields, class mirrors, interned strings, constant-pool
//     caches, reflection caches, thread objects, RootedObjects lists held by native code, and global references
//     (Runtime::acquire_global, for objects referenced from native memory such as Parcel buffers);
//   - conservative roots: every attached thread's native stack (from the stack pointer saved when it stopped up
//     to its stack base) and its saved registers. Any word that points into an object keeps it alive, which
//     covers object pointers held in C++ locals by the interpreter and native methods.
// References (java.lang.ref.Reference) are processed in Java's order of reachability strength:
//   1. soft and weak references whose referent is not strongly reachable are cleared (soft referents are treated as
//      strongly reachable unless the collection must free memory before an OutOfMemoryError);
//   2. objects to finalize: a FinalizerReference whose referent is unreachable moves it to its `zombie` field and
//      the referent (with everything it reaches) stays alive until FinalizerDaemon has called finalize();
//   3. weak references discovered while marking those objects are cleared, then phantom references whose referent
//      is still unreachable (Cleaners are phantom references).
// Cleared references with a queue are returned; the runtime hands them to ReferenceQueue.add (see runtime.cpp).

#include "vm/heap.h"

#include "vm/jni_env.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "vm/object.h"
#include "vm/runtime.h"

namespace dexe::vm {

Heap::Heap() {
    if (const char* stress = std::getenv("DEXE_GC_STRESS"))
        stress_interval_ = std::max<size_t>(1, std::strtoull(stress, nullptr, 10));
}

Heap::~Heap() { clear(); }

void Heap::clear() {
    std::lock_guard lock(heap_mutex_);
    for (HeapHeader* cur = head_; cur;) {
        HeapHeader* next = cur->next;
        delete cur->monitor;
        std::free(cur);
        cur = next;
    }
    head_ = nullptr;
    bytes_allocated_ = 0;
}

bool Heap::should_collect(size_t bytes) {
    std::lock_guard lock(heap_mutex_);
    if (stress_interval_) return total_allocations_ % stress_interval_ == 0;
    return bytes_allocated_ + bytes > gc_threshold_;
}

Object* Heap::allocate(size_t bytes) {
    void* mem = std::calloc(1, sizeof(HeapHeader) + bytes);
    if (!mem) return nullptr;
    auto* h = static_cast<HeapHeader*>(mem);
    h->size = bytes;
    std::lock_guard lock(heap_mutex_);
    h->next = head_;
    if (head_) head_->prev = h;
    head_ = h;
    bytes_allocated_ += bytes;
    total_allocations_++;
    return header_to_object(h);
}

std::vector<Object*> Heap::collect(Runtime& rt, bool clear_soft) {
    std::lock_guard heap_lock(heap_mutex_);
    const size_t bytes_before = bytes_allocated_;

    // Sorted object extents, for resolving conservative pointers (including interior pointers) to objects.
    struct Extent {
        uintptr_t start;
        uintptr_t end;
        HeapHeader* header;
    };
    std::vector<Extent> extents;
    for (HeapHeader* h = head_; h; h = h->next) {
        auto start = reinterpret_cast<uintptr_t>(header_to_object(h));
        extents.push_back({start, start + std::max<size_t>(h->size, 1), h});
    }
    std::sort(extents.begin(), extents.end(), [](const Extent& x, const Extent& y) { return x.start < y.start; });

    std::vector<Object*> mark_stack;
    auto mark = [&](Object* obj) {
        if (!obj) return;
        HeapHeader* h = object_to_header(obj);
        if (h->flags & HeapHeader::kMarkBit) return;
        h->flags |= HeapHeader::kMarkBit;
        mark_stack.push_back(obj);
    };
    auto mark_conservative = [&](uintptr_t word) {
        if (extents.empty() || word < extents.front().start || word >= extents.back().end) return;
        auto it = std::upper_bound(extents.begin(), extents.end(), word,
                                   [](uintptr_t w, const Extent& e) { return w < e.start; });
        if (it == extents.begin()) return;
        --it;
        if (word < it->end) mark(header_to_object(it->header));
    };
    auto scan_range = [&](uintptr_t from, uintptr_t to) {
        from = (from + 7) & ~uintptr_t(7);
        for (uintptr_t p = from; p + sizeof(uintptr_t) <= to; p += sizeof(uintptr_t))
            mark_conservative(*reinterpret_cast<const uintptr_t*>(p));
    };

    // Threads: Java thread objects, interpreter frames, native-held lists, stacks and registers.
    {
        std::lock_guard lock(rt.threads_mutex());
        for (const auto& t : rt.threads()) {
            mark(t->java_thread);
            if (!t->attached) continue;
            for (Frame* f = t->top_frame; f; f = f->caller)
                for (uint32_t i = 0; i < f->num_refs; ++i) mark(f->refs[i]);
            for (const auto* roots : t->local_roots)
                for (Object* o : *roots) mark(o);
            mark(t->jni_exception);
            auto regs = reinterpret_cast<uintptr_t>(&t->saved_regs);
            scan_range(regs, regs + sizeof(t->saved_regs));
            if (t->saved_sp && t->saved_sp < t->stack_base) scan_range(t->saved_sp, t->stack_base);
        }
    }

    // Classes: mirrors and static fields.
    for (const auto& k : rt.class_storage()) {
        mark(k->mirror);
        for (const auto& f : k->fields)
            if (f->is_static() && f->is_reference()) mark(f->static_value.l);
    }
    for (const auto& [str, obj] : rt.interned()) mark(obj);
    rt.for_each_global(mark);
    mark(rt.main_thread());
    for (const auto& cache : rt.dex_caches()) {
        mark(cache->class_loader);
        for (Object* o : cache->strings) mark(o);
        for (Object* o : cache->method_types) mark(o);
        for (Object* o : cache->method_handles) mark(o);
        for (Object* o : cache->call_sites) mark(o);
    }

    // Transitive marking. Referents of references are left for reference processing, except soft referents
    // while memory is not short.
    std::vector<Object*> references;
    uint32_t referent_slot = rt.referent_slot();
    auto drain = [&] {
        while (!mark_stack.empty()) {
            Object* obj = mark_stack.back();
            mark_stack.pop_back();
            Class* k = obj->klass;
            if (!k) continue;
            bool holds_referent = k->reference_kind != ReferenceKind::kNone &&
                                  (k->reference_kind != ReferenceKind::kSoft || clear_soft);
            if (holds_referent) references.push_back(obj);
            for (uint32_t slot : k->reference_slots)
                if (!(holds_referent && slot == referent_slot)) mark(obj->ref_slot(slot));
            if (k->is_array()) {
                char c = k->component->descriptor[0];
                if (c == 'L' || c == '[') {
                    auto* arr = static_cast<Array*>(obj);
                    for (int32_t i = 0; i < arr->length; ++i) mark(arr->data<Object*>()[i]);
                }
            }
        }
    };
    drain();

    auto is_marked = [](Object* obj) { return (object_to_header(obj)->flags & HeapHeader::kMarkBit) != 0; };
    std::vector<Object*> to_enqueue;
    uint32_t queue_slot = rt.queue_slot();
    auto clear_and_queue = [&](Object* ref) {
        ref->ref_slot(referent_slot) = nullptr;
        if (queue_slot != dex::kNoIndex && ref->ref_slot(queue_slot)) to_enqueue.push_back(ref);
    };
    // Clears the soft and weak references among references[from, to) whose referent is unreachable.
    auto clear_weak = [&](size_t from, size_t to) {
        for (size_t i = from; i < to; ++i) {
            Object* ref = references[i];
            ReferenceKind kind = ref->klass->reference_kind;
            if (kind != ReferenceKind::kSoft && kind != ReferenceKind::kWeak) continue;
            Object* referent = ref->ref_slot(referent_slot);
            if (referent && !is_marked(referent)) clear_and_queue(ref);
        }
    };

    // 1. Soft and weak references.
    size_t found_before_finalization = references.size();
    clear_weak(0, found_before_finalization);

    // 2. Objects to finalize are kept alive (all of them first, as ART does, then everything they reach).
    uint32_t zombie_slot = rt.zombie_slot();
    for (size_t i = 0; i < found_before_finalization; ++i) {
        Object* ref = references[i];
        if (ref->klass->reference_kind != ReferenceKind::kFinalizer) continue;
        Object* referent = ref->ref_slot(referent_slot);
        if (!referent || is_marked(referent)) continue;
        ref->ref_slot(zombie_slot) = referent;
        clear_and_queue(ref);
        mark(referent);
    }
    drain();

    // 3. Weak references reachable only through objects being finalized, then phantom references.
    clear_weak(found_before_finalization, references.size());
    for (Object* ref : references) {
        if (ref->klass->reference_kind != ReferenceKind::kPhantom) continue;
        Object* referent = ref->ref_slot(referent_slot);
        if (referent && !is_marked(referent)) clear_and_queue(ref);
    }

    // JNI weak global references to objects that are now unreachable are cleared.
    sweep_weak_globals(is_marked);

    // Sweep.
    for (HeapHeader* cur = head_; cur;) {
        HeapHeader* next = cur->next;
        if (cur->flags & HeapHeader::kMarkBit) {
            cur->flags &= ~HeapHeader::kMarkBit;
        } else {
            if (cur->prev) cur->prev->next = cur->next;
            else head_ = cur->next;
            if (cur->next) cur->next->prev = cur->prev;
            bytes_allocated_ -= cur->size;
            delete cur->monitor;
            if (stress_interval_) std::memset(cur, 0xdb, sizeof(HeapHeader) + cur->size);  // poison freed memory
            std::free(cur);
        }
        cur = next;
    }

    constexpr size_t kMinThreshold = 4 * 1024 * 1024;
    gc_threshold_ = std::min(kMaxBytes, std::max(kMinThreshold, bytes_allocated_ + bytes_allocated_ / 2));
    gc_count_++;
    // With DEXE_LOG, each collection is logged, as ART logs its GCs (the live heap over a session shows leaks).
    static const bool log = std::getenv("DEXE_LOG") != nullptr;
    if (log) {
        std::fprintf(stderr, "I/dexe: GC freed %zu kB, %zu kB live, next at %zu kB (limit %zu kB)\n",
                     (bytes_before - bytes_allocated_) >> 10, bytes_allocated_ >> 10, gc_threshold_ >> 10,
                     kMaxBytes >> 10);
    }
    return to_enqueue;
}

}  // namespace dexe::vm
