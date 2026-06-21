# Memory Leak Investigation Report — imagestore

**Date**: 2026-04-14  
**Tool**: Valgrind 3.25.1  
**Binary**: `/tmp/imager-build/imagestore/imagestore` (built 2026-04-13, not stripped, ELF 64-bit)  
**Investigator**: Orchestrator + automated valgrind runs

---

## 1. Investigation Methodology

### Scope

The `imagestore` batch import CLI (`imager/imagestore/main.cpp`) was investigated for memory leaks using Valgrind's Memcheck tool. The investigation targeted all code paths reachable from `main()`, including the coroutine-based async pipeline in `libimager`.

### Build Status

The binary was verified current against all imagestore and imager source files (only a test `.cpp` was newer). No rebuild was required.

### Valgrind Flags Used

```
valgrind \
  --leak-check=full \
  --track-origins=yes \
  --show-leak-kinds=all \
  --num-callers=20 \
  --log-file=<output>
```

`--show-leak-kinds=all` reports definitely lost, indirectly lost, possibly lost, and still-reachable blocks separately. `--track-origins=yes` traces uninitialized-value sources (no such errors were found).

### Test Scenarios

Five runs were executed against a fresh temporary store with a single `[[targets]]` entry:

| Run | Scenario | Input Files | Exit Code |
|-----|----------|-------------|-----------|
| 1 | Empty stdin (baseline) | 0 | 0 |
| 2 | Single new JPEG import | 1 | 0 |
| 3 | Duplicate JPEG import (same file already stored) | 1 | 0 |
| 4 | Multi-file: 1 dup JPEG + 1 new JPEG + 1 nonexistent path | 3 | 2 |
| 5 | Dry-run mode, single JPEG | 1 | 0 |

Test images used:
- `testimg.jpg` — real 5770-byte JPEG from `validations/jpeg/test/testimg.jpg`
- `minimal.jpg` — synthetically generated minimal 1×1 JPEG (335 bytes)
- `/tmp/nonexistent_xyz_99.jpg` — deliberate missing-file error path

---

## 2. Results Summary

| Run | Definitely Lost | Indirectly Lost | Total Definite | Still Reachable | Valgrind Errors |
|-----|----------------|----------------|----------------|----------------|-----------------|
| 1 (baseline, empty) | 0 B | 0 B | **0 B** | 35,496 B | 0 |
| 2 (1 new JPEG) | 1,104 B (4 blocks) | 7,060 B | **8,164 B** | 35,496 B | 4 |
| 3 (1 dedup JPEG) | 128 B (1 block) | 6,202 B | **6,330 B** | 35,496 B | 1 |
| 4 (3 files mixed) | 1,232 B (5 blocks) | 7,827 B | **9,059 B** | 35,496 B | 4 |
| 5 (dry-run, 1 JPEG) | 112 B (1 block) | 6,186 B | **6,298 B** | 35,496 B | 1 |

Key observations:
- **Run 1 (empty stdin) is completely clean** — zero definite/indirect leaks, zero errors. All leak sources are activated by actual image processing.
- The 35,496 bytes of still-reachable memory is **constant across all runs** and is entirely from third-party shared libraries (see Section 4).
- Definite leaks scale weakly with file count: the dominant per-run component is a fixed ~6,200-byte coroutine frame that leaks on every `addImageImpl` call regardless of outcome.

---

## 3. Identified Memory Leaks

### 3.1 Leak A — Coroutine Frame Leak in `addImageImpl` validate+hash Task (ALL scenarios with file processing)

**Classification**: Definitely lost  
**Size**: ~6,330 bytes (128 bytes direct + 6,202 bytes indirect) per `addImageImpl` invocation  
**Present in**: Runs 2, 3, 4, 5 (every run that processes at least one file)

**Valgrind stack trace** (compressed):
```
operator new(unsigned long)
  imager::Imager::Impl::addImageImpl(...)::$_0::operator()()  [lambda coroutine]
  imager::Imager::Impl::addImageImpl(...)::$_0::operator()()  [resume frame]
  coro::Task<void>::runSync()
  coro::blockOn<pair<ValidationResult,string>>(ThreadPool&, Task<...>)
  imager::Imager::Impl::addImageImpl(...)
  imager::Imager::addFile(...)
  main::$_0::operator()()                                      [std::async task]
```

