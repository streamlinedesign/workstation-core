#pragma once

#include "wsc/midi.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace wsc {

// One block of interleaved-free audio: one float array per channel.
struct AudioBlock {
    float* const* channels;  // channels[c][frame]
    std::uint32_t num_channels;
    std::uint32_t num_frames;
};

// Every sound engine implements this. The audio thread calls process() once per
// period, so nothing in it may allocate, lock, block or touch the file system.
class Engine {
public:
    virtual ~Engine() = default;

    virtual std::string_view name() const noexcept = 0;

    // Called once before the first process(), off the audio thread. May allocate.
    virtual void prepare(double sample_rate, std::uint32_t max_frames) = 0;

    // Render one block, adding into `out`. `events` are the MIDI events whose
    // timestamps fall inside this block, already in time order.
    virtual void process(std::span<const MidiEvent> events, AudioBlock out) noexcept = 0;

    // Voices currently sounding, for the UI and the voice stealer.
    virtual std::uint32_t active_voices() const noexcept = 0;
};

}  // namespace wsc
