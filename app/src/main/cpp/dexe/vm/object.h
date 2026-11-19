#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "dex/dex_file.h"
#include "dex/instruction.h"

namespace dexe::vm {

struct Class;
struct Method;
struct MethodHook;  // agent_taps.h
struct Object;
class Runtime;

// A Java value of any type. Which member is valid is determined by the type signature in context.
union Value {
    uint64_t raw;
    int32_t i;
    int64_t j;
    float f;
    double d;
    Object* l;
};

inline Value int_value(int32_t v) { Value x{}; x.i = v; return x; }
inline Value long_value(int64_t v) { Value x{}; x.j = v; return x; }
inline Value float_value(float v) { Value x{}; x.f = v; return x; }
inline Value double_value(double v) { Value x{}; x.d = v; return x; }
inline Value ref_value(Object* v) { Value x{}; x.l = v; return x; }
inline Value bool_value(bool v) { return int_value(v ? 1 : 0); }

// Number of 64-bit slots taken by java.lang.Object's own fields (shadow$_klass_ and shadow$_monitor_).
// Every object, arrays included, starts with them; Runtime verifies this when it loads java.lang.Object.
constexpr uint32_t kObjectSlots = 2;

// Header of every heap object: the runtime's class pointer, followed by one 64-bit slot per instance field.
// Primitive fields occupy the low bytes of their slot (little-endian), so a field's byte offset
// (8 + 8 * slot) can also be used for raw memory access, as sun.misc.Unsafe does.
struct Object {
    Class* klass;
    uint64_t* slots() { return reinterpret_cast<uint64_t*>(this + 1); }
    Object*& ref_slot(uint32_t i) { return *reinterpret_cast<Object**>(slots() + i); }
    int64_t& long_slot(uint32_t i) { return *reinterpret_cast<int64_t*>(slots() + i); }
};
static_assert(sizeof(Object) == 8);

inline uint32_t slot_offset(uint32_t slot) { return uint32_t(sizeof(Object) + 8 * slot); }

// Arrays: the java.lang.Object slots, the length, then the elements packed at their natural size.
struct Array : Object {
    uint64_t object_slots_[kObjectSlots];
    int32_t length;
    uint32_t padding_;
    template <typename T>
    T* data() { return reinterpret_cast<T*>(this + 1); }
};
static_assert(sizeof(Array) == 32);

// Implementation of a native (host-implemented) method. `args` holds `this` first for instance methods.
using NativeFn = Value (*)(Runtime& rt, Value* args);

struct Field {
    Class* owner = nullptr;
    std::string name;
    std::string type;  // descriptor
    uint32_t access = 0;
    uint32_t index = 0;     // position in owner->fields
    uint32_t slot = 0;      // instance fields: slot index within the object
    uint32_t dex_field_idx = dex::kNoIndex;  // in the defining dex file (for its annotations)
    Value static_value{};   // static fields: the value

    bool is_static() const { return access & dex::kAccStatic; }
    bool is_reference() const { return type[0] == 'L' || type[0] == '['; }
};

// A method of a java.lang.reflect.Proxy class (see Runtime::create_proxy_class). The constructor delegates to
// `prototype`, Proxy(InvocationHandler); every other method is an interface (or Object) method, `prototype`, whose
// calls go to the proxy's InvocationHandler. `exceptions` are the checked exceptions the method may throw.
struct ProxyInfo {
    Method* prototype = nullptr;
    std::vector<Class*> exceptions;
};

struct DexCache;

// A replacement for a method's code, set by the AI agent (docs/ai-agent.md, section 7.3): the code item, its decoded
// instructions, and the dex file and dex cache its indices resolve in (a patch dex the agent loaded). interpret()
// takes the body once per call, so a call that is running keeps its body when a new one is published; bodies are never
// freed while the process runs.
struct MethodBody {
    uint64_t id = 0;
    const dex::DexFile* dex = nullptr;
    DexCache* cache = nullptr;
    dex::CodeItem code;
    std::vector<dex::Instruction> decoded_insns;
    mutable std::atomic<uint8_t> registers_checked{0};  // as Method::registers_checked, for this body (a cache)
};

struct Method {
    Class* owner = nullptr;
    std::string name;
    std::string signature;  // "(I)V"
    std::string shorty;     // "VI": return type then parameters, with all references as 'L'
    uint32_t access = 0;
    uint32_t dex_method_idx = dex::kNoIndex;
    const dex::DexFile* dex = nullptr;
    std::optional<dex::CodeItem> code;
    NativeFn native = nullptr;
    void* jni_fn = nullptr;     // registered through JNI RegisterNatives (see jni_env.cpp)
    bool jni_critical = false;  // @CriticalNative: called without JNIEnv and class
    bool jni_fast = false;      // @FastNative: runs as Java code does (runnable), not in the native state
    bool jni_guest = false;     // jni_fn is an app library's code (guest_native.h): System V x86-64 (sysv_call)
    bool jni_arm64 = false;     // with jni_guest: ARM64 code, called on the guest CPU (call_arm64_native)
    std::shared_ptr<const ProxyInfo> proxy;  // set for the methods of proxy classes

