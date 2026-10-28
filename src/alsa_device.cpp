#include "wsc/alsa_device.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace wsc {

namespace {

std::string err(const char* what, int rc) {
    return std::string(what) + ": " + snd_strerror(rc);
}

// Preferred sample formats, best first. Most pro and USB interfaces offer S32_LE
// (24 bits in the top of 32) or S24_3LE; consumer codecs offer S16_LE.
constexpr snd_pcm_format_t kFormats[] = {
    SND_PCM_FORMAT_S32_LE, SND_PCM_FORMAT_S24_LE, SND_PCM_FORMAT_S24_3LE, SND_PCM_FORMAT_S16_LE,
};

}  // namespace

AlsaDevice::~AlsaDevice() { close(); }

void AlsaDevice::close() noexcept {
    if (pcm_) {
        snd_pcm_drop(pcm_);
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
    }
}

std::string AlsaDevice::open(const DeviceConfig& cfg) {
    close();
    if (cfg.name.rfind("hw:", 0) != 0) return "device must be a raw hw: name, got " + cfg.name;

    // SND_PCM_NO_AUTO_* makes alsa-lib refuse to silently insert plugins.
    int rc = snd_pcm_open(&pcm_, cfg.name.c_str(), SND_PCM_STREAM_PLAYBACK,
                          SND_PCM_NO_AUTO_RESAMPLE | SND_PCM_NO_AUTO_CHANNELS | SND_PCM_NO_AUTO_FORMAT);
    if (rc < 0) return err("open", rc);

    snd_pcm_hw_params_t* hw;
    snd_pcm_hw_params_alloca(&hw);
    if ((rc = snd_pcm_hw_params_any(pcm_, hw)) < 0) return err("hw_params_any", rc);

    info_.mmap = snd_pcm_hw_params_set_access(pcm_, hw, SND_PCM_ACCESS_MMAP_INTERLEAVED) == 0;
    if (!info_.mmap && (rc = snd_pcm_hw_params_set_access(pcm_, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
        return err("set_access", rc);

    fmt_ = SND_PCM_FORMAT_UNKNOWN;
    for (auto f : kFormats) {
        if (snd_pcm_hw_params_set_format(pcm_, hw, f) == 0) { fmt_ = f; break; }
    }
    if (fmt_ == SND_PCM_FORMAT_UNKNOWN) return "no supported sample format (want S32/S24/S16)";
    info_.format = snd_pcm_format_name(fmt_);

    if ((rc = snd_pcm_hw_params_set_channels(pcm_, hw, cfg.channels)) < 0) return err("set_channels", rc);
    info_.channels = cfg.channels;

    // Exact rate only: a resampler is precisely what we refuse to have in the path.
    if ((rc = snd_pcm_hw_params_set_rate(pcm_, hw, cfg.sample_rate, 0)) < 0) return err("set_rate (exact)", rc);
    info_.sample_rate = cfg.sample_rate;

    snd_pcm_uframes_t period = cfg.period_frames;
    if ((rc = snd_pcm_hw_params_set_period_size_near(pcm_, hw, &period, nullptr)) < 0) return err("set_period_size", rc);
    unsigned periods = cfg.periods;
    if ((rc = snd_pcm_hw_params_set_periods_near(pcm_, hw, &periods, nullptr)) < 0) return err("set_periods", rc);
    if ((rc = snd_pcm_hw_params(pcm_, hw)) < 0) return err("hw_params", rc);

    snd_pcm_hw_params_get_period_size(hw, &period, nullptr);
    snd_pcm_hw_params_get_periods(hw, &periods, nullptr);
    info_.period_frames = static_cast<std::uint32_t>(period);
    info_.periods = periods;

    snd_pcm_sw_params_t* sw;
    snd_pcm_sw_params_alloca(&sw);
    snd_pcm_sw_params_current(pcm_, sw);
    // Start when one period is queued; wake when one period of space is free.
    snd_pcm_sw_params_set_start_threshold(pcm_, sw, period);
    snd_pcm_sw_params_set_avail_min(pcm_, sw, period);
    if ((rc = snd_pcm_sw_params(pcm_, sw)) < 0) return err("sw_params", rc);

    bytes_per_frame_ = static_cast<std::uint32_t>(snd_pcm_format_physical_width(fmt_) / 8) * info_.channels;
    scratch_.assign(static_cast<std::size_t>(bytes_per_frame_) * info_.period_frames, 0);
    return {};
}

std::string AlsaDevice::start() {
    const int rc = snd_pcm_state(pcm_) == SND_PCM_STATE_PREPARED ? snd_pcm_start(pcm_) : 0;
    return rc < 0 ? err("start", rc) : std::string{};
}

int AlsaDevice::recover(int e) noexcept {
    // -EPIPE is an underrun, -ESTRPIPE a suspend. Both are recoverable; count them.
    if (snd_pcm_recover(pcm_, e, 1) < 0) return -1;
    return 1;
}

void AlsaDevice::convert(const float* const* planar, void* dst, std::uint32_t frames) noexcept {
    const std::uint32_t ch = info_.channels;
    auto clamp = [](float x) { return std::clamp(x, -1.f, 1.f); };
    switch (fmt_) {
        case SND_PCM_FORMAT_S32_LE: {
            auto* d = static_cast<std::int32_t*>(dst);
            for (std::uint32_t i = 0; i < frames; ++i)
                for (std::uint32_t c = 0; c < ch; ++c)
                    d[i * ch + c] = static_cast<std::int32_t>(std::lrint(clamp(planar[c][i]) * 2147483392.f));
            break;
        }
        case SND_PCM_FORMAT_S24_LE: {  // 24 bits in the low part of a 32-bit word
            auto* d = static_cast<std::int32_t*>(dst);
            for (std::uint32_t i = 0; i < frames; ++i)
                for (std::uint32_t c = 0; c < ch; ++c)
                    d[i * ch + c] = static_cast<std::int32_t>(std::lrint(clamp(planar[c][i]) * 8388607.f));
            break;
        }
        case SND_PCM_FORMAT_S24_3LE: {
            auto* d = static_cast<std::uint8_t*>(dst);
            for (std::uint32_t i = 0; i < frames; ++i)
                for (std::uint32_t c = 0; c < ch; ++c) {
                    const auto v = static_cast<std::int32_t>(std::lrint(clamp(planar[c][i]) * 8388607.f));
                    std::uint8_t* p = d + (i * ch + c) * 3;
                    p[0] = static_cast<std::uint8_t>(v);
                    p[1] = static_cast<std::uint8_t>(v >> 8);
                    p[2] = static_cast<std::uint8_t>(v >> 16);
                }
            break;
        }
        default: {  // S16_LE
            auto* d = static_cast<std::int16_t*>(dst);
            for (std::uint32_t i = 0; i < frames; ++i)
                for (std::uint32_t c = 0; c < ch; ++c)
                    d[i * ch + c] = static_cast<std::int16_t>(std::lrint(clamp(planar[c][i]) * 32767.f));
            break;
        }
    }
}

int AlsaDevice::write_period(const float* const* planar) noexcept {
    int xruns = 0;
    const snd_pcm_uframes_t period = info_.period_frames;

    for (int attempt = 0; attempt < 4; ++attempt) {
        int rc = snd_pcm_wait(pcm_, 1000);
        if (rc < 0) {
            if ((rc = recover(rc)) < 0) return -1;
            xruns += rc;
            continue;
        }

        if (info_.mmap) {
            // Convert once, then hand the driver the period in as many mmap chunks
            // as it asks for (normally exactly one).
            convert(planar, scratch_.data(), info_.period_frames);
            snd_pcm_uframes_t done = 0;
            bool failed = false;
            while (done < period) {
                const snd_pcm_sframes_t avail = snd_pcm_avail_update(pcm_);
                if (avail < 0) { rc = static_cast<int>(avail); failed = true; break; }
                const snd_pcm_channel_area_t* areas;
                snd_pcm_uframes_t offset, frames = period - done;
                if ((rc = snd_pcm_mmap_begin(pcm_, &areas, &offset, &frames)) < 0) { failed = true; break; }
                if (frames == 0) {
                    if (snd_pcm_wait(pcm_, 1000) < 0) { rc = -EPIPE; failed = true; break; }
                    continue;
                }
                // Interleaved mmap: every area shares one base; the frame at `offset`
                // starts at addr + first/8 + offset * bytes_per_frame.
                auto* dst = static_cast<std::uint8_t*>(areas[0].addr) + (areas[0].first / 8) + offset * bytes_per_frame_;
                std::memcpy(dst, scratch_.data() + done * bytes_per_frame_, frames * bytes_per_frame_);
                const snd_pcm_sframes_t committed = snd_pcm_mmap_commit(pcm_, offset, frames);
                if (committed < 0 || static_cast<snd_pcm_uframes_t>(committed) != frames) {
                    rc = committed < 0 ? static_cast<int>(committed) : -EPIPE;
                    failed = true;
                    break;
                }
                done += frames;
            }
            if (failed) {
                if ((rc = recover(rc)) < 0) return -1;
                xruns += rc;
                continue;
            }
            if (snd_pcm_state(pcm_) == SND_PCM_STATE_PREPARED) snd_pcm_start(pcm_);
            return xruns;
        }

        convert(planar, scratch_.data(), static_cast<std::uint32_t>(period));
        const snd_pcm_sframes_t n = snd_pcm_writei(pcm_, scratch_.data(), period);
        if (n < 0) {
            if ((rc = recover(static_cast<int>(n))) < 0) return -1;
            xruns += rc;
            continue;
        }
        return xruns;
    }
    return -1;
}

std::vector<std::string> list_playback_devices() {
    std::vector<std::string> out;
    int card = -1;
    while (snd_card_next(&card) == 0 && card >= 0) {
        snd_ctl_t* ctl;
        const std::string ctlname = "hw:" + std::to_string(card);
        if (snd_ctl_open(&ctl, ctlname.c_str(), 0) < 0) continue;
        snd_pcm_info_t* info;
        snd_pcm_info_alloca(&info);
        int dev = -1;
        while (snd_ctl_pcm_next_device(ctl, &dev) == 0 && dev >= 0) {
            snd_pcm_info_set_device(info, static_cast<unsigned>(dev));
            snd_pcm_info_set_subdevice(info, 0);
            snd_pcm_info_set_stream(info, SND_PCM_STREAM_PLAYBACK);
            if (snd_ctl_pcm_info(ctl, info) < 0) continue;
            out.push_back("hw:" + std::to_string(card) + "," + std::to_string(dev) + "  " + snd_pcm_info_get_name(info));
        }
        snd_ctl_close(ctl);
    }
    return out;
}

std::vector<std::string> list_rawmidi_inputs() {
    std::vector<std::string> out;
    int card = -1;
    while (snd_card_next(&card) == 0 && card >= 0) {
        snd_ctl_t* ctl;
        const std::string ctlname = "hw:" + std::to_string(card);
        if (snd_ctl_open(&ctl, ctlname.c_str(), 0) < 0) continue;
        snd_rawmidi_info_t* info;
        snd_rawmidi_info_alloca(&info);
        int dev = -1;
        while (snd_ctl_rawmidi_next_device(ctl, &dev) == 0 && dev >= 0) {
            snd_rawmidi_info_set_device(info, static_cast<unsigned>(dev));
            snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);
            snd_rawmidi_info_set_subdevice(info, 0);
            if (snd_ctl_rawmidi_info(ctl, info) < 0) continue;
            const unsigned subs = snd_rawmidi_info_get_subdevices_count(info);
            for (unsigned s = 0; s < subs; ++s) {
                snd_rawmidi_info_set_subdevice(info, s);
                if (snd_ctl_rawmidi_info(ctl, info) < 0) continue;
                out.push_back("hw:" + std::to_string(card) + "," + std::to_string(dev) + "," + std::to_string(s) +
                              "  " + snd_rawmidi_info_get_subdevice_name(info));
            }
        }
        snd_ctl_close(ctl);
    }
    return out;
}

}  // namespace wsc
