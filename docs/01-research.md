# OP-1 / OP-1 field research notes

Everything below is what is *publicly* known, what is *actually* reusable, and what the
instrument does that a plugin would have to reproduce. Sources are linked so each claim can be
checked rather than taken on trust.

---

## 1. What the machine actually is

| | OP-1 (2011, R246 firmware) | OP-1 field (2022, 1.6.x) |
|---|---|---|
| Tape | 4 tracks, 6 min, mostly mono, 24-bit | 4 **stereo** tracks, 6 min each, 8 tapes, 32-bit |
| Tape styles | none (one tape character) | Studio 4-track, Vintage 4-track, Porta 4-track, Disk Mini |
| Internal rate | 88.2 kHz engine / 44.1 kHz session | 32-bit stereo signal path (marketing says 96 kHz capable) |
| Tricks | 8 "tape tricks" keys | same 8, plus undo, crossfade splice, per-track USB outs |

Sound On Sound reviewing the field put the concept in one line: *"With stereo to match all the
possible audio sources, the tape is kind of an 8‑track in old money."*
([SOS review](https://www.soundonsound.com/reviews/teenage-engineering-op-1-field)) — which is
more or less the brief you gave me, so the idea has a precedent in how people already talk about
this machine.

**The four tape styles, as reviewers and owners describe them** (they are the single most
important thing to model, far more than any "warmth" knob):

| Style | Speed | Character reported by owners |
|---|---|---|
| Studio 4-track | 15 ips | cleanest, "total tape silence", no wobble |
| Vintage 4-track | 7½ ips | warmth, gentle saturation, slight wobble, low hiss — the default people leave it on |
| Porta 4-track | 3¾ ips (cassette) | obvious hiss, audible saturation, real wow |
| Disk Mini | magneto-optical | clean but compressed, odd upper-harmonic artefacts, hard top-end wall |

Sources: [TE field user guide](https://teenage.engineering/_img/6275254dfb267f0004b9e832_original.pdf),
[SOS](https://www.soundonsound.com/reviews/teenage-engineering-op-1-field),
[MusicRadar](https://www.musicradar.com/reviews/teenage-engineering-op-1-field),
[OP Forums A/B of all four](https://op-forums.com/t/comparison-of-tape-sound-recordings/30195).

**The eight tape tricks** (keys 1–8 in tape/mixer mode) — this vocabulary is the part of the OP-1
that a plugin can genuinely learn from
([TE tape-mode guide](https://teenage.engineering/guides/op-1/original/tape-mode),
[manual p.23](https://www.manualslib.com/manual/753903/Teenage-Engineering-Op-1.html?page=23)):

1. LOOP IN · 2. LOOP OUT · 3. LOOP TOGGLE · 4. BREAK (stops the tape, but an active loop keeps
running underneath so the break stays in time) · 5. REVERSE · 6. CHOP (tempo-locked repeat) ·
7. MEMO 1 · 8. MEMO 2 (momentary recall of any parameter set; 1+2 together interpolates between
the two memos).

Plus: lift / drop / split / join / lift-all / merge-drop, `SHIFT + stop` for a finer tape grid,
`STOP + ←/→` to jump to head or end, `SHIFT + rewind/FF` to jump bar markers, beat-match.

---

## 2. Can the firmware be reverse engineered? (technical answer)

Yes, publicly, and in considerable depth — but not in a way that gives you usable DSP code.

**What is public and legitimate:**

| Project | Licence | What it is |
|---|---|---|
| [op1hacks/op1repacker](https://github.com/op1hacks/op1repacker) | MIT | unpacks/repacks `.op1` files (`CRC32 + LZMA alone + tar`), applies stock mods |
| [BertBrandstifter/op1-sdk](https://github.com/BertBrandstifter/op1-sdk) | MIT | a real SDK for adding engines/FX/sequencers to R246, with a Blackfin simulator, byte-exact build proof, and — for our purposes — **`docs/internals.md`, a documented map of TE's firmware** |
| [antpb/op1r-src](https://github.com/antpb/op1r-src) | GPL-3.0 | custom firmware R246: extra engines, quad LFO, USB track outs |
| [op1r patcher](https://pcppcp.github.io/op1r/web/) | — | browser-side firmware patcher |

From the SDK's internals document (facts, not code, and the document itself is MIT):

- CPU: **ADSP-BF524C2** (Blackfin), TE code is VisualDSP++ C++ on the VDK RTOS; image
  `OP1_vdk.ldr` = 801 LDR blocks; stock R246 SHA-256 pinned as `c5315218…13a9`.
- Audio: callback at `0xff80036c`, **64 stereo frames**, SPORT0 I2S, engines render at **88.2 kHz**.
- UI: 320×156, framebuffers at `0x02000000`/`0x03000000`, 16-bit 5-6-5, 512-byte stride,
  double-buffered; AGG rasteriser plus a rapidxml **SVG screen loader** — which is exactly why the
  OP-1's screens look like a vector instrument: they *are* SVG.
- Registration tables: FX = delay, grid, punch, phone, spring, filter, nitro, cwo; modes = synth,
  drum, **tape (id 3)**, mixer, tempo, mic, album, seq, browser.
- ~2 MB of free SDRAM at `0x01e00000–0x01ffffff`; op1r puts 109 KB of custom code there and hooks
  37 call sites + 14 table pointers.

**What is blocked:** the L1 blocks (48 KB of instruction SRAM plus most L1 data) are **encrypted** —
in every 24 bytes the first 8 are scrambled. `te-boot` contains XTEA (32 rounds) and manipulates the
chip's OTP memory, so the key is per-device, not in the update file. Because the reset/startup code
lives in L1, TE's own firmware cannot be booted in the simulator without decrypting it. The public
projects deliberately work around this instead of attacking it, and they redistribute **no TE code** —
they patch your own copy.

**Why this is still a dead end for a plugin:** even with full access, what you would get is
Blackfin assembly for a 200 MHz fixed-point DSP with a proprietary RTOS. It cannot be linked into a
JUCE/CLAP x86-64 or arm64 plugin, and translating it would be both enormous and legally radioactive.
The *ideas* are useful (how many heads, what the tape does at each speed, which screens, what the
parameter ranges are); the *code* is not.

**Conclusion:** model the machine, don't port it. That is also what the SDK's own author does when
writing new engines.

---

## 3. Open-source code that is genuinely usable

Ranked by how much of a tape plugin it can actually carry.

### 3.1 `chalkwalk/chalkwalk-tape` — MIT, C++17, JUCE-free ★ the important find

A tape deck **as a library**: medium, heads, layers, seams, resampling. It is not a delay line and
not a looper — it is the parts you would build either from. Explicitly designed so that nothing on
the audio thread can reach a file, a message thread or an allocator.

- `Medium.h` — the tape: geometry, f32/i16 depth behind one accessor, lazy commit
- `Heads.h` / `EraseHead.h` — read/write heads as index arithmetic; erase as a head, not zeroes
- `LayerStack.h`, `Seam.h`, `MarkerLane.h` — takes, loop joins, markers
- `Resampler.h` — polyphase windowed sinc **with scatter** (depositing at a fractional position),
  which is what writing to tape at a variable speed actually is. No mainstream resampler ships the
  transpose of interpolation.
- Physics: `Hysteresis.h` (Jiles–Atherton, plus a vectorised batch), `Bias.h`, `Transport.h`
  (wow, flutter, scrape, motor ripple from reel and capstan geometry), `WearMap.h`, `TapeNoise.h`,
  `Compander.h` (dbx-style), `SlidingBand.h`
- Reproduce losses as closed forms: `LossEffects.h` (spacing, thickness, gap, azimuth),
  `HeadLengthLoss.h` (Duinker & Geurst), `TapeEq.h` (NAB, IEC/CCIR, cassette), `MinimumPhase.h`
  (cepstral FIR), `FirDesign.h`, `DesignCache.h`
- One dependency: [signalsmith-dsp](https://github.com/Signalsmith-Audio/dsp) (MIT, header-only),
  and only for a Kaiser window.

MIT means no licence contamination, and the physics is keyed to **wavelength and speed** rather
than to a "tone" knob — the same discipline I used in our prototype engine.

### 3.2 Tape saturators to read (not to link into closed code)

| Project | Licence | Take from it |
|---|---|---|
| [jatinchowdhury18/AnalogTapeModel](https://github.com/jatinchowdhury18/AnalogTapeModel) (CHOW Tape) | GPL-3.0 | the reference implementation of the DAFx-19 paper: hysteresis, bias, deadzone, head bump, "chew", oversampling |
| [lowwavestudios/Track24](https://github.com/lowwavestudios/Track24) | GPL-3.0 | simple JUCE tape saturator with even/odd harmonic texture control |
| Airwindows (e.g. ToTape) | MIT | single-file, no-framework tape/console processing; check the header in each file |
| [milomaurer23/tape](https://github.com/milomaurer23/tape) | — | a from-scratch hysteresis/wow/flutter engine **with tests**, useful as a testing template |

GPL-3.0 is fine for a plugin you never distribute; it becomes a problem the moment you ship it.

### 3.3 Frameworks

| | Licence | Verdict for this project |
|---|---|---|
| **JUCE 8** | dual: **AGPLv3** or commercial (Personal tier free, no splash) | best fit. Personal/non-sold → either the free Personal licence or AGPLv3. Note JUCE's own README asks tools to mention that a commercial licence may be required. |
| **iPlug2** | GitHub reports "Other"/NOASSERTION; secondary sources disagree (MIT vs GPL-3.0+) | lighter and permissive-if-actually-MIT, but **verify `LICENSE` before committing** — I would not build on it until that is read |
| CLAP | MIT | the plugin *API*; use as an additional format alongside VST3/AU |
| VST3 SDK | GPL-3.0 or Steinberg proprietary | with AGPLv3 JUCE you can only take the GPL-3.0 branch |

---

## 4. The DSP literature worth having on disk

- **Chowdhury, "Real-time Physical Modelling for Analog Tape Machines", DAFx-19** —
  [PDF](https://ccrma.stanford.edu/~jatin/420/tape/TapeModel_DAFx.pdf). Hysteresis, bias, head bump,
  deadzone, and a real machine measured against the model.
- **Jiles & Atherton (1986)** — the magnetisation model everyone's tape hysteresis descends from.
- **Duinker & Geurst** — finite head-length contour loss (in `chalkwalk-tape`).
- **NAB / IEC 94 / CCIR** — the record/reproduce equalisation curves; they are why "7½ ips" sounds
  darker than "15 ips" for a reason rather than as a preset.
- **Krasnosel'skii play operator** — the cheap, rate-independent hysteresis element I used in the
  prototype (stable at any drive, no ODE solve). Not as accurate as J-A; see `docs/03-engine.md`.

---

## 5. What this means for an 8-track tape plugin

1. **No TE code, ever.** Clean-room behaviour modelling, with measurements taken from an instrument
   you own. Legally comfortable, and technically the only thing that fits a desktop plugin.
2. **Copy the interaction design, not the pixels.** Eight tricks, four speeds, a grid, memo A/B,
   break-that-stays-in-time — these are *functions*, and they are the reason the tape feels like tape.
3. **Build the character from physics.** Speed → gap loss → HF loss; bias/drive → hysteresis →
   harmonics; reel geometry → wow; track width → crosstalk. Every knob is then a physical quantity,
   which is also what makes four styles fall out of one engine instead of four parallel presets.
4. **`chalkwalk-tape` (MIT) is the strongest starting point** for the transport/medium/loss layer;
   write our own hysteresis/tone stage on top of the DAFx-19 model.
5. **Eight tracks is a real design choice, not a gimmick** — Field owners already call its four
   stereo tracks an 8-track. It also means the mixer, panning and crosstalk models matter.
