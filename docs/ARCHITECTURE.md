# Architecture notes

The full assessment and staged roadmap live in the project document
("Synthesizer Workstation OS: Feasibility and Roadmap"). This file records the
decisions that shape the code.

## Signal path

```
MIDI port ──(rawmidi, timestamped)──> ring buffer ──> audio thread
                                                        │
computer keyboard (evdev, Stage 4) ───────────────────> │
                                                        ▼
                                      engines render into float buffers
                                                        │
                                            master gain, limiter (Stage 2)
                                                        │
                                      AlsaDevice converts to S32/S24/S16
                                                        │
                                              hw: device, mmap ──> DAC
```

Nothing between the engines and the converter resamples, mixes with other apps or
changes volume. If the interface runs at 48 kHz, every engine runs at 48 kHz.

## Timing

- One `period` of `period_frames` is rendered per loop. Default 64 frames at 48 kHz
  (1.33 ms), two periods in the ring, so nominal output latency is 2.67 ms.
- MIDI events are stamped with `CLOCK_MONOTONIC` when their last byte arrives and placed
  inside the next block at the frame matching their lateness, so the spacing between
  notes is kept to the sample even though they are applied one block late.
- `CoreStats` exposes the worst render time; the budget is the period length.

## What Stage 2 adds

- Engine host: many engines, a graph across CPU cores, a mixer with per-layer gain ramps.
- Parameter descriptors (`id`, `taper`, `unit`, `smooth_ms`) and the socket protocol for the UI.
- A master limiter so stacked engines can never hard-clip.
