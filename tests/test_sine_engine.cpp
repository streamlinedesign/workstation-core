#include "check.hpp"
#include "wsc/sine_engine.hpp"

#include <cmath>
#include <vector>

namespace {
struct Block {
    std::vector<float> l, r;
    std::vector<float*> ptrs;
    explicit Block(std::uint32_t n) : l(n, 0.f), r(n, 0.f), ptrs{l.data(), r.data()} {}
    wsc::AudioBlock view() { return {ptrs.data(), 2, static_cast<std::uint32_t>(l.size())}; }
    float peak() const {
        float p = 0.f;
        for (float s : l) p = std::max(p, std::fabs(s));
        return p;
    }
};
}  // namespace

TEST(sine_silent_without_notes) {
    wsc::SineEngine e;
    e.prepare(48000, 64);
    Block b(64);
    e.process({}, b.view());
    CHECK(b.peak() == 0.f);
    CHECK(e.active_voices() == 0);
}

TEST(sine_plays_and_releases) {
    wsc::SineEngine e;
    e.prepare(48000, 64);
    wsc::MidiEvent on{};
    on.type = wsc::MidiEvent::Type::NoteOn;
    on.data1 = 69;
    on.data2 = 127;
    Block b(64);
    e.process(std::span<const wsc::MidiEvent>(&on, 1), b.view());
    float peak = 0.f;
    for (int i = 0; i < 10; ++i) { Block c(64); e.process({}, c.view()); peak = c.peak(); }
    CHECK(peak > 0.1f);
    CHECK(e.active_voices() == 1);

    wsc::MidiEvent off = on;
    off.type = wsc::MidiEvent::Type::NoteOff;
    Block c(64);
    e.process(std::span<const wsc::MidiEvent>(&off, 1), c.view());
    for (int i = 0; i < 2 * 48000 / 64; ++i) { Block d(64); e.process({}, d.view()); }  // two seconds
    CHECK(e.active_voices() == 0);
}

TEST(sine_frequency_is_right) {
    // A4 at 48 kHz: 440 cycles per second. Count zero crossings over one second.
    wsc::SineEngine e;
    e.prepare(48000, 480);
    wsc::MidiEvent on{};
    on.type = wsc::MidiEvent::Type::NoteOn;
    on.data1 = 69;
    on.data2 = 127;
    Block warm(480);
    e.process(std::span<const wsc::MidiEvent>(&on, 1), warm.view());
    int crossings = 0;
    float prev = 0.f;
    for (int blk = 0; blk < 100; ++blk) {
        Block b(480);
        e.process({}, b.view());
        for (float s : b.l) {
            if ((prev < 0.f && s >= 0.f)) ++crossings;
            prev = s;
        }
    }
    CHECK(crossings >= 439 && crossings <= 441);
}

TEST(sine_event_offset_delays_onset) {
    wsc::SineEngine e;
    e.prepare(48000, 64);
    wsc::MidiEvent on{};
    on.type = wsc::MidiEvent::Type::NoteOn;
    on.data1 = 60;
    on.data2 = 127;
    on.frame = 32;
    Block b(64);
    e.process(std::span<const wsc::MidiEvent>(&on, 1), b.view());
    bool silent_before = true;
    for (int i = 0; i < 32; ++i) silent_before = silent_before && b.l[static_cast<std::size_t>(i)] == 0.f;
    CHECK(silent_before);
}
