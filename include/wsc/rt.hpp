#pragma once

#include <cstdint>
#include <string>

namespace wsc::rt {

// Lock all current and future pages in RAM. Call once, early, from the main thread.
// Returns an empty string on success, else the reason.
std::string lock_memory();

// Make the calling thread SCHED_FIFO at `priority` (1..99). Needs CAP_SYS_NICE or
// an rtprio limit in /etc/security/limits.d. Returns an empty string on success.
std::string set_realtime(int priority);

// Pin the calling thread to one CPU. Returns an empty string on success.
std::string pin_to_cpu(int cpu);

// Flush denormals to zero on this thread (MXCSR FTZ/DAZ on x86, FPCR FZ on arm64).
void disable_denormals() noexcept;

std::int64_t now_ns() noexcept;

}  // namespace wsc::rt
