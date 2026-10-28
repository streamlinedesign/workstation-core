#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>

namespace wsc {

// Single-producer, single-consumer lock-free ring buffer.
// Used to hand MIDI events from the input thread to the audio thread.
// Capacity must be a power of two. Never blocks, never allocates after construction.
template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(std::size_t capacity_pow2)
        : buf_(capacity_pow2), mask_(capacity_pow2 - 1) {
        static_assert(std::is_trivially_copyable_v<T>);
    }

    bool push(const T& v) noexcept {
        const auto w = write_.load(std::memory_order_relaxed);
        const auto r = read_.load(std::memory_order_acquire);
        if (w - r == buf_.size()) return false;  // full
        buf_[w & mask_] = v;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }

    std::optional<T> pop() noexcept {
        const auto r = read_.load(std::memory_order_relaxed);
        const auto w = write_.load(std::memory_order_acquire);
        if (r == w) return std::nullopt;  // empty
        T v = buf_[r & mask_];
        read_.store(r + 1, std::memory_order_release);
        return v;
    }

    std::size_t size() const noexcept {
        return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire);
    }

private:
    std::vector<T> buf_;
    const std::size_t mask_;
    alignas(64) std::atomic<std::size_t> write_{0};
    alignas(64) std::atomic<std::size_t> read_{0};
};

}  // namespace wsc
