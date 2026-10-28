#pragma once

#include "wsc/alsa_device.hpp"
#include "wsc/engine.hpp"
#include "wsc/midi_input.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace wsc {

struct CoreConfig {
    DeviceConfig device;
    std::string midi_port;        // empty: no MIDI
    int rt_priority = 70;         // SCHED_FIFO priority of the audio thread
    int cpu = -1;                 // pin the audio thread to this CPU, -1 = don't
    float master_gain = 0.5f;
};

// Live counters, read by the UI or a status printer from any thread.
struct CoreStats {
    std::atomic<std::uint64_t> periods{0};
    std::atomic<std::uint64_t> xruns{0};
    std::atomic<std::uint64_t> midi_events{0};
    std::atomic<std::uint64_t> midi_dropped{0};
    std::atomic<std::uint32_t> voices{0};
    std::atomic<std::int64_t> max_render_ns{0};   // worst process() time since start
    std::atomic<std::int64_t> last_render_ns{0};
    std::atomic<std::int64_t> midi_to_block_ns{0}; // last event: arrival to block start
};

// The audio core: owns the device, the MIDI input and (for Stage 1) one engine,
// and runs the real-time loop on its own thread.
class AudioCore {
public:
    AudioCore() = default;
    ~AudioCore();

    // Opens the device and MIDI, prepares the engine. Empty string on success.
    std::string start(const CoreConfig& cfg, std::unique_ptr<Engine> engine);
    void stop() noexcept;

    const CoreStats& stats() const noexcept { return stats_; }
    const DeviceInfo& device_info() const noexcept { return device_.info(); }
    std::string rt_warning() const { return rt_warning_; }

private:
    void audio_thread() noexcept;

    CoreConfig cfg_;
    AlsaDevice device_;
    MidiInput midi_;
    std::unique_ptr<Engine> engine_;
    std::vector<std::vector<float>> buffers_;
    std::vector<float*> channel_ptrs_;
    std::vector<MidiEvent> events_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::string rt_warning_;
    CoreStats stats_;
};

}  // namespace wsc
