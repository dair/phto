#pragma once

// Include this header in main.cpp to activate memory tracing.
// The feature is entirely compiled out when MEMORY_TRACE is not defined.

#ifdef MEMORY_TRACE

namespace imagestore {

// Call once at program start to log that tracing is active.
void memoryTraceInit();

} // namespace imagestore

#endif // MEMORY_TRACE