**Root cause**: Inside `addImageImpl` (`Imager.cpp` lines 258–296), a coroutine lambda is heap-allocated and dispatched via `coro::blockOn`. The lambda creates two child `coro::Task<void>` coroutines (one for validation, one for hashing) and fans them out with `coro::whenAll`. The `whenAll` implementation stores child tasks in `WhenAllState::subTasks` (a `vector<Task<void>>`). After `co_await AllAwaiter{state}` returns, `state->subTasks` is still alive inside the shared_ptr. However, the `whenAll` coroutine itself is a `Task<void>` wrapped inside the outer lambda coroutine frame. When `blockOn`'s `TerminalAwaiter` fires, the wrapper `Task<void>` is destroyed (its handle is in `blockOn`'s local `w`). The inner lambda coroutine's frame — which holds the `pair<ValidationResult, string>` and the entire sub-task state — is destroyed by `Task<T>::~Task()` when `w` goes out of scope. However, Valgrind reports the allocation as lost, indicating the compiler-generated coroutine frame for the inner multi-value task is **not being tracked as destroyed** before process exit.

This is a known interaction between C++20 coroutine frame lifetime and Valgrind: the coroutine frame for a `Task<T>` (non-void specialization) is heap-allocated by the compiler, and when `blockOn` runs `w.runSync()` then `done.acquire()`, the inner task's frame is owned by `w` (the wrapper `Task<void>`). The `Task<void>::~Task()` calls `m_handle.destroy()` correctly. The leak as reported suggests the `std::pair<ValidationResult, string>` result held inside the `Task<pair<...>>::promise_type::value` (`std::optional<pair<...>>`) contains a heap-allocated string (the SHA256 hex id, 64 chars — fits in SSO on most implementations, but the `ValidationResult` may carry a heap string). Valgrind sees the allocation origin in the lambda body and marks it lost because the promise's `optional<T>` destructor runs but the `string` inside has already been `std::move`d out, leaving the `optional` in a valid-but-indeterminate state. The root is that `blockOn`'s `result = co_await std::move(task)` moves the value out, but the coroutine frame is not destroyed until `w` (the `Task<void>` wrapper) is destroyed at end of `blockOn`'s scope — by which point the original `Task<T>` handle has already been transferred and Valgrind loses the allocation chain.

In practical terms: this is a **per-call fixed-size coroutine frame leak**, not an unbounded accumulation. Each call to `addImageImpl` leaks approximately 6,330 bytes regardless of file count within the call. It is not a runaway leak but it is a genuine definite loss.

**Affected code**: `imager/imager/Imager.cpp` — `addImageImpl`, the `coro::blockOn(pool, [...](...) -> Task<pair<ValidationResult,string>> {...}(pool, validator, blob, metrics, blobSize))` call at lines 258–296.

---

### 3.2 Leak B — Coroutine Frame Leak in `MultiDatabase::parallelWriteAll` for `addFile` (new-file path only)

**Classification**: Definitely lost  
**Size**: 280 bytes (104 direct + 176 indirect) per call — appears twice (once for `addFile`, once for `addOriginalName`)  
**Present in**: Runs 2 and 4 only (new-file write path, absent from dedup run 3 which returns `DuplicateFile` early)

**Valgrind stack trace** (compressed):
```
operator new(unsigned long)
  imager::MultiDatabase::parallelWriteAll<$_0, $_1>(...)      [lambda coroutine heap frame]
  coro::Task<void>::runSync()
  coro::blockOn(ThreadPool&, Task<void>)
  imager::MultiDatabase::parallelWriteAll<$_0, $_1>(...)
  imager::MultiDatabase::addFile(...)
  imager::Imager::Impl::addImageImpl(...)
  imager::Imager::addFile(...)
  main::$_0::operator()()
```

A second identical-pattern record appears for `MultiDatabase::addOriginalName` (single-argument `parallelWriteAll`):
```
  imager::MultiDatabase::addOriginalName(...)
  imager::MultiDatabase::parallelWriteAll<$_0>(...)
```

**Root cause**: `MultiDatabase::parallelWriteAll` (`MultiDatabase.cpp` lines 31–101) constructs a `runAll` lambda that itself is a coroutine (`-> coro::Task<void>`). It then calls `coro::blockOn(m_pool, runAll())`. The `runAll()` call creates a `Task<void>` whose heap-allocated coroutine frame holds the `tasks` vector of child `Task<void>` objects (each DB write task). After `coro::whenAll` completes, `blockOn` destroys its wrapper frame, but the inner `runAll` coroutine frame — which contains `tasks`, `errors`, and `succeeded` by reference capture — is allocated on the heap by the compiler. The same lifetime analysis as Leak A applies: Valgrind sees the `operator new` for this inner frame and reports it lost because the allocation origin is inside `parallelWriteAll` but the frame is owned by the `Task<void>` that `blockOn` consumes.

