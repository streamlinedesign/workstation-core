#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <alsa/asoundlib.h>

namespace wsc {

struct DeviceConfig {
    std::string name = "hw:0,0";  // always a raw hw: device, never "default"
    std::uint32_t sample_rate = 48000;
    std::uint32_t period_frames = 64;
    std::uint32_t periods = 2;
    std::uint32_t channels = 2;
};

// What the hardware actually gave us after negotiation.
struct DeviceInfo {
    std::uint32_t sample_rate = 0;
    std::uint32_t period_frames = 0;
    std::uint32_t periods = 0;
    std::uint32_t channels = 0;
    std::string format;  // e.g. "S32_LE"
    bool mmap = false;
};

// Exclusive playback on one ALSA hw: device. No alsa-lib plugins, no resampling,
// no dmix: the samples we write are the samples the DAC converts.
// Transfer is mmap when the driver offers it, else plain write.
class AlsaDevice {
public:
    AlsaDevice() = default;
    ~AlsaDevice();
    AlsaDevice(const AlsaDevice&) = delete;
    AlsaDevice& operator=(const AlsaDevice&) = delete;

    // Opens and configures. Returns an empty string on success, else the reason.
    std::string open(const DeviceConfig& cfg);
    void close() noexcept;

    const DeviceInfo& info() const noexcept { return info_; }

    // Blocks until the device can take one period, then converts `planar`
    // (one float array per channel, info().period_frames long) and writes it.
    // Returns the number of xruns recovered during this call (normally 0),
    // or -1 on an unrecoverable error.
    int write_period(const float* const* planar) noexcept;

    // Start the stream after the first period has been written.
    std::string start();

private:
    int recover(int err) noexcept;
    void convert(const float* const* planar, void* dst, std::uint32_t frames) noexcept;

    snd_pcm_t* pcm_ = nullptr;
    DeviceInfo info_{};
    snd_pcm_format_t fmt_ = SND_PCM_FORMAT_UNKNOWN;
    std::vector<std::uint8_t> scratch_;  // interleaved period for the non-mmap path
    std::uint32_t bytes_per_frame_ = 0;
};

// Lists playback PCMs and raw MIDI ports as hw: names.
std::vector<std::string> list_playback_devices();
std::vector<std::string> list_rawmidi_inputs();

}  // namespace wsc
