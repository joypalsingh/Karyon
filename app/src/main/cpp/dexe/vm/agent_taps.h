#pragma once

// Places where the AI agent (src/agent, only in dexrun) watches the runtime: set by the agent when it starts, null in
// packaged apps, where each costs one relaxed atomic load. A tap runs on the thread where the event happens, in the
// running state: it must be quick, must not allocate Java objects, call Java or throw, and must not take a lock that a
// thread could hold across a safepoint.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "vm/object.h"

namespace dexe::vm {

class Runtime;

struct AgentTaps {
    // Java code caught an exception (the handler in `catcher` runs next).
    void (*exception_caught)(Runtime& rt, Object* exception, Method* catcher);
    // Java code wrote to standard output (fd 1) or standard error (fd 2): System.out and System.err.
    void (*output)(int fd, const char* data, size_t length);
    // A hooked method was called (method_hooks.cpp): one line describing the call and what came of it.
    void (*hook_called)(uint64_t hook_id, const std::string& text);
};

inline std::atomic<const AgentTaps*> g_agent_taps{nullptr};

inline const AgentTaps* agent_taps() { return g_agent_taps.load(std::memory_order_relaxed); }

// ---- method hooks (docs/ai-agent.md, section 7.2) ----

// What a hooked method does instead of (or around) its body.
enum class HookAction : uint8_t {
    kLog,     // runs as usual; each call, its arguments and its result are logged
    kReturn,  // returns `value` without running (the default value: a stub, "deleting" the method)
    kThrow,   // throws a new `exception` with `message` without running
};

// Set on Method::hook by the agent. Published once and never changed or freed while the process runs (a call may be
// reading it on another thread when the agent removes it), so the agent keeps every hook it ever made.
struct MethodHook {
    uint64_t id = 0;
    HookAction action = HookAction::kLog;
    bool log = true;              // also log calls of kReturn and kThrow hooks
    Value value{};                // kReturn: a primitive result
    bool string_value = false;    // kReturn of a reference type: a new string `text` (else null)
    std::string text;             // kReturn: the string; kThrow: the message
    std::string exception;        // kThrow: the exception class's descriptor
};

// True once any method was ever hooked: Runtime::call looks at Method::hook only then.
inline std::atomic<bool> g_method_hooks_used{false};

// How far the agent moved the app's wall clock (env.time), in milliseconds: added to System.currentTimeMillis and
// java.time's clock. Monotonic clocks (nanoTime, SystemClock.uptimeMillis) are not moved, as on a phone whose user
// sets the time.
inline std::atomic<int64_t> g_clock_offset_ms{0};

}  // namespace dexe::vm