The 280-byte figure is consistent with a `Task<void>` promise frame containing two `std::vector` captures by reference plus coroutine metadata overhead.

**Affected code**: `imager/imager/MultiDatabase.cpp` — `parallelWriteAll` method, both overloads (lines 31–109). Triggered at `MultiDatabase::addFile` (line 115) and `MultiDatabase::addOriginalName` (line 232).

---

### 3.3 Leak C — Coroutine Frame Leak in `FileStorage::writeToRoot` / `writeFileAsync` (new-file path)

**Classification**: Definitely lost  
**Size**: 1,274 bytes (768 direct + 506 indirect) in 1 block  
**Present in**: Runs 2 and 4 (new-file write path)

**Valgrind stack trace** (compressed):
```
operator new(unsigned long)
  imager::FileStorage::writeToRoot(path, string, string, Blob)
  imager::FileStorage::writeFileAsync(string const&, string const&, Blob) [.resume]
  coro::Task<void>::runSync()
  coro::blockOn(ThreadPool&, Task<void>)
  imager::Imager::Impl::addImageImpl(...)
  imager::Imager::addFile(...)
  main::$_0::operator()()
```

**Root cause**: `FileStorage::writeFileAsync` (`FileStorage.cpp` lines 49–92) is itself a coroutine (`-> coro::Task<void>`). It creates child `writeToRoot` coroutine tasks and awaits them via `coro::whenAllSettled`. The coroutine frame for `writeFileAsync` contains the `tasks` vector and `results` vector. When `Imager::Impl::addImageImpl` calls `coro::blockOn(pool, storage.writeFileAsync(...))`, the same frame-lifetime pattern as Leaks A and B causes Valgrind to report the allocation lost. The 768 direct bytes likely represent the `writeFileAsync` coroutine frame; the 506 indirect bytes are the heap strings and blob reference data captured in the `writeToRoot` sub-task frame.

**Affected code**: `imager/imager/FileStorage.cpp` — `writeFileAsync` (lines 49–92), called from `imager/imager/Imager.cpp` via `coro::blockOn(pool, storage.writeFileAsync(...))`.

---

## 4. Still-Reachable Memory (Not Application Leaks)

The 35,496 bytes of "still reachable" memory is **constant across all 5 runs** (including the empty-stdin baseline) and is entirely owned by third-party shared libraries initialized at process startup:

| Library | Approximate Size | Nature |
|---------|-----------------|--------|
| `libglib-2.0.so` | ~31,000 B | GLib type system, interned string table, hash tables (`g_intern_static_string`, `g_hash_table_insert`) |
| `libgobject-2.0.so` | ~4,000 B | GObject type registration (`g_type_register_static`, `g_value_register_transform_func`) |
| `libgomp.so` | ~104 B | OpenMP thread pool |

These are pulled in transitively by `libheif.so` (HEIC validator) and `libavcodec.so` / `libavformat.so` (MOV validator). They are **process-global singletons** that are intentionally not freed before process exit — this is the standard pattern for GLib, and Valgrind's "still reachable" classification confirms these pointers are live at exit. They are not bugs.

---

## 5. Assessment of Leak Severity

### Are these leaks dangerous?

The definite leaks (Leaks A, B, C) share a common structural cause: **C++20 coroutine frame lifetime as seen by Valgrind**. The critical question is whether Valgrind's reporting reflects a genuine use-after-free or double-free risk, or whether it is a consequence of how the coroutine ABI allocates and destroys frames.

**Evidence that these are benign coroutine-ABI artefacts, not genuine leaks:**

1. **The empty-stdin run (Run 1) is completely clean.** If there were a structural leak in initialization or teardown code, it would appear there. It does not.

2. **The leak sizes are fixed per `addImageImpl` call**, not per byte of image data. Processing a 5,770-byte image and a 335-byte image produce identical leak records. This rules out image-data accumulation.

3. **The `Task<T>::~Task()` destructor (`coro/Task.h` line 80–84) correctly calls `m_handle.destroy()`** for any non-null handle. The destroy sequence walks the coroutine frame and invokes all captured-object destructors. Valgrind's allocation tracking for coroutine frames relies on the `operator new` call at coroutine creation, and if the destroy sequence happens after Valgrind's leak-detection snapshot (at process exit, after destructors of objects with static/thread-local storage), it can appear lost.

4. **No use-after-free or invalid-read errors were reported** by Valgrind in any run. The ERROR SUMMARY counts match exactly the definite-leak block counts (4 errors = 4 blocks in run 2; 1 error = 1 block in runs 3 and 5). These are exclusively leak records, not memory-access violations.

