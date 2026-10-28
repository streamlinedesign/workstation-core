// wsc-play: the Stage 1 proof. Opens one hw: device exclusively, one rawmidi port,
// and plays a sine per MIDI note. Prints xruns and timing once a second.
//
//   wsc-play --device hw:1,0 --midi hw:2,0,0 --rate 48000 --period 64 --periods 2
//   wsc-play --device hw:1,0 --tone 440       (no MIDI: a test tone)

#include "wsc/core.hpp"
#include "wsc/sine_engine.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_quit{false};
void on_signal(int) { g_quit.store(true); }

// Wraps SineEngine to hold one note forever: a test tone without a keyboard.
class ToneEngine final : public wsc::Engine {
public:
    explicit ToneEngine(std::uint8_t note) : note_(note) {}
    std::string_view name() const noexcept override { return "tone"; }
    void prepare(double sr, std::uint32_t frames) override { inner_.prepare(sr, frames); }
    void process(std::span<const wsc::MidiEvent> events, wsc::AudioBlock out) noexcept override {
        if (!started_) {
            wsc::MidiEvent on{};
            on.type = wsc::MidiEvent::Type::NoteOn;
            on.data1 = note_;
            on.data2 = 100;
            inner_.process(std::span<const wsc::MidiEvent>(&on, 1), out);
            started_ = true;
            return;
        }
        inner_.process(events, out);
    }
    std::uint32_t active_voices() const noexcept override { return inner_.active_voices(); }

private:
    wsc::SineEngine inner_;
    std::uint8_t note_;
    bool started_ = false;
};

void usage() {
    std::puts("wsc-play [--device hw:X,Y] [--midi hw:X,Y,Z] [--rate 48000] [--period 64] [--periods 2]\n"
              "         [--channels 2] [--priority 70] [--cpu N] [--gain 0.5] [--tone MIDI_NOTE]");
}

}  // namespace

int main(int argc, char** argv) {
    wsc::CoreConfig cfg;
    int tone = -1;
    for (int i = 1; i < argc; ++i) {
        auto arg = [&](const char* name) { return std::strcmp(argv[i], name) == 0 && i + 1 < argc; };
        if (arg("--device")) cfg.device.name = argv[++i];
        else if (arg("--midi")) cfg.midi_port = argv[++i];
        else if (arg("--rate")) cfg.device.sample_rate = static_cast<std::uint32_t>(std::atoi(argv[++i]));
        else if (arg("--period")) cfg.device.period_frames = static_cast<std::uint32_t>(std::atoi(argv[++i]));
        else if (arg("--periods")) cfg.device.periods = static_cast<std::uint32_t>(std::atoi(argv[++i]));
        else if (arg("--channels")) cfg.device.channels = static_cast<std::uint32_t>(std::atoi(argv[++i]));
        else if (arg("--priority")) cfg.rt_priority = std::atoi(argv[++i]);
        else if (arg("--cpu")) cfg.cpu = std::atoi(argv[++i]);
        else if (arg("--gain")) cfg.master_gain = static_cast<float>(std::atof(argv[++i]));
        else if (arg("--tone")) tone = std::atoi(argv[++i]);
        else { usage(); return 2; }
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    std::unique_ptr<wsc::Engine> engine;
    if (tone >= 0) engine = std::make_unique<ToneEngine>(static_cast<std::uint8_t>(tone));
    else engine = std::make_unique<wsc::SineEngine>();

    wsc::AudioCore core;
    if (auto e = core.start(cfg, std::move(engine)); !e.empty()) {
        std::fprintf(stderr, "error: %s\n", e.c_str());
        return 1;
    }
    const auto& info = core.device_info();
    const double period_ms = 1000.0 * info.period_frames / info.sample_rate;
    std::printf("device %s  %s  %u Hz  %u ch  period %u frames (%.2f ms) x %u  %s\n",
                cfg.device.name.c_str(), info.format.c_str(), info.sample_rate, info.channels,
                info.period_frames, period_ms, info.periods, info.mmap ? "mmap" : "writei");
    std::printf("nominal output latency %.2f ms\n", period_ms * info.periods);
    if (auto w = core.rt_warning(); !w.empty())
        std::printf("warning: %s (run as root, or raise rtprio/memlock in limits.conf)\n", w.c_str());
    std::puts("time      periods     xruns  voices  render_last  render_max  midi_events  midi_lag");

    const auto t0 = std::chrono::steady_clock::now();
    while (!g_quit.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto& s = core.stats();
        const auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count();
        std::printf("%6llds %10llu %9llu %7u %9.3f ms %8.3f ms %12llu %7.2f ms%s\n",
                    static_cast<long long>(secs),
                    static_cast<unsigned long long>(s.periods.load()),
                    static_cast<unsigned long long>(s.xruns.load()),
                    s.voices.load(),
                    s.last_render_ns.load() / 1e6, s.max_render_ns.load() / 1e6,
                    static_cast<unsigned long long>(s.midi_events.load()),
                    s.midi_to_block_ns.load() / 1e6,
                    s.midi_dropped.load() ? "  MIDI DROPPED" : "");
        std::fflush(stdout);
    }
    core.stop();
    const auto& s = core.stats();
    std::printf("\nstopped: %llu periods, %llu xruns, worst render %.3f ms of %.2f ms budget\n",
                static_cast<unsigned long long>(s.periods.load()),
                static_cast<unsigned long long>(s.xruns.load()),
                s.max_render_ns.load() / 1e6, period_ms);
    return s.xruns.load() == 0 ? 0 : 3;
}
