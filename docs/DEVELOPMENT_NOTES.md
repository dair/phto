# Developer Notes — non-obvious implementation knowledge

Cross-cutting facts that are easy to get wrong and aren't obvious from the code
or the per-feature plan docs, distilled from prior debugging/implementation work.
For build/run basics see the top-level [`README.md`](../README.md) and
[`CLAUDE.md`](../CLAUDE.md); for test conventions see
[`docs/testing/CONVENTIONS.md`](testing/CONVENTIONS.md).

## `Imager::addFile` has two write paths (small vs large)

`Imager::addFile` routes on the streaming threshold (`kStreamingThreshold`, 256 MB):

- **< 256 MB:** the whole file is read into a `Blob`, then `addImageImpl(blob, …)`
  → `writeFileAsync` → `writeToRoot`. **The source path is not passed into
  `FileStorage` on this path.**
- **> 256 MB:** `addFileLarge` → `writeFileFromDiskAsync` → `writeToRootFromDisk`,
  which *does* receive the source path.

Implication: any post-ingest step that needs the original source file (timestamp
preservation, extra checksums, …) must be hooked at the `Imager::addFile` /
`addFileLarge` level — after `addImageImpl`/streaming returns `Ok` — **not** inside
`FileStorage`. Hooking only `writeToRootFromDisk` silently skips the common
small-file path.

**atime trap:** reading the source file in order to ingest it updates the file's
atime. To preserve the original atime, read the timestamps *before* opening any
`ifstream` and re-apply them after a successful write. See plan
[0020.TIMESTAMPS](plan/0020.TIMESTAMPS.md).

## Layer boundaries & implementation sequencing

The codebase has clean, upward-only dependencies, which dictates the order in
which to build new format/feature work:

- **`database/` and each `validations/<fmt>/` module are independent** — neither
  references the other, so they can be implemented in parallel.
- **Facade integration** (validator registration + `imager/` wiring + any
  `FileStorage` changes) depends on *both* the DB layer and the validator.
- **Core `Imager` logic** depends on facade integration (it needs the new DB
  methods and a registered validator).
- **Tests** come last (they depend on the core logic).

When adding the next sidecar type or feature, check for this same independence
before sequencing the work.

## Coroutine `whenAll` frame lifetime (`coro/`)

A 762 MB-per-run leak in `imagestore` (fixed 2026-04-14; plan
[0018.MEMORY](plan/0018.MEMORY.md)) traced to `coro/WhenAll.h`: storing sub-tasks
as `vector<Task<void>>` created a reference cycle
`WhenAllState → subTasks → coroutine frames → WhenAllState`, which pinned every
captured `Blob` shared_ptr alive so nothing was ever freed.

The fix uses a fire-and-forget `detail::SubTask` with
`final_suspend = std::suspend_never`, so each sub-task frame self-destroys on
completion and the state holds no reference to sub-task frames.

**Rule for future coro work:** never destroy a child coroutine frame from a
parent that the child reached via a direct `resume()` call (rather than symmetric
transfer through `final_suspend`) — the pool thread may still be unwinding through
that frame's call stack. Make frames self-destroying instead. The
`imagestore_memcheck` Valgrind test (see
[testing/CONVENTIONS.md](testing/CONVENTIONS.md)) guards against regressions here.

## `<cstdio>` before `<jpeglib.h>`

In `validations/jpeg`, `#include <cstdio>` must appear — outside any `extern "C"`
block — *before* `extern "C" { #include <jpeglib.h> }`. The system `jpeglib.h`
declares `jpeg_stdio_dest`/`jpeg_stdio_src` in terms of `FILE*` but does not pull
in `<stdio.h>` itself.

All image/codec dependencies are system packages resolved via `find_package` /
`pkg_check_modules`; no third-party library sources are bundled in the tree.
