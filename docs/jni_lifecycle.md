# JNI Lifecycle — Pitfalls & VDiag Solutions

> **Purpose:** Document the 3 most dangerous JNI pitfalls encountered when building `libvdiag_jni.so` (Boundary 2 in VDiag architecture), and the exact mechanisms VDiag uses to prevent each one.
>
> **Files:** `jni_onload.cpp`, `jni_callback.h`, `jni_callback.cpp`, `jni_bridge.cpp`

---

## 0. JNI reference types — quick primer

Before the pitfalls, understand the 3 reference types:

| Type | Scope | Created by | Must delete? |
|---|---|---|---|
| **Local ref** | Current JNI frame (usually one JNI call) | `FindClass`, `NewObject`, `NewStringUTF`, etc. | No — auto-deleted when JNI call returns |
| **Global ref** | Until explicit `DeleteGlobalRef` | `NewGlobalRef(localRef)` | **YES** — lives forever if not deleted |
| **Weak global ref** | Until GC collects it | `NewWeakGlobalRef(localRef)` | YES — `DeleteWeakGlobalRef` |

**The JVM GC only knows about global refs.** Local refs are invisible beyond their frame. This asymmetry is the root cause of all three pitfalls below.

---

## 1. Pitfall #1 — GlobalRef leak (JVM out-of-memory)

### What goes wrong

```cpp
// ❌ WRONG — jclass and jobject are LOCAL refs by default
jclass clazz = env->FindClass("com/vdiag/IDiagCallback");  // local ref
g_callbackClass = clazz;   // stored globally but still LOCAL — GC can move/clear it

// Later, on a callback:
env->CallVoidMethod(g_callbackClass, ...);  // 💥 undefined behavior or crash
```

A local `jclass` ref is only valid within the JNI call frame. Storing it in a global C++ variable means the next GC cycle can invalidate it silently. No error is thrown — just memory corruption or `SIGSEGV`.

Conversely, calling `NewGlobalRef` every request without ever calling `DeleteGlobalRef` leaks one slot per call until `JNI DETECTED ERROR: global reference table overflow`.

### How VDiag solves it

**`jni_onload.cpp` — cache class + methods once at load time:**
```cpp
jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    g_jvm = vm;                                          // (1) save JavaVM for cross-thread use
    JNIEnv* env;
    vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);

    jclass localClass = env->FindClass("com/vdiag/IDiagCallback");
    g_callbackClass = reinterpret_cast<jclass>(
        env->NewGlobalRef(localClass));                  // (2) promote to GlobalRef
    env->DeleteLocalRef(localClass);                     // (3) delete local — not needed anymore

    g_onResultId = env->GetMethodID(g_callbackClass, "onResult", "(ILjava/lang/String;J)V");
    g_onErrorId  = env->GetMethodID(g_callbackClass, "onError",  "(IILjava/lang/String;)V");
    // jmethodID is not a ref — no GlobalRef needed, safe to cache forever
    return JNI_VERSION_1_6;
}

void JNI_OnUnload(JavaVM* vm, void* /*reserved*/) {
    JNIEnv* env;
    vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    env->DeleteGlobalRef(g_callbackClass);               // (4) balanced delete
    g_callbackClass = nullptr;
}
```

**`jni_callback.cpp` — per-request GlobalRef with RAII:**
```cpp
// Constructor: promote callback object to GlobalRef
JniCallbackBridge::JniCallbackBridge(JNIEnv* env, jobject callback) {
    m_callback = env->NewGlobalRef(callback);   // keeps callback alive across GC
}

// Destructor: always delete — even if onResult was never called
JniCallbackBridge::~JniCallbackBridge() noexcept {
    if (m_callback != nullptr) {
        JNIEnv* env = getEnv();                 // get env for this thread
        env->DeleteGlobalRef(m_callback);       // balanced delete
        m_callback = nullptr;
    }
}

// Move constructor: transfer ownership, set source to null — prevent double-delete
JniCallbackBridge::JniCallbackBridge(JniCallbackBridge&& other) noexcept {
    m_callback = other.m_callback;
    other.m_callback = nullptr;              // source no longer owns the ref
}
```

**Rule:** every `NewGlobalRef` has exactly one `DeleteGlobalRef`. RAII guarantees this even when exceptions or early returns occur.

---

## 2. Pitfall #2 — Calling JNI from a native thread (no JNIEnv)

### What goes wrong

The `JNIEnv*` pointer is **thread-local** — it is only valid on the thread that received it. When the `DiagEngine` worker thread (a pthreads native thread, not an Android Looper thread) tries to fire a callback:

