#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csetjmp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "vm/object.h"
#include "vm/jni_env.h"

namespace dexe::vm {

class Runtime;
struct Frame;
struct JniLocals;
struct ThreadState;

// A Java monitor (the lock and wait set of an object), created on first use and owned by the object's
// heap header. Waiting for the monitor counts as blocking, so garbage collection can run meanwhile.
struct Monitor {
    std::mutex mutex;
    std::condition_variable entry_cv;
    std::condition_variable wait_cv;
    ThreadState* owner = nullptr;
    uint32_t recursion_count = 0;

    void enter(Runtime& rt, ThreadState* self);
    void exit(Runtime& rt, ThreadState* self);
    bool holds_lock(ThreadState* self);
    void wait(Runtime& rt, ThreadState* self, int64_t timeout_ms, int32_t nanos);
    void notify(Runtime& rt, ThreadState* self);
    void notify_all(Runtime& rt, ThreadState* self);
};

// Execution state of an OS thread that runs Java code.
struct ThreadState {
    Runtime* runtime = nullptr;
    std::thread::id tid{};
    void* os_handle = nullptr;      // Win32 handle of threads started by the runtime
    unsigned long os_thread_id = 0;  // Win32 thread id of every attached thread (the profiler opens it)
    Object* java_thread = nullptr;  // java.lang.Thread instance
    Frame* top_frame = nullptr;
    uintptr_t stack_limit = 0;      // lowest stack address Java calls may use (see Runtime::kStackReserve)
    bool throwing_overflow = false;
    bool throwing_oom = false;      // allocating the OutOfMemoryError itself may exceed the heap limit
    // Deepest stack address Java calls have reached since the dead stack below the caller was last cleared.
    uintptr_t deepest_sp = UINTPTR_MAX;
    bool is_daemon = false;
    std::atomic<bool> is_alive{false};
    std::atomic<bool> interrupted{false};

    // ---- garbage collection (guarded by the runtime's safepoint mutex) ----
    bool attached = false;          // the thread is registered and its stack is valid
    bool running = false;           // the thread may touch the heap; a collection waits until it stops
    int blocking_depth = 0;         // nesting of ScopedBlocking regions
    uintptr_t stack_base = 0;       // highest address of the thread's stack
    // Saved when the thread stops for a collection or starts blocking: the collector scans the stack from
    // saved_sp to stack_base and the saved registers conservatively.
    uintptr_t saved_sp = 0;
    jmp_buf saved_regs;
    // The Java exception a JNI function raised, thrown when the native method returns (see jni_env.cpp).
    Object* jni_exception = nullptr;
    // While a library's JNI_OnLoad runs: the class loader that loads the library, which JNI FindClass uses then (as
    // ART). Also reachable from the Java frame loading the library.
    bool in_jni_on_load = false;
    Object* jni_on_load_loader = nullptr;
    // The local references of the JNI native method this thread is running, or null while it runs Java code or the
    // runtime's own C++ (see jni_env.cpp). Its list is also in local_roots. Changed only while the thread is running.
    JniLocals* jni_locals = nullptr;
    std::unique_ptr<JniLocals> owned_jni_locals;
    // Object references held by native code in C++ containers (see RootedObjects).
    std::vector<const std::vector<Object*>*> local_roots;

    // Support for Unsafe.park / unpark
    std::mutex park_mutex;
    std::condition_variable park_cv;
    bool park_permit = false;

    // Monitor wait interruption tracking
    std::mutex wait_mutex;
    Monitor* current_waiting_monitor = nullptr;

    void park(bool is_absolute, int64_t time);
    void unpark();
    void interrupt();
};

// Thread-local pointer to the current thread's ThreadState.
extern thread_local ThreadState* current_thread_state;

}  // namespace dexe::vm
