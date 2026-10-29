# Roadmap and the decisions only you can make

## Where this stands

| | Status |
|---|---|
| Research: firmware, licences, literature | done — `01-research.md`, `02-legal.md` |
| Tape engine + measurements + tests | done — `dsp/`, 16 assertions passing, 5 wav renders |
| UI feel prototype | done — `ui/`, live in the browser, four stocks, tricks, transport |
| Plugin shell (VST3/AU) | not started — needs the four answers below |
| Recording / media layer | not started |

## Proposed architecture

```
┌──────────────── plugin (JUCE 8, AGPLv3 or Personal) ────────────────┐
│  PluginProcessor                                                    │
│   ├── tt::Deck           ← dsp/include/tt/TapeMachine.h             │
│   │    ├── Transport, 8 × Track (medium), master EQ/glue/limiter    │
│   ├── MediaStore         ← per-track float32 tape, or 24-bit + swap  │
│   ├── Recorder           ← live input → medium through record path   │
│   └── StatePreset        ← stock, speed, tricks, mixer               │
│  PluginEditor                                                       │
│   └── the panel in ui/, ported to JUCE components                   │
└─────────────────────────────────────────────────────────────────────┘
```

Two structural decisions the prototype already forces:

**1. Format.** JUCE 8 + CMake, targeting VST3 + AU + standalone on macOS (and Windows if you need
it). AU matters if you are on Logic; standalone matters for using it as an instrument without a DAW.
CLAP is a cheap extra later (MIT headers, JUCE needs a third-party wrapper).

**2. Media storage.** This is the one real engineering constraint. Six minutes × 8 stereo tracks:

| depth | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| float32 | 1.0 GB | 1.1 GB | 2.2 GB |
| int24 | 762 MB | 829 MB | 1.66 GB |
| int16 | 508 MB | 553 MB | 1.1 GB |

The OP-1 field does 4 stereo × 6 min in 256 MB of *total* device memory, so it cannot be storing raw
float32 — either its tape is shorter than advertised per track, or it is compressed, or the "32-bit"
is the signal path rather than the medium. For us: default to **int24 at the project rate, 2 minutes
per track, streamed to a temp file, with a "long tape" setting**; offer float32 in memory as an
option if you have RAM. Do not silently allocate 1.1 GB on the audio thread at load, which is the
default way this goes wrong.

## Phases

**Phase 1 — shell (a weekend).** JUCE project, parameters bound to `TapeSpec`, the panel, audio
passing through the deck with a single stereo input recorded to track 1. Ship a working insert effect.

**Phase 2 — the tape (the real work).** Live record with punch-in/out, erase-as-a-head, lift/drop/
split/join, undo (OP-1 1.6 added tape undo for a reason), splice crossfade, eight armed tracks,
solo/mute, and the record/play head gap.

**Phase 3 — authenticity.** Swap the Preisach hysteresis for Jiles–Atherton; add azimuth and
head-length losses from `chalkwalk-tape` (MIT); add wear/dropout as a *stateful* property of the
medium (the head wears where you play); model the Disk Mini compander properly with pre-emphasis.

**Phase 4 — the extras that make it an instrument.** Varispeed as a musical parameter (not just a
control), tempo-locked chop, memo A/B with interpolation between them, MIDI control for the tricks
(so a foot pedal can do BREAK), and per-track stem export.

**Phase 5 (optional, and fun) — the other direction.** `op1-sdk` (MIT) makes it possible to write a
tape-flavoured effect *for the actual OP-1*: route **b**, an effect hosted on a free FX slot (delay,
grid, punch, phone, filter, nitro are the free hosts), with the DSP written in C for a 200 MHz
Blackfin. Budget is tight — 1–3.5 % of the OP-1's DSP time for a small effect per the SDK's own
measurements — but a "tape stop + wobble + saturation" punch-in is a realistic first project, and it
would make a nice companion to the plugin. Entirely separate codebase; no legal crossover, since the
SDK is MIT and patches your own firmware.

## The four questions I need answered

1. **Host and OS.** macOS-only AU/VST3 + standalone, or Windows too? Are you in Logic, Ableton,
   Bitwig, Reaper?
2. **What is it, exactly?** (a) a multitrack tape *recorder/looper* inside the DAW — record eight
   tracks, arrange them on a tape, mix and print; (b) a tape-character *insert* for one source, with
   eight strips as parallel tape voices; or (c) a hybrid — one tape deck you can record into, with
   per-track processing.
3. **Recording.** Does it need to capture live audio into the eight tracks (with punch/undo), or is
   the tape mostly a playback/arrangement surface for clips you drop on it?
4. **Sound target.** Faithful to the field's four stocks, or a wider "tape laboratory" (more speeds,
   bias, wear, track width) with the four stocks as starting points?

Answer those and Phase 1 stops being speculative.