```cpp
// ❌ WRONG — using env captured from a different thread
void worker_callback(JNIEnv* env_from_binder_thread, jobject cb, ...) {
    // env_from_binder_thread is invalid here — this is a different thread
    env_from_binder_thread->CallVoidMethod(cb, ...);  // 💥 JVM crash / ART abort
}
```

ART will immediately abort with: `java.lang.RuntimeException: Thread not attached`.

### How VDiag solves it

**`AttachCurrentThread` + `pthread_key` auto-detach:**

```cpp
// jni_callback.cpp
namespace {
    pthread_key_t  g_detachKey;
    pthread_once_t g_detachKeyOnce = PTHREAD_ONCE_INIT;

    // Called automatically by pthreads when the native thread exits
    void DetachThunk(void* /*arg*/) {
        if (g_jvm != nullptr) {
            g_jvm->DetachCurrentThread();    // balanced Attach/Detach
        }
    }

    void CreateDetachKey() {
        pthread_key_create(&g_detachKey, DetachThunk);
    }
}

JNIEnv* JniCallbackBridge::getEnv() {
    pthread_once(&g_detachKeyOnce, CreateDetachKey);   // one-time key setup

    JNIEnv* env = nullptr;
    jint status = g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);

    if (status == JNI_EDETACHED) {
        // Thread is not attached — attach it now
        g_jvm->AttachCurrentThread(&env, nullptr);

        // Register the detach destructor so pthreads auto-detaches on thread exit
        pthread_setspecific(g_detachKey, reinterpret_cast<void*>(1));
    }
    return env;
}
```

**Why `pthread_key` matters:** if you call `AttachCurrentThread` without a matching `DetachCurrentThread`, the thread entry leaks in the JVM thread table. On a long-running service this becomes a resource exhaustion. The key destructor (`DetachThunk`) fires when the native thread's stack unwinds — guaranteed cleanup with zero manual code at call sites.

---

## 3. Pitfall #3 — Double `DeleteGlobalRef` / use-after-free

### What goes wrong

When `JniCallbackBridge` is stored in a `std::shared_ptr` or passed via lambda, copy semantics can create two owners of the same `jobject`:

```cpp
// ❌ WRONG — bridge is copied into lambda
auto bridge = JniCallbackBridge(env, callback);
engine.submit(req, [bridge](Response r) {     // copy!
    bridge.onResult(...);
});
// bridge (original) destructs → DeleteGlobalRef
// lambda's bridge copy destructs → DeleteGlobalRef again → 💥 JVM abort
```

ART abort: `JNI ERROR: DeleteGlobalRef on invalid global reference`.

### How VDiag solves it

**Copy constructor is explicitly deleted; move constructor transfers ownership:**

```cpp
// jni_callback.h
class JniCallbackBridge {
public:
    JniCallbackBridge(JNIEnv* env, jobject callback);
    ~JniCallbackBridge() noexcept;

    // ✅ copy FORBIDDEN — prevents two owners of the same GlobalRef
    JniCallbackBridge(const JniCallbackBridge&)            = delete;
    JniCallbackBridge& operator=(const JniCallbackBridge&) = delete;

    // ✅ move ALLOWED — transfers ownership, nulls source
    JniCallbackBridge(JniCallbackBridge&&) noexcept;
    JniCallbackBridge& operator=(JniCallbackBridge&&)      = delete;  // move-assign off for simplicity
};
```

Usage in `jni_bridge.cpp`:
```cpp
auto bridge = std::make_shared<JniCallbackBridge>(env, callback);
g_engine->submit(req,
    [bridge](const DiagResponse& resp) {        // capture shared_ptr by value
        bridge->onResult(resp.reqId, resp.value, resp.latencyUs);
    }
);
// shared_ptr ref-count manages lifetime; destructor fires once, exactly once
```

Using `shared_ptr` means the lambda and any other owners share the same bridge instance — `DeleteGlobalRef` happens exactly once when the last `shared_ptr` releases.

---

## 4. Full lifecycle diagram

