#include "vm/thread.h"

#include <chrono>

#include "vm/runtime.h"

namespace dexe::vm {

thread_local ThreadState* current_thread_state = nullptr;

// Monitor operations never throw while holding `mutex`: throwing runs Java code (exception constructors), which
// could need this monitor or wait for a collection. Waits happen as blocked threads so collections can proceed,
// and `mutex` is always released before leaving the blocked state, because leaving may wait for a collection to
// finish and a running thread waiting for `mutex` would then never reach a safepoint.

void Monitor::enter(Runtime& rt, ThreadState* self) {
    std::unique_lock<std::mutex> lock(mutex);
    if (owner == self) {
        recursion_count++;
        return;
    }
    if (owner == nullptr) {
        owner = self;
        recursion_count = 1;
        return;
    }
    rt.enter_blocking(self);
    entry_cv.wait(lock, [&] { return owner == nullptr; });
    owner = self;
    recursion_count = 1;
    lock.unlock();
    rt.leave_blocking(self);
}

void Monitor::exit(Runtime& rt, ThreadState* self) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (owner == self) {
            if (--recursion_count == 0) {
                owner = nullptr;
                entry_cv.notify_one();
            }
            return;
        }
    }
    rt.throw_new("Ljava/lang/IllegalMonitorStateException;", "current thread does not hold the monitor");
}

bool Monitor::holds_lock(ThreadState* self) {
    std::lock_guard<std::mutex> lock(mutex);
    return owner == self && self != nullptr;
}

void Monitor::wait(Runtime& rt, ThreadState* self, int64_t timeout_ms, int32_t nanos) {
    if (timeout_ms < 0 || nanos < 0 || nanos > 999999)
        rt.throw_new("Ljava/lang/IllegalArgumentException;", "timeout value is negative or nanos out of range");
    std::unique_lock<std::mutex> lock(mutex);
    if (owner != self) {
        lock.unlock();
        rt.throw_new("Ljava/lang/IllegalMonitorStateException;", "current thread does not hold the monitor");
    }
    if (self->interrupted.exchange(false)) {
        lock.unlock();
        rt.throw_new("Ljava/lang/InterruptedException;");
    }

    // Release the monitor completely while waiting, then take it back with the same recursion count.
    uint32_t saved_recursion = recursion_count;
    owner = nullptr;
    recursion_count = 0;
    entry_cv.notify_one();

    rt.enter_blocking(self);
    {
        std::lock_guard wait_lock(self->wait_mutex);
        self->current_waiting_monitor = this;
    }
    if (timeout_ms == 0 && nanos == 0) {
        wait_cv.wait(lock);
    } else {
        wait_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms) + std::chrono::nanoseconds(nanos));
    }
    bool was_interrupted;
    {
        std::lock_guard wait_lock(self->wait_mutex);
        self->current_waiting_monitor = nullptr;
        was_interrupted = self->interrupted.exchange(false);
    }
    entry_cv.wait(lock, [&] { return owner == nullptr; });
    owner = self;
    recursion_count = saved_recursion;
    lock.unlock();
    rt.leave_blocking(self);

    if (was_interrupted) rt.throw_new("Ljava/lang/InterruptedException;");
}

void Monitor::notify(Runtime& rt, ThreadState* self) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (owner == self) {
            wait_cv.notify_one();
            return;
        }
    }
    rt.throw_new("Ljava/lang/IllegalMonitorStateException;", "current thread does not hold the monitor");
}

void Monitor::notify_all(Runtime& rt, ThreadState* self) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (owner == self) {
            wait_cv.notify_all();
            return;
        }
    }
    rt.throw_new("Ljava/lang/IllegalMonitorStateException;", "current thread does not hold the monitor");
}

void ThreadState::park(bool is_absolute, int64_t time) {
    std::unique_lock<std::mutex> lock(park_mutex);
    if (park_permit) {
        park_permit = false;
        return;
    }
    if (interrupted.load(std::memory_order_acquire)) return;
    if (is_absolute && time <= 0) return;  // deadline already passed
    auto ready = [&] { return park_permit || interrupted.load(std::memory_order_acquire); };
    runtime->enter_blocking(this);
    if (time == 0 && !is_absolute) {
        park_cv.wait(lock, ready);
    } else if (is_absolute) {
        auto deadline = std::chrono::system_clock::time_point(std::chrono::milliseconds(time));
        park_cv.wait_until(lock, deadline, ready);
    } else if (time > 0) {
        park_cv.wait_for(lock, std::chrono::nanoseconds(time), ready);
    }
    park_permit = false;
    lock.unlock();
    runtime->leave_blocking(this);
}

void ThreadState::unpark() {
    std::lock_guard<std::mutex> lock(park_mutex);
    park_permit = true;
    park_cv.notify_one();
}

void ThreadState::interrupt() {
    interrupted.store(true, std::memory_order_release);
    unpark();
    std::lock_guard lock(wait_mutex);
    if (current_waiting_monitor) current_waiting_monitor->wait_cv.notify_all();
}

}  // namespace dexe::vm
