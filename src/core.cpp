#include "wsc/core.hpp"
#include "wsc/rt.hpp"

#include <algorithm>
#include <cstring>

namespace wsc {

AudioCore::~AudioCore() { stop(); }

std::string AudioCore::start(const CoreConfig& cfg, std::unique_ptr<Engine> engine) {
    stop();
    cfg_ = cfg;
    engine_ = std::move(engine);

    if (auto e = device_.open(cfg.device); !e.empty()) return "audio device: " + e;
    const auto& info = device_.info();

    if (!cfg.midi_port.empty()) {
        if (auto e = midi_.open(cfg.midi_port); !e.empty()) return "midi: " + e;
    }

    // Everything the audio thread touches is allocated here, once.
    buffers_.assign(info.channels, std::vector<float>(info.period_frames, 0.f));
    channel_ptrs_.clear();
    for (auto& b : buffers_) channel_ptrs_.push_back(b.data());
    events_.reserve(1024);
    engine_->prepare(info.sample_rate, info.period_frames);

    if (auto e = rt::lock_memory(); !e.empty()) rt_warning_ = "mlockall: " + e;

    running_.store(true);
    thread_ = std::thread([this] { audio_thread(); });
    return {};
}

void AudioCore::stop() noexcept {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
    midi_.close();
    device_.close();
    engine_.reset();
}

void AudioCore::audio_thread() noexcept {
    if (auto e = rt::set_realtime(cfg_.rt_priority); !e.empty())
        rt_warning_ += (rt_warning_.empty() ? "" : "; ") + ("SCHED_FIFO: " + e);
    if (cfg_.cpu >= 0) rt::pin_to_cpu(cfg_.cpu);
    rt::disable_denormals();

    const auto& info = device_.info();
    const std::uint32_t frames = info.period_frames;
    const double ns_per_frame = 1e9 / info.sample_rate;

    // Prime the ring with one silent period so the stream starts cleanly.
    for (auto& b : buffers_) std::fill(b.begin(), b.end(), 0.f);
    device_.write_period(channel_ptrs_.data());
    device_.start();

    while (running_.load(std::memory_order_relaxed)) {
        const std::int64_t block_start = rt::now_ns();

        // Collect every MIDI event that has arrived. Events are stamped on arrival;
        // their offset inside this block is their lateness relative to the block start,
        // clamped to the block, so timing between notes is preserved.
        events_.clear();
        while (auto ev = midi_.pop()) {
            auto e = *ev;
            const std::int64_t late = block_start - e.time_ns;
            stats_.midi_to_block_ns.store(late, std::memory_order_relaxed);
            const double off = static_cast<double>(frames) - static_cast<double>(late) / ns_per_frame;
            e.frame = static_cast<std::uint32_t>(std::clamp(off, 0.0, static_cast<double>(frames - 1)));
            events_.push_back(e);
            if (events_.size() == events_.capacity()) break;  // never grow on this thread
        }
        std::stable_sort(events_.begin(), events_.end(),
                         [](const MidiEvent& a, const MidiEvent& b) { return a.frame < b.frame; });
        stats_.midi_events.fetch_add(events_.size(), std::memory_order_relaxed);
        stats_.midi_dropped.store(midi_.dropped(), std::memory_order_relaxed);

        for (auto& b : buffers_) std::fill(b.begin(), b.end(), 0.f);
        engine_->process(events_, AudioBlock{channel_ptrs_.data(), info.channels, frames});
        for (auto& b : buffers_)
            for (auto& s : b) s *= cfg_.master_gain;

        const std::int64_t render_ns = rt::now_ns() - block_start;
        stats_.last_render_ns.store(render_ns, std::memory_order_relaxed);
        if (render_ns > stats_.max_render_ns.load(std::memory_order_relaxed))
            stats_.max_render_ns.store(render_ns, std::memory_order_relaxed);
        stats_.voices.store(engine_->active_voices(), std::memory_order_relaxed);

        const int xr = device_.write_period(channel_ptrs_.data());
        if (xr < 0) break;  // unrecoverable: the device went away
        if (xr > 0) stats_.xruns.fetch_add(static_cast<std::uint64_t>(xr), std::memory_order_relaxed);
        stats_.periods.fetch_add(1, std::memory_order_relaxed);
    }
    running_.store(false);
}

}  // namespace wsc
