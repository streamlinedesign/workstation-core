#pragma once

#include "wsc/engine.hpp"

#include <array>

namespace wsc {

// The Stage 1 proof engine: polyphonic sine with a short linear attack and
// exponential release. Exists to prove the core, not to sound good.
class SineEngine final : public Engine {
public:
    static constexpr std::uint32_t kMaxVoices = 16;

    std::string_view name() const noexcept override { return "sine"; }
    void prepare(double sample_rate, std::uint32_t max_frames) override;
    void process(std::span<const MidiEvent> events, AudioBlock out) noexcept override;
    std::uint32_t active_voices() const noexcept override;

    void set_gain(float g) noexcept { gain_ = g; }

private:
    struct Voice {
        bool active = false;
        bool released = false;
        std::uint8_t note = 0;
        float phase = 0.f;
        float phase_inc = 0.f;
        float env = 0.f;
        float velocity = 0.f;
        std::uint64_t started = 0;  // for oldest-voice stealing
    };

    void note_on(std::uint8_t note, std::uint8_t velocity) noexcept;
    void note_off(std::uint8_t note) noexcept;

    std::array<Voice, kMaxVoices> voices_{};
    double sample_rate_ = 48000.0;
    float attack_inc_ = 0.f;
    float release_mul_ = 0.f;
    float gain_ = 0.2f;
    std::uint64_t counter_ = 0;
};

}  // namespace wsc
