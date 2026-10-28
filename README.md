# workstation-core

The audio core of the synthesizer workstation OS: a userspace real-time audio engine
that owns the sound card exclusively, the way OSS did, and runs every sound engine in
one tightly scheduled graph. Working name; the real name is still open.

Stage 1 (this tree) proves the path from a key press to the DAC:

- `AlsaDevice`: exclusive `hw:` playback, no plugins, no resampling, mmap when the driver offers it, xrun recovery.
- `MidiInput`: one rawmidi port on its own thread, every event timestamped on arrival, lock-free queue to the audio thread.
- `AudioCore`: the real-time loop. `SCHED_FIFO`, locked memory, denormals off, no allocation after start.
- `Engine`: the interface every instrument implements. `SineEngine` is the proof engine.
- `wsc-play`: run it, play notes, watch xruns and render time once a second.

## Build

```
sudo apt install build-essential cmake ninja-build libasound2-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## Run

```
./build/wsc-devices                       # find your hw: device and MIDI port
sudo ./build/wsc-play --device hw:1,0 --midi hw:2,0,0 --period 64
./build/wsc-play --device hw:1,0 --tone 69   # test tone, no keyboard needed
```

Without root, add to `/etc/security/limits.d/audio.conf` and log in again:

```
@audio - rtprio 95
@audio - memlock unlimited
```

The Stage 1 exit test is 24 hours at 64 frames with `xruns` staying at 0. The kernel
side of that (PREEMPT_RT, IRQ threading, CPU isolation) lives in `workstation-os`.

## Layout

```
include/wsc/   public headers
src/           implementation
apps/          wsc-play, wsc-devices
tests/         unit tests, no external framework
docs/          design notes
```

## Rules for code on the audio thread

No `new`, no `malloc`, no locks, no file or socket I/O, no `std::string`, no exceptions,
no syscalls other than the device write. Measure with `wsc-play` before and after any change.
