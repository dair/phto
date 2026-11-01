#pragma once

#include <cstdlib>

#ifdef __GLIBC__
  #include <malloc.h>
#endif

namespace imager {

/// Cap glibc's per-thread malloc arenas at 2 (plan 0018).
///
/// libheif/libde265 decode with worker threads that allocate large plane/tile buffers; glibc gives each
/// thread its own arena and the arenas retain and fragment those buffers after they are freed. Measured on a
/// 3024x4032 HEIC: a single-threaded decode loop grew from 52 to 713 MiB after 40 decodes by default and stayed
/// flat (~110 MiB) with MALLOC_ARENA_MAX=2, at identical speed. imagestore --jobs 1 on 800 distinct HEICs went from
/// ~1.3 GiB to ~160 MiB.
///
/// Call once at the very start of main(), before any thread is created. An operator-set MALLOC_ARENA_MAX
/// environment variable always wins (nothing is changed then). No-op on non-glibc libcs.
inline void capMallocArenas() noexcept {
#ifdef __GLIBC__
  if (std::getenv("MALLOC_ARENA_MAX") == nullptr) {
    mallopt(M_ARENA_MAX, 2);
  }
#endif
}

} // namespace imager
