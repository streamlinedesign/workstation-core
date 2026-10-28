#pragma once

#include <cstdint>

namespace wsc {

// One timestamped MIDI event. Trivially copyable so it can travel through the ring buffer.
struct MidiEvent {
    enum class Type : std::uint8_t {
        NoteOff,
        NoteOn,
        PolyPressure,
        ControlChange,
        ProgramChange,
        ChannelPressure,
        PitchBend,
        Other,
    };

    std::int64_t time_ns = 0;  // CLOCK_MONOTONIC when the last byte arrived
    std::uint32_t frame = 0;   // offset inside the current block, filled in by the core
    Type type = Type::Other;
    std::uint8_t channel = 0;  // 0..15
    std::uint8_t data1 = 0;    // note / controller / program
    std::uint8_t data2 = 0;    // velocity / value
    std::int16_t bend = 0;     // -8192..8191 for PitchBend
};

// Running-status aware parser for a raw MIDI byte stream.
// Feed bytes one at a time; returns true when an event is complete.
class MidiParser {
public:
    bool feed(std::uint8_t byte, std::int64_t time_ns, MidiEvent& out) noexcept;

private:
    std::uint8_t status_ = 0;
    std::uint8_t data_[2] = {0, 0};
    std::uint8_t have_ = 0;
    std::uint8_t need_ = 0;
};

}  // namespace wsc