    bool is_static() const { return access & dex::kAccStatic; }
    bool is_constructor() const { return name == "<init>"; }
    std::string pretty() const;

    uint32_t vtable_index = UINT32_MAX;
    std::vector<dex::Instruction> decoded_insns;
    // Whether every pre-decoded instruction's registers are in range (interpreter.cpp, checked once, at the first
    // call): 0 not checked yet, 1 they are (the interpreter skips its per-instruction checks), 2 some are not.
    std::atomic<uint8_t> registers_checked{0};
    // The AI agent's hook on this method (agent_taps.h), or null: always null outside dexrun's developer builds.
    std::atomic<const MethodHook*> hook{nullptr};
    // The agent's replacement for `code` (MethodBody), or null: the method runs the code from its own dex file.
    std::atomic<const MethodBody*> body{nullptr};
};

enum class ClassState { kLoaded, kInitializing, kInitialized, kError };

// How a java.lang.ref.Reference subclass holds its referent, which decides when the collector clears it:
// soft referents survive until memory runs short, weak ones until they are otherwise unreachable, finalizer
// referents are kept alive for their finalize() call, and phantom ones are cleared after finalization.
enum class ReferenceKind : uint8_t { kNone, kSoft, kWeak, kFinalizer, kPhantom };

struct DexCache;

struct Class {
    std::string descriptor;
    Class* super = nullptr;
    std::vector<Class*> interfaces;
    uint32_t access = 0;
    std::vector<std::unique_ptr<Field>> fields;    // declared static and instance fields
    std::vector<std::unique_ptr<Method>> methods;  // declared methods
    uint32_t instance_slots = 0;                   // including inherited fields
    std::vector<uint32_t> reference_slots;         // instance field slots holding object references
    ReferenceKind reference_kind = ReferenceKind::kNone;  // for java.lang.ref.Reference and its subclasses
    bool is_finalizable = false;  // overrides Object.finalize(); instances are registered with FinalizerReference
    ClassState state = ClassState::kLoaded;
    std::thread::id init_thread{};
    char primitive = 0;          // 'I', 'Z', ... for primitive classes
    Class* component = nullptr;  // element type for array classes
    const dex::DexFile* dex = nullptr;
    const dex::ClassDef* def = nullptr;
    DexCache* cache = nullptr;
    // The defining class loader (Class.getClassLoader), null for the boot class path. Classes are kept per loader
    // (Runtime::find_class); an array class belongs to its component's loader. Kept alive by the class's mirror.
    Object* loader = nullptr;
    Object* mirror = nullptr;  // the java.lang.Class instance
    std::vector<Method*> vtable;

    bool is_interface() const { return access & dex::kAccInterface; }
    bool is_array() const { return component != nullptr; }
    Method* find_declared_method(std::string_view name, std::string_view signature) const;
    Field* find_declared_field(std::string_view name, std::string_view type) const;
    Field* find_declared_field(std::string_view name) const;
};

// Size in bytes of an array element for the given component descriptor.
size_t element_size(char component_descriptor_char);

// Builds a shorty ("VIL") from a method signature ("(ILjava/lang/String;)V").
std::string shorty_from_signature(std::string_view signature);

}  // namespace dexe::vm
