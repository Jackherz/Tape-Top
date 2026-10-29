# Tape–Top

An eight-track tape instrument, modelled after the tape section of a certain portable Swedish
synth — built as a personal project, from scratch, with no Teenage Engineering code in it.

This repository currently holds: the research, a working DSP engine with tests, a UI feel prototype,
and a plugin shell for macOS (VST3/AU/standalone) that has been written but not yet compiled — see
`plugin/README.md` for exactly where that stands.

## What's here

```
docs/
  01-research.md    OP-1 / field internals, what the firmware reveals, what is reusable, literature
  02-legal.md       the licensing/IP answer: what is safe, what is not, and why
  03-engine.md      the tape model: signal path, stock parameters, measurements, limits
  04-ui.md          design language, tokens, layout, interaction vocabulary
  05-roadmap.md     plugin architecture, media budget, phases, open questions
dsp/
  include/tt/       the engine (header-only, C++17, no dependencies)
    TapeMachine.h   record head, EQ, compander, transport, 8 tracks, deck
    Recorder.h      takes, punch, undo, lift/drop, split/join, splice
  src/demo.cpp      renders an eight-track sketch through all four stocks
  src/*_test.cpp    16 engine assertions + 21 recorder assertions
plugin/             JUCE 8 shell (VST3/AU/standalone), macOS
ui/                 the panel prototype; plays the engine renders
```

## Run it

Everything can be driven from the repo root:

```bash
./scripts/build-macos.sh   # the plugin (VST3 / AU / standalone), macOS
make test      # 37 assertions: engine + recorder/editor
make render    # render the four stocks to ui/audio/
make ui        # serve the panel on :8080
make plugin    # configure the JUCE build (macOS)
```

Or by hand, from inside this checkout:

```bash
# 1. the engine: build, test, render the demos
make -C dsp test
make -C dsp render          # writes dsp/out/*.wav and copies them into ui/audio/

# 2. the panel  (note: ui/ is inside the repo, not at ~)
cd ui && python3 -m http.server 8080
# open http://localhost:8080  (click anywhere to start the transport)
```

Keyboard: `space` play/stop, `1–8` tape tricks, `←/→` seek. Drag knobs vertically.

## What it sounds like

Four renders of the same eight-track sketch, plus a transport pass:

| file | what |
|---|---|
| `00-dry-digital.wav` | the sketch with no tape, for A/B |
| `01-studio-15ips.wav` | 15 ips, clean, no wobble |
| `02-vintage-7.5ips.wav` | 7.5 ips, head bump, a breath of wow |
| `03-porta-3.75ips.wav` | cassette: hiss, saturation, real wow |
| `04-diskmini.wav` | companded, hard 15 kHz wall |
| `05-tricks-stop-reverse-chop.wav` | tape stop → spin up → reverse → chop → brake |

## The one-paragraph version of the research

The OP-1's firmware is unusually well documented in public (op1repacker, op1r, and an MIT-licensed
SDK that maps the whole thing down to hook addresses), but its DSP lives in **encrypted Blackfin
assembly** for a 200 MHz fixed-point DSP — useless in a desktop plugin both technically and legally.
So: no code reuse, ever; model the machine from measurement instead. What *is* reusable is
`chalkwalk-tape` (MIT, a genuine tape-deck library: medium, heads, Jiles–Atherton hysteresis, wear,
wow from reel geometry, NAB/IEC EQ, and a scatter resampler), plus the DAFx-19 tape-modelling paper
that CHOW's GPL implementation follows. In the UK, observing and measuring the instrument you own is
expressly permitted (CDPA s.50BA); decompiling beyond interoperability, or decrypting the firmware,
is not — and "personal use" changes none of that. See `docs/02-legal.md`.

## Status

- engine: working, measured — 37/37 assertions pass
- recording/editing layer: working — record, punch, undo, lift/drop, split/join, splice
- UI: prototype (no recording in the browser)
- plugin: written, not yet compiled — first build needed on a Mac

Not affiliated with Teenage Engineering. `OP-1` and `OP-1 field` are their trademarks and are used
here only to describe what inspired the model.
