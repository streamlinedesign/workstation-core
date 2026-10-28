#include "wsc/rt.hpp"

#include <cerrno>
#include <cstring>
#include <ctime>

#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>

#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

namespace wsc::rt {

std::string lock_memory() {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) return std::strerror(errno);
    return {};
}

std::string set_realtime(int priority) {
    sched_param p{};
    p.sched_priority = priority;
    const int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &p);
    if (rc != 0) return std::strerror(rc);
    return {};
}

std::string pin_to_cpu(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    const int rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    if (rc != 0) return std::strerror(rc);
    return {};
}

void disable_denormals() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#elif defined(__aarch64__)
    std::uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1u << 24);  // FZ
    asm volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
}

std::int64_t now_ns() noexcept {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
}

}  // namespace wsc::rt
