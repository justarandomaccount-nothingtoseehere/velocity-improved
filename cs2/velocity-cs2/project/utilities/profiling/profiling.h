#ifndef VELOCITY_UTILITIES_PROFILING_PROFILING_H_
#define VELOCITY_UTILITIES_PROFILING_PROFILING_H_

#include <external/tracy/public/tracy/Tracy.hpp>

namespace profiling {

inline void Startup() {
#if defined(TRACY_ENABLE)
  if (!tracy::IsProfilerStarted()) {
    tracy::StartupProfiler();
    TracySetProgramName("Velocity");
  }
#endif
}

inline void Shutdown() {
#if defined(TRACY_ENABLE)
  if (tracy::IsProfilerStarted()) {
    tracy::ShutdownProfiler();
  }
#endif
}

inline void Frame() {
#if defined(TRACY_ENABLE)
  if (tracy::IsProfilerStarted()) {
    FrameMark;
  }
#endif
}

inline void ThreadName(const char* name) {
#if defined(TRACY_ENABLE)
  if (tracy::IsProfilerStarted()) {
    tracy::SetThreadName(name);
  }
#else
  static_cast<void>(name);
#endif
}

}  // namespace profiling

#endif
