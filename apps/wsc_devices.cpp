// wsc-devices: list the raw hw: playback devices and MIDI input ports the core can use.

#include "wsc/alsa_device.hpp"

#include <cstdio>

int main() {
    std::puts("playback devices (use with wsc-play --device):");
    for (const auto& d : wsc::list_playback_devices()) std::printf("  %s\n", d.c_str());
    std::puts("midi inputs (use with wsc-play --midi):");
    for (const auto& m : wsc::list_rawmidi_inputs()) std::printf("  %s\n", m.c_str());
    return 0;
}
