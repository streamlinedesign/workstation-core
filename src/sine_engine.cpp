#include "wsc/sine_engine.hpp"

#include <cmath>
#include <numbers>

namespace wsc {

void SineEngine::prepare(double sample_rate, std::uint32_t /*max_frames*/) {
    sample_rate_ = sample_rate;
    attack_inc_ = static_cast<float>(1.0 / (0.005 * sample_rate));           // 5 ms attack
    release_mul_ = static_cast<float>(std::exp(-1.0 / (0.15 * sample_rate)));  // 150 ms release
    for (auto& v : voices_) v = Voice{};
}

void SineEngine::note_on(std::uint8_t note, std::uint8_t velocity) noexcept {
    Voice* target = nullptr;
    for (auto& v : voices_) {
        if (!v.active) { target = &v; break; }
    }
    if (!target) {  // steal the oldest voice
        target = &voices_[0];
        for (auto& v : voices_) if (v.started < target->started) target = &v;
    }
    const double hz = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    target->active = true;
    target->released = false;
    target->note = note;
    target->phase = 0.f;
    target->phase_inc = static_cast<float>(2.0 * std::numbers::pi * hz / sample_rate_);
    target->env = 0.f;
    target->velocity = static_cast<float>(velocity) / 127.f;
    target->started = ++counter_;
}

void SineEngine::note_off(std::uint8_t note) noexcept {
    for (auto& v : voices_) {
        if (v.active && v.note == note && !v.released) v.released = true;
    }
}

void SineEngine::process(std::span<const MidiEvent> events, AudioBlock out) noexcept {
    // Sample-accurate event handling: render up to each event, apply it, continue.
    std::uint32_t frame = 0;
    std::size_t ev = 0;
    constexpr float kTwoPi = 2.f * std::numbers::pi_v<float>;

    auto render = [&](std::uint32_t from, std::uint32_t to) {
        for (auto& v : voices_) {
            if (!v.active) continue;
            for (std::uint32_t i = from; i < to; ++i) {
                if (v.released) v.env *= release_mul_;
                else if (v.env < 1.f) v.env = std::min(1.f, v.env + attack_inc_);
                const float s = std::sin(v.phase) * v.env * v.velocity * gain_;
                v.phase += v.phase_inc;
                if (v.phase >= kTwoPi) v.phase -= kTwoPi;
                for (std::uint32_t c = 0; c < out.num_channels; ++c) out.channels[c][i] += s;
            }
            if (v.released && v.env < 1e-3f) v.active = false;  // -60 dB: free the voice
        }
    };

    for (; ev < events.size(); ++ev) {
        const auto& e = events[ev];
        const std::uint32_t at = std::min(e.frame, out.num_frames);
        if (at > frame) { render(frame, at); frame = at; }
        if (e.type == MidiEvent::Type::NoteOn) note_on(e.data1, e.data2);
        else if (e.type == MidiEvent::Type::NoteOff) note_off(e.data1);
    }
    render(frame, out.num_frames);
}

std::uint32_t SineEngine::active_voices() const noexcept {
    std::uint32_t n = 0;
    for (const auto& v : voices_) n += v.active ? 1u : 0u;
    return n;
}

}  // namespace wsc