```
Java side                              C++ side
─────────────────────────────────────  ───────────────────────────────────────
System.loadLibrary("vdiag_jni")
  │
  └──► JNI_OnLoad(JavaVM* vm)
         g_jvm = vm
         FindClass("IDiagCallback")
         g_callbackClass = NewGlobalRef(local)        ← (A) Global: lives until JNI_OnUnload
         DeleteLocalRef(local)
         g_onResultId = GetMethodID(...)              ← not a ref, safe to cache
         return JNI_VERSION_1_6

DiagCarService.onCreate()
  │
  └──► nativeInit("mock")
         HalFactory::create("mock") → g_engine

DiagServiceBinder.getProperty(req, callback)           [Binder thread]
  │
  └──► nativeGetProperty(reqId, propId, callback)
         bridge = make_shared<JniCallbackBridge>(env, callback)
                    └─ NewGlobalRef(callback)          ← (B) Global: ref-count owns it

         g_engine->submit(req, [bridge](...){...})     [queue]

                                       [Engine worker thread]
                                         pop request
                                         encode UDS bytes
                                         hal->sendAndReceive(bytes)
                                         decode response
                                         bridge->onResult(id, value, latency)
                                           getEnv()
                                             JNI_EDETACHED → AttachCurrentThread  ← (C)
                                             pthread_setspecific(key, 1)           ← (D) register detach
                                           CallVoidMethod(m_callback, g_onResultId, ...)
                                         bridge shared_ptr releases
                                           JniCallbackBridge::~dtor()
                                             DeleteGlobalRef(m_callback)           ← (B) balanced
                                       [thread exit someday]
                                         pthread destructor: DetachThunk()
                                           DetachCurrentThread()                   ← (C) balanced

[App process killed / client unbinds]
  DeathRecipient fires → ClientRegistry removes binder
  (bridge already destroyed from engine response — ref count = 0)

System.unloadLibrary or process exit
  JNI_OnUnload(JavaVM* vm)
    DeleteGlobalRef(g_callbackClass)                   ← (A) balanced
    g_jvm = nullptr
```

Every `New*Ref` maps to exactly one `Delete*Ref`. Every `AttachCurrentThread` maps to exactly one `DetachCurrentThread`.

---

## 5. Summary table

| # | Pitfall | Root cause | VDiag mechanism | Where in code |
|---|---|---|---|---|
| **P1** | GlobalRef leak → JVM OOM | `FindClass` returns local ref; never deleted | `JNI_OnLoad` caches as GlobalRef; RAII `JniCallbackBridge` deletes in destructor | `jni_onload.cpp`, `jni_callback.cpp` |
| **P2** | Calling JNI from native thread → ART abort | `JNIEnv*` is thread-local; worker thread has none | `getEnv()` calls `AttachCurrentThread` on miss; `pthread_key` destructor auto-detaches on thread exit | `jni_callback.cpp` |
| **P3** | Double `DeleteGlobalRef` → use-after-free | Copy semantics duplicate `jobject` pointer | Copy ctor `= delete`; move ctor nulls source; `shared_ptr` ensures single destruction | `jni_callback.h`, `jni_bridge.cpp` |

---

## 6. Testing strategy

| Scenario | How to verify |
|---|---|
| GlobalRef leak | Run with `-Xcheck:jni`; check Logcat for `global reference table overflow` |
| Thread attach/detach balance | `adb shell dumpsys meminfo com.vdiag:car_service` — thread count stable after 1000 requests |
| Double-delete | Address Sanitizer (`-fsanitize=address`) on standalone hal/ tests catches use-after-free immediately |
| Move semantics | `static_assert(!std::is_copy_constructible_v<JniCallbackBridge>)` — compile-time guarantee |

---

## 7. Interview talking points

1. **On pitfall #1:** *"JNI có 3 loại reference. `FindClass` trả local ref — chỉ valid trong JNI call đó. Nếu cache vào global C++ variable mà không `NewGlobalRef`, GC có thể move nó bất kỳ lúc nào → crash. VDiag cache trong `JNI_OnLoad` và RAII bridge delete đúng lúc."*

2. **On pitfall #2:** *"Engine worker là pthread native thread — không có `JNIEnv`. Phải `AttachCurrentThread` trước khi gọi Java method. Và phải `DetachCurrentThread` sau, nếu không thread entry leak trong JVM. Tôi dùng `pthread_key_create` để destructor auto-detach khi thread exit — không cần nhớ gọi manual."*

3. **On pitfall #3:** *"Tôi xóa copy constructor của `JniCallbackBridge` để compiler catch bất kỳ copy nào tại compile time. Move constructor chuyển ownership và null source để tránh double-delete. `shared_ptr` đảm bảo destructor chỉ chạy một lần."*

4. **On overall approach:** *"Mỗi pitfall là một boundary mà lifetime model Java và C++ không match. Java có GC, C++ có manual memory. JNI là nơi hai model này giao nhau — phải explicit về ai own gì và cho đến khi nào."*