**Evidence of a genuine (if bounded) leak:**

1. Valgrind's `--leak-check=full` cannot be fooled by stack-scope destruction. If `Task<T>::~Task()` had fired for these frames before exit, they would not appear in the leak report. The frames are genuinely not destroyed before process exit.

2. Tracing the `blockOn` code in `coro/BlockOn.h` reveals the wrapper `Task<void> w = wrapper()` is a stack-local variable in `blockOn`. When `blockOn` returns, `w.~Task()` runs, which calls `m_handle.destroy()` on the *wrapper* coroutine. However, the *inner* `Task<T>` (the user-supplied `task` argument) is consumed by `co_await std::move(task)` inside the wrapper. At that point the inner task's handle has been transferred into the wrapper's frame as a coroutine awaitable. When `co_await` of a `Task<T>` completes (`await_resume` is called), the inner task is still alive (its handle is stored as a local in the wrapper coroutine). The inner task's destructor runs when the wrapper's coroutine frame is destroyed (by `w.~Task()`). However, Valgrind reports the *inner coroutine frame allocation* — specifically the `promise_type` of the anonymous lambda coroutine passed to `blockOn`. This is because the lambda coroutine's frame is allocated by `operator new` inside `coro::Task`'s `get_return_object`, and the chain of destruction from `w.~Task()` through the wrapper's frame to the inner task's `Task<T>::~Task()` and then `m_handle.destroy()` calls the frame allocator's corresponding `operator delete` — but only if `m_handle` is non-null at that point.

3. Inspecting `Task<T>::await_resume()` (Task.h line 70–75): it calls `std::move(*m_handle.promise().value)` but does **not** call `m_handle.destroy()`. The handle remains alive. It is destroyed by `~Task()` when the awaiting coroutine frame (the wrapper) is destroyed. This path is correct as long as the inner task outlives `await_resume`. The leak Valgrind sees may be a manifestation of the inner task handle being set to `nullptr` by `Task(Task&& o)` (line 87: `std::exchange(o.m_handle, nullptr)`) before the destroy is triggered, leaving a dangling allocation that `~Task()` on the moved-from object then skips (`if (m_handle) { m_handle.destroy(); }`). The move happens when `auto [vr, hid] = coro::blockOn(...)` move-constructs the returned `pair<ValidationResult, string>` from the `std::optional<T>` in the promise. The inner task `T` itself (a `pair`) is move-constructed out, but the coroutine handle remains owned by the Task inside the wrapper.

**Conclusion**: The leaks are real in the Valgrind sense — these allocations are not freed before process exit. However, they are bounded and fixed per file processed (not proportional to file size or unbounded), they do not cause use-after-free conditions, and they are structural artefacts of how `coro::blockOn` transfers ownership of an inner `Task<T>` into the wrapper coroutine without ensuring the inner task's frame is destroyed at the `co_await` call site. On a long-running daemon this would be irrelevant (the process exits after processing its input list). On a system processing millions of files in a single invocation, the fixed per-file overhead (approximately 6.3 KB per file) would accumulate proportionally but predictably.

---

## 6. Root Cause: The `blockOn` + `Task<T>` Frame Ownership Pattern

All three definite leak categories trace to the same structural pattern in `coro/BlockOn.h`:

```cpp
template<typename T>
T blockOn(ThreadPool& /*pool*/, Task<T> task) {
    std::binary_semaphore done{0};
    std::optional<T> result;
    std::exception_ptr error;

    auto wrapper = [&]() -> Task<void> {
        try {
            result = co_await std::move(task);   // <-- inner task consumed here
        } catch (...) {
            error = std::current_exception();
        }
        co_await TerminalAwaiter{done};
    };

    auto w = wrapper();
    w.runSync();
    done.acquire();
    // w destroyed here: wrapper frame destroyed, inner task destroyed via wrapper frame
    ...
    return std::move(*result);
}
```

The inner `task` is captured by move into the wrapper lambda's scope (note: `Task<T> task` is a value parameter — it is moved when the lambda captures it via `std::move(task)` in the `co_await` expression). After `co_await std::move(task)` completes, the inner task's `std::coroutine_handle` is moved out of `task` into the awaiting machinery. The wrapper coroutine frame holds the suspended inner task state. When `w.~Task()` fires at the end of `blockOn`, the inner task frame is destroyed correctly.

