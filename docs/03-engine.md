# The tape engine (what `dsp/` is, and what it is not)

`dsp/` is a working, dependency-free C++17 tape model with an offline renderer and 16
assertions. It exists for three reasons: to prove the sound before any plugin shell exists, to
fix the parameter set, and to give the UI something real to play.

```
dsp/
  include/tt/Dsp.h            biquad, one-pole, DC blocker, noise, 4-point interpolation
  include/tt/TapeMachine.h    record head, EQ, compander, transport, track, deck
  src/demo.cpp                an eight-track sketch rendered through all four stocks
  src/engine_test.cpp         assertions (make test)
  Makefile                    make / make test / make render
```

`make render` writes `dsp/out/*.wav` and copies them to `ui/audio/` for the prototype.

---

## 1. Signal path

One `Deck` = one `Transport` + eight `Track`s + master section. The transport is shared on
purpose: on real tape all tracks wow together, and that shared wobble is a large part of why a
multitrack recorder sounds like one machine.

```
                       ┌──────────────── record head ────────────────┐
 in ─▶ pre-emphasis ─▶ │ bias ─▶ hysteresis ─▶ drive trim ─▶ DC block │ ─▶ medium (f32, per track)
                       └─────────────────────────────────────────────┘
 medium ─▶ interpolate @ transport position ─▶ neighbours (crosstalk)
        ─▶ de-emphasis ─▶ gap/space loss ─▶ head bump ─▶ rumble HP ─▶ DC
        ─▶ compander decode ─▶ hiss + dropout ─▶ head-gain(×speed) ─▶ pan/gain
                                                                        │
  eight tracks ─▶ master 3-band EQ ─▶ glue compressor ─▶ soft limiter ─▶ out
```

### Why each block is where it is

- **Pre-emphasis before the hysteresis, de-emphasis after it.** Recording HF hot and pulling it back
  on reproduce is what makes tape's HF saturation behave; skipping the undo is the difference
  between a tape model and a brightener. The EQ pair is verified to cancel (test 1).
- **Head gain scales with speed.** A reproduce head answers the *rate of change* of flux, so a
  stopped tape is silent, not muted, and a half-speed tape is quieter as well as lower. This is what
  makes BREAK/tape-stop feel mechanical instead of like a fader (test 5).
- **Hiss does not scale with speed.** A stopped tape still hisses — and that residual noise is the
  single most convincing thing about the stop in the render.
- **Crosstalk reads the neighbouring track's medium**, not its output, so pan/gain stay independent.
- **Glue compressor detector is 12 ms / 300 ms.** Anything faster and the low end disappears,
  because the detector starts tracking the bass waveform rather than its envelope. I had exactly
  this bug and it looked like "the model has no sub" in the spectrum.

---

## 2. The four stocks

| | Studio | Vintage | Porta | Disk Mini |
|---|---|---|---|---|
| speed (ips) | 15 | 7.5 | 3.75 | 7.5 |
| head gap (µm) | 1.2 | 2.2 | 3.6 | 2.6 |
| head bump | 42 Hz +2 dB | 62 Hz +3 dB | 92 Hz +4 dB | 70 Hz +1 dB |
| HF loss @10k | 0.5 dB | 3 dB | 7 dB | 9 dB |
| record drive | −2 dB | +1.5 dB | +5 dB | +1 dB |
| wow / flutter | 0.04 % | 0.12 % | 0.35 % | 0.02 % |
| hiss | −86 dB | −68 dB | −52 dB | −78 dB |
| compander | – | – | – | 85 % |

Measured, relative to the dry digital mix (band energy, same peak):

```
                 20Hz   60   120   250   500   1k    2k    4k    8k   12k   16k  centroid
studio 15 ips    -2.2  +3.4  +2.9  +2.4  +2.4  +2.4  +2.2  +1.7  +0.1  -2.1  -4.4   4.9 kHz
vintage 7.5      -2.7  +3.3  +4.4  +2.6  +2.4  +2.3  +2.0  +0.5  -3.3  -7.1 -10.3   3.8 kHz
porta 3.75       -2.9  +2.2  +5.1  +3.9  +2.6  +2.2  +0.5  -4.4  -9.0 -11.5 -12.9   2.9 kHz
disk mini        -3.6  +2.5  +2.9  +3.6  +2.4  +1.8  +0.8  -1.8  -8.3 -13.4 -17.0   3.0 kHz
dry                       6452 Hz centroid, 0 dB everywhere
```

The important property: **the difference between stocks falls out of speed and gap**, not out of
four sets of tone knobs. Change 15 ips to 3.75 and you get the portamento's whole HF story for free.

Static behaviour of the record head (1 kHz):

| | −20 dBFS in | 0 dBFS in |
|---|---|---|
| Studio | −0.05 dB, 0.5 % THD | −1.3 dB, 21 % THD |
| Vintage | −0.2 dB, 2.3 % THD | −2.6 dB, 21 % THD |
| Porta | −1.1 dB, 1.2 % THD | −5.1 dB, 9 % THD |
| Disk Mini | −0.2 dB, 0.8 % THD | −2.4 dB, 12 % THD |

i.e. transparent at nominal level, compressing when slammed, and the cassette stock gives up first.

---

## 3. Known limitations (deliberate, for now)

1. **Hysteresis is a play-operator Preisach, not Jiles–Atherton.** Chosen because it is stable at
   any drive and any sample rate with no ODE solve. Consequence: the loop shape is not measured
   against a real B-H curve, and the bias is modelled as "how much loop survives" rather than as an
   actual 100 kHz AC bias. `chalkwalk-tape` has a proper J-A `Hysteresis.h` (MIT) — swapping it in is
   the single biggest authenticity upgrade available.
2. **No azimuth loss, no head-length ripple, no wear.** All four are closed forms in `chalkwalk-tape`;
   they are the difference between "warm" and "worn".
3. **4-point Lagrange interpolation**, not polyphase sinc. Fine for ±20 % varispeed; wrong for a
   full-octave varispeed or for pitch-down effects.
4. **No record/play head gap delay.** Real machines put the record head a few ms before the play
   head, which is why monitoring while recording sounds slightly late. One line of code, deliberately
   postponed until the record path exists.
5. **No recording.** The demo prints a prepared sketch in one pass; the plugin needs a live record
   path, punch, undo and splice crossfades.
6. **Media is 32-bit float in RAM.** Six minutes × 8 stereo tracks at 48 kHz is **1.1 GB**. The OP-1
   field ships 256 MB total, which means TE is not storing what I am storing — see the roadmap for
   the storage decision.

---

## 4. Tests

`make -C dsp test` — 16 assertions:

- record/reproduce EQ cancels within 1.5 dB at 100 / 500 / 2000 Hz;
- every stock is within 2 dB of unity at −20 dBFS, and compresses at 0 dBFS;
- 12 kHz loss is ordered 15 ips > 7.5 ips > 3.75 ips, and Porta is ≥ 6 dB darker than Studio;
- a stopped transport drops ≥ 40 dB;
- loop regions wrap;
- eight silent tracks produce hiss between −30 and −80 dBFS (audible, not a wall).

Two of those tests caught real bugs during development (a biquad using input history in the
denominator, and a transport advancing in samples while reporting seconds — playback was running at
44100× and sounding like filtered noise). Worth keeping.
