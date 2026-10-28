#pragma once

#include "wsc/midi.hpp"
#include "wsc/ring_buffer.hpp"

#include <atomic>
#include <string>
#include <thread>

#include <alsa/asoundlib.h>

namespace wsc {

// Reads one ALSA rawmidi port on its own thread, timestamps every event on arrival,
// and queues it for the audio thread. Reads raw USB/DIN bytes directly: no ALSA
// sequencer, no extra hop.
class MidiInput {
public:
    explicit MidiInput(std::size_t queue_capacity_pow2 = 1024) : queue_(queue_capacity_pow2) {}
    ~MidiInput();

    // `port` is a rawmidi name such as "hw:1,0,0". Empty string on success.
    std::string open(const std::string& port);
    void close() noexcept;

    // Audio thread side.
    std::optional<MidiEvent> pop() noexcept { return queue_.pop(); }

    std::uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    void run() noexcept;

    snd_rawmidi_t* in_ = nullptr;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<std::uint64_t> dropped_{0};
    RingBuffer<MidiEvent> queue_;
};

}  // namespace wsc
