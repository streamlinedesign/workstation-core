#include "check.hpp"
#include "wsc/midi.hpp"

#include <vector>

namespace {
std::vector<wsc::MidiEvent> parse(std::initializer_list<std::uint8_t> bytes) {
    wsc::MidiParser p;
    std::vector<wsc::MidiEvent> out;
    wsc::MidiEvent ev;
    std::int64_t t = 0;
    for (auto b : bytes) if (p.feed(b, ++t, ev)) out.push_back(ev);
    return out;
}
}  // namespace

TEST(midi_note_on_off) {
    auto ev = parse({0x90, 60, 100, 0x80, 60, 0});
    CHECK(ev.size() == 2);
    CHECK(ev[0].type == wsc::MidiEvent::Type::NoteOn);
    CHECK(ev[0].channel == 0 && ev[0].data1 == 60 && ev[0].data2 == 100);
    CHECK(ev[1].type == wsc::MidiEvent::Type::NoteOff);
}

TEST(midi_running_status) {
    auto ev = parse({0x91, 60, 100, 62, 90, 64, 80});
    CHECK(ev.size() == 3);
    for (auto& e : ev) CHECK(e.type == wsc::MidiEvent::Type::NoteOn && e.channel == 1);
    CHECK(ev[2].data1 == 64 && ev[2].data2 == 80);
}

TEST(midi_note_on_zero_velocity_is_off) {
    auto ev = parse({0x90, 60, 0});
    CHECK(ev.size() == 1 && ev[0].type == wsc::MidiEvent::Type::NoteOff);
}

TEST(midi_realtime_bytes_do_not_break_running_status) {
    auto ev = parse({0x90, 60, 0xF8, 100, 0xFE, 62, 100});
    CHECK(ev.size() == 2);
    CHECK(ev[1].data1 == 62);
}

TEST(midi_pitch_bend_and_one_byte_messages) {
    auto ev = parse({0xE0, 0x00, 0x40, 0xC3, 7, 0xD0, 55});
    CHECK(ev.size() == 3);
    CHECK(ev[0].type == wsc::MidiEvent::Type::PitchBend && ev[0].bend == 0);
    CHECK(ev[1].type == wsc::MidiEvent::Type::ProgramChange && ev[1].channel == 3 && ev[1].data1 == 7);
    CHECK(ev[2].type == wsc::MidiEvent::Type::ChannelPressure && ev[2].data1 == 55);
}

TEST(midi_timestamp_is_kept) {
    auto ev = parse({0x90, 60, 100});
    CHECK(ev[0].time_ns == 3);
}
