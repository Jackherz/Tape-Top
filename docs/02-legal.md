# Licensing, IP and "I own the hardware, I won't sell it"

Short version: **the plugin is completely safe to build if it contains no TE code and no TE
firmware.** "Personal use only" lowers your exposure but changes no rule. The line is not
*commercial vs personal* — it is *expression vs behaviour*.

*Not legal advice; I am not a lawyer. This is what the statutes and the recent case law say, so you
can decide what risk you want to carry on your own machine.*

---

## 1. What copyright does and does not cover

Copyright protects the **expression** of a program — the code — not the ideas, procedures, methods
of operation or mathematical concepts behind it. So:

| | Status |
|---|---|
| Reading TE firmware, copying its DSP routines, decompiling and translating them | infringement (and possibly circumvention, §2 below) |
| Listening to an OP-1, measuring its THD, its frequency response at each tape speed, its wow rate, its noise floor, and designing your own model against those numbers | this is what audio engineers have always done; the measurements are facts |
| Copying the *feature set* (8 tricks, 4 tape speeds, beat-match, memo A/B) | features and functions are not protected; the specific screen artwork, fonts and icons are |
| Using the name "OP-1" or TE's logo, or making the panel look like an OP-1 | trademark / passing off / trade dress — don't |
| Reusing MIT code (op1repacker, op1-sdk, chalkwalk-tape) | fine, keep the notice |
| Reusing GPL-3.0 code (op1r, CHOW Tape) in a plugin you never distribute | fine in practice; you cannot ship the result without publishing under GPL |

## 2. Reverse engineering: what UK/EU law actually gives you

You are in the UK, so the Copyright, Designs and Patents Act 1988 applies (post-Brexit it still
carries the old Software Directive, 2009/24/EC, as amended):

- **s.50BA** — a lawful user may *observe, study or test* the functioning of a program to determine
  the ideas and principles underlying it, and **s.296A** makes any contract term void to the extent
  it tries to stop you. This is the clause that permits black-box measurement.
- **s.50B** — decompilation, but only where necessary to achieve **interoperability** with an
  independently created program, and not if you use the information to make a program substantially
  similar in expression.

The 2025 UK High Court decision in *IBM v LzLabs* read both exceptions narrowly: observing *output*
is inside s.50BA, but "detailed examination of how the program achieved its output" is examining
expression, not function, and falls outside.
([Jones Day analysis](https://www.jonesday.com/en/insights/2025/05/uk-court-rules-on-reverse-engineering-of-mainframe-software),
[EM Law summary of ss.50B/50BA](https://emlaw.co.uk/software-technology-lawyers/software-licences/))

For us that draws a clean, useful line: **measure the machine, don't read its mind.** Load a sine
through the OP-1 at each tape speed, capture and analyse it — that is s.50BA territory. Disassemble
`OP1_vdk.ldr` to lift the tape routines — that is not, and it is also pointless (§4 of
`01-research.md`: it is Blackfin assembly).

## 3. The US wrinkle you should know about: DMCA §1201

If you ever try the interesting shortcut — decrypt the OP-1's encrypted L1 to look at TE's actual
tape code — that is a separate offence from infringement. 17 U.S.C. §1201(a)(1) prohibits
circumventing a technological measure that controls access to a work, and the L1 encryption is
precisely an access control on their code. The permanent carve-outs are narrow (§1201(f)
interoperability, (g) encryption research, (j) security testing, plus the triennial exemptions), and
none of them cover "I want to know how their tape sounds".
([Copyright Office 1201 background](https://www.copyright.gov/1201/1201_background_slides.pdf),
[2024 rulemaking](https://www.copyright.gov/1201/2024/2024_Section_1201_Registers_Recommendation.pdf))

Owning the device does not grant this; not selling the result does not grant this either. Note also
that the firmware is presumably under a TE EULA that forbids reverse engineering — breaching a
contract is a separate problem from copyright, and "I won't resell it" does not cure it.

**Practical upshot:** the firmware is a fascinating artifact and a poor source of code. The public
MIT/GPL projects already document its architecture; that documentation is what we need, and reading
it is unambiguously fine.

## 4. Licence hygiene for this repo

Current status, deliberately conservative:

- **No TE code, no TE firmware, no decrypted anything.**
- References to `op1-sdk` are *facts read from its documentation* (MIT-licensed text), not copied
  code. If we later copy a snippet, its MIT notice comes with it.
- Our engine (`dsp/`) is original and dependency-free → we can license it however we like. I have
  left the repo unlicensed; adding MIT to `dsp/` and the UI is a one-line decision for you.
- If we adopt `chalkwalk-tape` (MIT) → keep its notice, no obligations beyond that. Its dependency
  `signalsmith-dsp` is MIT too.
- If we look at CHOW/AnalogTapeModel or op1r (GPL-3.0) → read-only, or the whole plugin becomes
  GPL-3.0. For a personal instrument that is genuinely fine; it only bites on distribution.
- **JUCE 8** is dual-licensed: AGPLv3, or a commercial licence. For a plugin you build and use
  yourself, either the free Personal tier or AGPLv3 works. Choose before writing the plugin shell,
  because it decides whether your plugin source can stay private.
- **Do not** put "OP-1", "Teenage Engineering", or their logotypes in the UI, the plugin name, or
  the preset names. Describe compatibility in prose if you like ("modelled after a famous portable
  tape machine") — that is nominative use, not branding.

## 5. What about the four tape styles and the tricks?

- **Names** like "Studio 4-track", "Porta", "Disk Mini" are product features / descriptive terms;
  using similar vocabulary in a private build is not a practical concern, but shipping with the same
  names would be cheeky. In the prototype I kept them because it is your reference point; before any
  distribution I would rename them (e.g. STUDIO / VINTAGE / CASSETTE / MINIDISC — all generic).
- **Speeds** (15, 7½, 3¾ ips), bias, NAB/IEC curves are engineering standards, not IP.
- **The trick set** (loop in/out, break, reverse, chop, memo) is generic tape/looper vocabulary.
- **The look** must be yours. The reference boards you sent are a vernacular (Y2K chrome, lab
  silkscreen, barcode labels), not a TE design — that is exactly why the prototype can be both
  obviously inspired and obviously not a copy.

## 6. If you want maximum comfort later

1. Write down the behavioural spec first (`docs/03-engine.md`), then code against the spec without
   looking at TE-derived material. That is the textbook clean-room split, and it is easy here
   because we never had the source in the first place.
2. Keep a `NOTICES.md` listing every third-party file and its licence.
3. Keep firmware images and any unpacked copies out of this Git repo (they are hundreds of MB and
   they are TE's).