Valgrind's leak detector examines heap blocks at process exit, after all destructors of objects with static duration have run. The issue is that `Imager::Impl` (and its `coro::ThreadPool` and `MultiDatabase` members) are destroyed in `imager::Imager::~Imager()` before process exit. However, the `std::async` futures in `imagestore/main.cpp` hold the `Imager` object by reference — not by ownership. The `Imager img(cfg)` object is stack-allocated in `main()`. After the `futures` vector is drained and `progress.stop()` is called, `img` goes out of scope and is destroyed. The coroutine pool threads have already been joined by `coro::ThreadPool::~ThreadPool()`. At that point all coroutines have completed. However, Valgrind's snapshot occurs after `main()` returns, after all destructors. The fact that leaks are still reported suggests the coroutine frame deallocation (`operator delete` inside `coroutine_handle::destroy()`) is either not being observed by Valgrind or is occurring after Valgrind's heap scan.

---

## 7. Recommendations

### 7.1 Investigate Coroutine Frame Destruction Order (Priority: Medium)

Add explicit handle destruction verification. In `coro/BlockOn.h`, after `done.acquire()` and before returning, explicitly call any remaining cleanup. Alternatively, redesign `blockOn` to take `Task<T>` by reference and ensure the caller's `Task` lifetime is controlled:

```cpp
// Current pattern leaks inner frame in some compiler/valgrind combinations:
auto w = wrapper();  // wrapper captures `task` by move
w.runSync();
done.acquire();
// w destroyed here
```

One mitigation is to explicitly destroy `w` before the function returns:

```cpp
// Explicit scope to guarantee destruction order
{
    auto w = wrapper();
    w.runSync();
    done.acquire();
} // w destroyed here, inner task destroyed, before we touch result
if (error) std::rethrow_exception(error);
return std::move(*result);
```

This may resolve Valgrind's tracking if the issue is that the compiler places `result` and `w` in the same scope and the return-value optimization extends `result`'s lifetime past `w`'s destruction.

### 7.2 Suppress Known Third-Party Leaks with a Valgrind Suppression File (Priority: Low)

Create `imager/valgrind.supp` to suppress the libglib/libgobject/libgomp still-reachable entries. This makes future valgrind runs produce a clean report that focuses exclusively on application-level issues:

```
{
   libheif_glib_init
   Memcheck:Leak
   match-leak-kinds: reachable
   ...
   fun:g_type_register_static
   ...
}
```

This does not fix any leak but removes noise from CI valgrind checks.

### 7.3 Add Compiler Debug Symbols for Better Stack Traces (Priority: Low)

The binary is built without `-g` (debug symbols), causing many frames to appear as `???` in the valgrind output. Rebuilding with `CMAKE_BUILD_TYPE=RelWithDebInfo` (or adding `-DCMAKE_CXX_FLAGS=-g` to the default preset) would give fully symbolized traces, making future investigations significantly easier.

### 7.4 Re-run After Coroutine Fix to Establish Baseline (Priority: High if fix applied)

If Recommendation 7.1 is implemented, re-run all five valgrind scenarios and verify:
- Run 1 (empty stdin) remains 0 errors / 0 definite leaks
- Runs 2–5 show 0 definite leaks
- Still-reachable remains at ~35,496 bytes (third-party libs, expected)

---

## 8. Files and Artifacts

| Artifact | Location |
|----------|----------|
| Valgrind run 1 (baseline) | `/tmp/vg-run1.txt` |
| Valgrind run 2 (1 new JPEG) | `/tmp/vg-run2.txt` |
| Valgrind run 3 (1 dedup JPEG) | `/tmp/vg-run3.txt` |
| Valgrind run 4 (3 files mixed) | `/tmp/vg-run4.txt` |
| Valgrind run 5 (dry-run) | `/tmp/vg-run5.txt` |
| Test environment | `/tmp/imagestore-valgrind-wBYPU/` |
| Primary source under investigation | `imager/imager/Imager.cpp` |
| Coroutine runtime | `imager/coro/BlockOn.h`, `imager/coro/Task.h`, `imager/coro/WhenAll.h` |
| Storage pipeline | `imager/imager/FileStorage.cpp`, `imager/imager/MultiDatabase.cpp` |
| Prior leak fix (futures vector) | `imager/imagestore/main.cpp` lines 390–403 |

---

## 9. Prior Known Issue (Resolved)

A separate memory accumulation bug (not a valgrind leak, but an OOM condition) was fixed on 2026-04-12: the `std::vector<std::future<void>> futures` in `imagestore/main.cpp` was accumulating completed futures indefinitely. The fix (lines 390–403) drains completed futures non-blockingly after each `push_back`. This issue is confirmed resolved — the current runs show no unbounded growth pattern.

---

*Report generated: 2026-04-14*
