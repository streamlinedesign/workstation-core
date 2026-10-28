#include "wsc/midi.hpp"

namespace wsc {

namespace {

std::uint8_t data_bytes_for(std::uint8_t status) noexcept {
    switch (status & 0xF0) {
        case 0xC0:  // program change
        case 0xD0:  // channel pressure
            return 1;
        case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0:
            return 2;
        default:
            return 0;  // system messages are ignored for now
    }
}

}  // namespace

bool MidiParser::feed(std::uint8_t byte, std::int64_t time_ns, MidiEvent& out) noexcept {
    if (byte >= 0xF8) return false;  // realtime bytes never disturb running status
    if (byte & 0x80) {
        status_ = byte;
        have_ = 0;
        need_ = data_bytes_for(byte);
        if (need_ == 0) status_ = 0;  // system common: drop and clear running status
        return false;
    }
    if (status_ == 0) return false;  // data byte with no status: noise

    data_[have_++] = byte;
    if (have_ < need_) return false;
    have_ = 0;  // running status: next data bytes reuse status_

    out.time_ns = time_ns;
    out.channel = static_cast<std::uint8_t>(status_ & 0x0F);
    out.data1 = data_[0];
    out.data2 = data_[1];
    out.bend = 0;
    switch (status_ & 0xF0) {
        case 0x80: out.type = MidiEvent::Type::NoteOff; break;
        case 0x90:
            // Note on with velocity 0 is a note off by convention.
            out.type = data_[1] == 0 ? MidiEvent::Type::NoteOff : MidiEvent::Type::NoteOn;
            break;
        case 0xA0: out.type = MidiEvent::Type::PolyPressure; break;
        case 0xB0: out.type = MidiEvent::Type::ControlChange; break;
        case 0xC0: out.type = MidiEvent::Type::ProgramChange; break;
        case 0xD0: out.type = MidiEvent::Type::ChannelPressure; break;
        case 0xE0:
            out.type = MidiEvent::Type::PitchBend;
            out.bend = static_cast<std::int16_t>(((data_[1] << 7) | data_[0]) - 8192);
            break;
        default: out.type = MidiEvent::Type::Other; break;
    }
    return true;
}

}  // namespace wsc
