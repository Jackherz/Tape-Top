# Tape-Top plugin (macOS: VST3 · AU · Standalone)

The plugin shell that wraps the engine in `../dsp` and the panel design in `../ui`.

## Build

```bash
cd Tape-Top            # the repo root
./scripts/build-macos.sh          # picks a working generator, builds Release
# or: make plugin
```

JUCE 8 is fetched by CMake at configure time (first configure takes a few minutes and needs network).
`COPY_PLUGIN_AFTER_BUILD` is on, so the AU/VST3 land in `~/Library/Audio/Plug-Ins/…` automatically.

Requirements: macOS 10.15+, CMake 3.22+, and the Xcode **Command Line Tools**
(`xcode-select --install`). Full Xcode is optional; the script uses Ninja or Unix Makefiles.

### Troubleshooting (the two things that actually go wrong)

**`cd: no such file or directory: plugin`** — you are not inside the repo. The code lives in the
clone, not in your home folder:

```bash
git clone -b arena/e95242b2-tape-top https://github.com/Jackherz/Tape-Top.git
cd Tape-Top
ls                     # docs  dsp  plugin  scripts  ui
```

**`CMake Error: Xcode 1.5 not supported / Could not create named generator Xcode`** — CMake could not
find a full Xcode install. That is normal on a machine with only Command Line Tools, where
`xcode-select -p` prints `/Library/Developer/CommandLineTools`. Two fixes:

```bash
# A. don't use the Xcode generator (works fine, this is what the script does)
cmake -S plugin -B plugin/build -G Ninja            # or -G "Unix Makefiles"
cmake --build plugin/build --parallel

# B. if you do have full Xcode installed, point the toolchain at it
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
```

Also note `cmake --build build --config Release` only applies to multi-config generators (Xcode,
Ninja Multi-Config). With Ninja or Makefiles, `-DCMAKE_BUILD_TYPE=Release` is set at configure time
and the build command takes no `--config`.

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
