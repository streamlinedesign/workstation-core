#include "wsc/midi_input.hpp"
#include "wsc/rt.hpp"

#include <poll.h>

#include <vector>

namespace wsc {

MidiInput::~MidiInput() { close(); }

std::string MidiInput::open(const std::string& port) {
    close();
    const int rc = snd_rawmidi_open(&in_, nullptr, port.c_str(), SND_RAWMIDI_NONBLOCK);
    if (rc < 0) return std::string("rawmidi open ") + port + ": " + snd_strerror(rc);
    stop_.store(false);
    thread_ = std::thread([this] { run(); });
    return {};
}

void MidiInput::close() noexcept {
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
    if (in_) {
        snd_rawmidi_close(in_);
        in_ = nullptr;
    }
}

void MidiInput::run() noexcept {
    // A notch below the audio thread: MIDI must be prompt but never delay audio.
    rt::set_realtime(60);

    const int nfds = snd_rawmidi_poll_descriptors_count(in_);
    std::vector<pollfd> fds(static_cast<std::size_t>(nfds));
    snd_rawmidi_poll_descriptors(in_, fds.data(), static_cast<unsigned>(nfds));

    MidiParser parser;
    std::uint8_t buf[256];
    while (!stop_.load(std::memory_order_relaxed)) {
        if (poll(fds.data(), static_cast<nfds_t>(nfds), 50) <= 0) continue;
        const std::int64_t t = rt::now_ns();
        const ssize_t n = snd_rawmidi_read(in_, buf, sizeof buf);
        if (n <= 0) continue;
        MidiEvent ev;
        for (ssize_t i = 0; i < n; ++i) {
            if (parser.feed(buf[static_cast<std::size_t>(i)], t, ev) && !queue_.push(ev))
                dropped_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

}  // namespace wsc
