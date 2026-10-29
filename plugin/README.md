# Tape-Top plugin (macOS: VST3 · AU · Standalone)

The plugin shell that wraps the engine in `../dsp` and the panel design in `../ui`.

## Build

```bash
cd plugin
cmake -B build -G Xcode            # or: -G Ninja
cmake --build build --config Release
```

JUCE 8 is fetched by CMake at configure time (first configure takes a few minutes and needs network).
`COPY_PLUGIN_AFTER_BUILD` is on, so the AU/VST3 land in `~/Library/Audio/Plug-Ins/…` automatically.

Requirements: Xcode 15+, CMake 3.22+.

## What this is

```
PluginProcessor   host transport ↔ deck transport, recording, editing, state
PluginEditor      the panel: drawn, not assembled from widgets
Theme.h           the design tokens (same palette as ui/ui.css)
../dsp            tt::Deck, tt::TapeEditor — the same code the tests exercise
```

- Stereo in, stereo out. Incoming audio is printed through the record head onto whichever tracks are
  **armed**, at the transport position, exactly as the tape workflow implies.
- The output is the deck's eight strips mixed and summed through the master section.
- Host transport drives the deck; when the host is stopped, the panel's transport is the clock.
- Record (key 4 / `T` on a lane) arms the selected track and starts capture. Punch, lift, drop, undo
  and erase call straight into `tt::TapeEditor`.

## Honest status

**This has not been compiled yet.** The sandbox this was written in has no X11, ALSA or freetype
development headers, so JUCE's GUI modules cannot be built there. Everything in `../dsp` *is*
compiled and tested (37 assertions); the JUCE layer is written, reviewed for API correctness against
JUCE 8, and expected to need a short first-build pass on your machine — most likely candidates:
`FontOptions` construction in `Theme.h`, and any deprecation warnings from `juce_recommended_warning_flags`.

If configure fails on the JUCE download, vendor it manually:

```bash
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git ../JUCE
# then in CMakeLists.txt, add_subdirectory(../JUCE JUCE) instead of FetchContent
```

## Not implemented yet

- **Persisting the medium.** Plugin state stores settings only. Two minutes × 8 stereo tracks at
  48 kHz float32 is ~2 GB, so tape audio needs its own store (see `docs/05-roadmap.md`).
- **Waveform rendering on the lanes** — takes are drawn as hatched blocks; the medium does not yet
  expose a peak cache.
- **Recording to more than the armed track set**, loop-record comping, and the tape grid selector
  (the BAR/BEAT/STEP chips are decorative in this build).
- **Meter ballistics** are peak-with-falloff, not VU.

## Controls

| | |
|---|---|
| Space | play / stop |
| 1–8 | tape tricks: loop in, loop out, loop, break, reverse, chop, lift, drop |
| T1–T8 label | arm that track |
| Lane body | select track / scrub |
| Drag knobs | master EQ, output, per-strip gain |
| BREAK | hold to spin the transport down, release to spin back up |
