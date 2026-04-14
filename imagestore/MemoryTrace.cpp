#ifdef MEMORY_TRACE

  #include "MemoryTrace.h"

  #include <malloc.h>
  #include <unistd.h>

  #include <atomic>
  #include <cstddef>
  #include <cstdlib>
  #include <new>

namespace {

std::atomic<std::size_t> g_totalAllocated{0};
std::atomic<std::size_t> g_currentAllocated{0};

// Per-thread reentrancy guard: prevents recursive entry during TLS slot
// initialisation or any other allocation triggered inside this function.
thread_local bool g_inTrace{false};

// Write a formatted trace line to stderr using write(2) — allocation-free and
// async-signal-safe.
// Format: "<prefix><n> bytes, <total> bytes total allocated\n"
void writeLine(const char* prefix, std::size_t n, std::size_t total) noexcept {
  char buf[256];
  int len = 0;

  for (const char* p = prefix; *p; ++p) {
    buf[len++] = *p;
  }

  auto appendUInt = [&](std::size_t v) {
    if (v == 0) {
      buf[len++] = '0';
      return;
    }
    char tmp[32];
    int tlen = 0;
    while (v > 0) {
      tmp[tlen++] = static_cast<char>('0' + (v % 10));
      v /= 10;
    }
    for (int i = tlen - 1; i >= 0; --i) {
      buf[len++] = tmp[i];
    }
  };

  appendUInt(n);
  const char* mid = " bytes, ";
  for (const char* p = mid; *p; ++p) {
    buf[len++] = *p;
  }
  appendUInt(total);
  const char* suffix = " bytes total allocated\n";
  for (const char* p = suffix; *p; ++p) {
    buf[len++] = *p;
  }

  write(STDERR_FILENO, buf, static_cast<std::size_t>(len));
}

// Allocate without any size header — we use malloc_usable_size() at free time.
void* tracedAlloc(std::size_t size) {
  void* ptr = std::malloc(size);
  if (!ptr) {
    throw std::bad_alloc{};
  }

  g_currentAllocated.fetch_add(size, std::memory_order_relaxed);
  std::size_t total = g_totalAllocated.fetch_add(size, std::memory_order_relaxed) + size;

  if (!g_inTrace) {
    g_inTrace = true;
    writeLine("Allocated ", size, total);
    g_inTrace = false;
  }

  return ptr;
}

void tracedFree(void* ptr) noexcept {
  if (!ptr) {
    return;
  }

  // Use malloc_usable_size to recover the block size without a hidden header.
  // This is a glibc/Linux extension available on the target platform.
  std::size_t blockSize = malloc_usable_size(ptr);

  g_currentAllocated.fetch_sub(blockSize, std::memory_order_relaxed);
  // Decrement g_totalAllocated so the logged total reflects live bytes, not
  // cumulative bytes ever allocated (which would always grow and obscure leaks).
  std::size_t total = g_totalAllocated.fetch_sub(blockSize, std::memory_order_relaxed) - blockSize;

  if (!g_inTrace) {
    g_inTrace = true;
    writeLine("Deallocated ", blockSize, total);
    g_inTrace = false;
  }

  std::free(ptr);
}

} // namespace

// ---------------------------------------------------------------------------
// operator new overloads
// ---------------------------------------------------------------------------

void* operator new(std::size_t size) {
  return tracedAlloc(size);
}

void* operator new[](std::size_t size) {
  return tracedAlloc(size);
}

// ---------------------------------------------------------------------------
// operator delete overloads (sized and non-sized)
// ---------------------------------------------------------------------------

void operator delete(void* ptr) noexcept {
  tracedFree(ptr);
}

void operator delete[](void* ptr) noexcept {
  tracedFree(ptr);
}

// Sized delete — we ignore the size parameter and use malloc_usable_size
// for consistency with the non-sized path.
void operator delete(void* ptr, std::size_t /*size*/) noexcept {
  tracedFree(ptr);
}

void operator delete[](void* ptr, std::size_t /*size*/) noexcept {
  tracedFree(ptr);
}

// ---------------------------------------------------------------------------
// imagestore::memoryTraceInit
// ---------------------------------------------------------------------------

namespace imagestore {

void memoryTraceInit() {
  write(STDERR_FILENO, "Memory tracing enabled\n", 23);
}

} // namespace imagestore

#endif // MEMORY_TRACE
