# Roadmap and the decisions only you can make

## Decisions (locked)

| | |
|---|---|
| Host | **macOS**: VST3 + AU + Standalone (Logic/Ableton/Bitwig), Apple silicon + Intel |
| Shape | **Hybrid**: one deck you can record into, with per-track tape character |
| Recording | **Full**: record, punch, overdub, undo, lift/drop/split/join |
| Sound | **Faithful**: Studio / Vintage / Porta / Disk Mini, calibrated against the real unit |

## Where this stands

| | Status |
|---|---|
| Research: firmware, licences, literature | done — `01-research.md`, `02-legal.md` |
| Tape engine + measurements + tests | done — `dsp/`, 16 assertions passing, 5 wav renders |
| UI feel prototype | done — `ui/`, live in the browser, four stocks, tricks, transport |
| Recording / editing layer (`tt::TapeEditor`) | done — record, punch, undo, lift/drop, split/join, splice crossfade, 21 assertions |
| Plugin shell (VST3/AU) | written in `plugin/`, **not yet compiled** (no GUI dev headers in the build sandbox) |

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

**Phase 1 — first build (hours).** `cd plugin && cmake -B build -G Xcode && cmake --build build` on
your Mac, fix whatever the first compile surfaces, load it in the DAW, arm T1, hit record. The panel,
the deck, the recorder and the state are already wired.

**Phase 2 — the tape (the real work).** The editing logic exists and is tested; what is missing is
the *storage* (below) and the workflow details: monitoring while recording (the record/play head gap),
loop-record comping, solo/mute, and the tape grid selector.

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

## Still open

1. **Media persistence.** Where does the tape live between sessions — a `.tapetop` folder next to
   the project, `Application Support`, or inside the DAW state (bad idea at this size)? Recommendation:
   a folder, referenced by path in the state, with "missing tape" handling.
2. **Tape length.** 2 min/track default, or go straight for 6 like the field and accept ~500 MB at
   int24/48k? This decides whether we need streaming or can stay in RAM.
3. **Default stock.** Vintage (as in the prototype) or Studio, since Studio is the one that will not
   surprise you on a mix bus?
