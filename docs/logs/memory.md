# Memory Work Log

## [2026-04-14 11:00] - Long-run memory investigation plan and runtime instrumentation

- **Agent**: Codex
- **Task**: Learn the `imager` / `imagestore` memory behavior, document a concrete investigation process, then add runtime instrumentation that can distinguish RSS growth from live library-owned buffer growth.
- **Outcome**:
  - Added a dedicated investigation plan at [docs/plan/0018.MEMORY.md](/home/vibe/imager/imager/docs/plan/0018.MEMORY.md). The plan explains why leak checks were insufficient, identifies the main suspects (`std::async` dispatch behavior, heavy validator backends, allocator retention, `ErrorFile` accumulation), and defines a staged diagnostic workflow.
  - Added a new opt-in runtime reporter:
    - [imagestore/MemoryReporter.h](/home/vibe/imager/imager/imagestore/MemoryReporter.h)
    - [imagestore/MemoryReporter.cpp](/home/vibe/imager/imager/imagestore/MemoryReporter.cpp)
  - Wired the reporter into the `imagestore` executable in:
    - [imagestore/main.cpp](/home/vibe/imager/imager/imagestore/main.cpp)
    - [imagestore/CMakeLists.txt](/home/vibe/imager/imager/imagestore/CMakeLists.txt)
  - Added two CLI switches:
    - `--memory-report`
    - `--memory-interval N`
  - The reporter samples Linux process memory from `/proc/self/status` and emits:
    - current RSS (`VmRSS`)
    - peak RSS (`VmHWM`)
    - `blobs_alive`
    - `blob_bytes_alive`
    - aggregate in-flight file count
    - aggregate in-flight bytes
    - thread-pool queue depth / active threads
    - processing counters (`processed`, `added`, `duplicates`, `errors`, `skipped`)
  - Output is routed through `ResultLog`, so it coexists with both normal line mode and verbose TTY scrolling mode without bypassing the existing output synchronization.
  - `--quiet` and `--memory-report` are treated as mutually exclusive because the memory stream is itself diagnostic output.
  - Verification:
    - `cmake --build --preset default` passed
    - `ctest --preset default --output-on-failure -R 'imagestore_(cli_tests|output_tests|memcheck)'` passed
- **Rationale**:
  - The existing `Blob` metrics already track the library’s explicit file-buffer ownership. Adding process RSS and HWM beside them makes it possible to answer the key question: "Is RSS growing because our live buffers are growing, or because something else retains memory?"
  - The instrumentation is intentionally opt-in so normal imports stay unchanged.
- **Next Step**:
  - Run representative long imports with:
    - `--memory-report --memory-interval 5 --jobs 1`
    - `--memory-report --memory-interval 5 --jobs <normal>`
    - isolated per-format workloads
  - Compare RSS against `blob_bytes_alive` to classify the growth source before using heavier external profilers.
