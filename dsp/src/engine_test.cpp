// Tape-Top — engine assertions
//
// These are the checks that caught two real bugs during development (a biquad
// using input history in the denominator, and a transport advancing in samples
// but reporting seconds). Keep them: an audio model that is not measured drifts
// into "it sounds warm" and nobody can tell what changed.
//
// Build & run: make -C dsp test

#include "tt/TapeMachine.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int gFailures = 0;
int gChecks = 0;

void check(bool condition, const std::string& what, const std::string& detail = {})
{
    ++gChecks;
    if (condition)
    {
        std::printf("  PASS  %s\n", what.c_str());
    }
    else
    {
        ++gFailures;
        std::printf("  FAIL  %s  %s\n", what.c_str(), detail.c_str());
    }
}

// Level of a sine after `fn` has processed it for `seconds`, in dB relative to
// the input (i.e. 0 dB means transparent).
template <typename Fn>
double toneGainDb(Fn&& fn, double freq, double amplitude, double seconds = 0.6)
{
    constexpr double sr = 44100.0;
    double inEnergy = 0, outEnergy = 0;
    const auto n = static_cast<int>(sr * seconds);
    for (int i = 0; i < n; ++i)
    {
        const float x = static_cast<float>(amplitude * std::sin(2.0 * tt::kPi * freq * i / sr));
        const float y = fn(x);
        if (i > n / 2)
        {
            inEnergy += x * x;
            outEnergy += double(y) * y;
        }
    }
    return 10.0 * std::log10(outEnergy / std::max(inEnergy, 1e-20));
}

// RMS level of the deck over `seconds`, in dBFS.
template <typename Fn>
double deckLevelDb(Fn&& fn, double seconds)
{
    constexpr double sr = 44100.0;
    const auto n = static_cast<int>(sr * seconds);
    double energy = 0;
    for (int i = 0; i < n; ++i)
    {
        float l = 0, r = 0;
        fn(l, r);
        energy += double(l) * l + double(r) * r;
    }
    return 10.0 * std::log10(energy / (2.0 * n) + 1e-20);
}

std::vector<float> sine(double freq, double amplitude, double seconds)
{
    constexpr double sr = 44100.0;
    std::vector<float> v(static_cast<std::size_t>(sr * seconds));
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<float>(amplitude * std::sin(2.0 * tt::kPi * freq * i / sr));
    return v;
}

} // namespace

int main()
{
    constexpr double sr = 44100.0;
    std::printf("Tape-Top engine assertions\n\n");

    // 1 — the record/reproduce EQ pair must cancel in the linear region.
    std::printf("record/reproduce EQ cancels (linear region)\n");
    {
        for (double freq : { 100.0, 500.0, 2000.0 })
        {
            tt::TapeEq eq;
            eq.prepare(sr, tt::specFor(tt::Style::Studio));
            const double g = toneGainDb([&eq](float x) { return eq.reproduce(eq.record(x)); },
                                        freq, 0.02);
            check(std::fabs(g) < 1.5, "flat at " + std::to_string(int(freq)) + " Hz",
                  std::to_string(g) + " dB");
        }
    }

    // 2 — calibration: nominal level in equals nominal level out, every stock.
    for (auto style : { tt::Style::Studio, tt::Style::Vintage, tt::Style::Porta, tt::Style::DiscMini })
    {
        const auto spec = tt::specFor(style);
        tt::TapeEq eq;
        eq.prepare(sr, spec);
        tt::RecordHead head;
        head.prepare(sr, spec);
        const double g = toneGainDb([&](float x) { return eq.reproduce(head.process(eq.record(x))); },
                                    1000.0, tt::dbToGain(-20.0));
        std::printf("calibration %s\n", spec.name);
        check(std::fabs(g) < 2.0, "unity at -20 dBFS", std::to_string(g) + " dB");

        // 3 — hot into tape must come out compressed, not louder.
        const double hot = toneGainDb([&](float x) { return eq.reproduce(head.process(eq.record(x))); },
                                      1000.0, 1.0);
        check(hot < -0.5, "compresses at 0 dBFS", std::to_string(hot) + " dB");
    }

    // 4 — halving the tape speed must lose top end. This is the one property that
    //     distinguishes a tape model from a "warmth" knob.
    std::printf("HF loss follows tape speed\n");
    {
        const auto gainAt = [](tt::Style style, double freq) {
            const auto spec = tt::specFor(style);
            tt::TapeEq eq;
            eq.prepare(sr, spec);
            return toneGainDb([&eq](float x) { return eq.reproduce(eq.record(x)); }, freq, 0.02);
        };
        const double studio = gainAt(tt::Style::Studio, 12000.0);
        const double vintage = gainAt(tt::Style::Vintage, 12000.0);
        const double porta = gainAt(tt::Style::Porta, 12000.0);
        check(studio > vintage && vintage > porta, "15 > 7.5 > 3.75 ips at 12 kHz",
              std::to_string(studio) + " / " + std::to_string(vintage) + " / " + std::to_string(porta));
        check(porta < studio - 6.0, "porta is at least 6 dB darker than studio at 12 kHz",
              std::to_string(porta - studio) + " dB");
    }

    // 5 — a stopped tape is silent, not muted. The head output must fall to the
    //     noise floor, and it must do so from the flux rate, not from a fader.
    std::printf("transport\n");
    {
        tt::Deck deck;
        deck.prepare(sr, tt::Style::Vintage, 4.0);
        const auto tone = sine(440.0, 0.5, 3.0);
        for (int t = 0; t < tt::Deck::kTracks; ++t)
            deck.track(t).setDropouts(false);
        deck.track(0).record(0.0, tone.data(), tone.data(), tone.size());
        deck.transport().seek(0.0);
        deck.transport().setTargetSpeed(1.0);
        const double playing = deckLevelDb([&](float& l, float& r) { deck.processSample(l, r); }, 0.5);
        deck.transport().setTargetSpeed(0.0);
        deckLevelDb([&](float& l, float& r) { deck.processSample(l, r); }, 0.35); // let it spin down
        const double stopped = deckLevelDb([&](float& l, float& r) { deck.processSample(l, r); }, 0.25);
        check(playing - stopped > 40.0, "silent after stop, no fade",
              std::to_string(playing - stopped) + " dB drop");
    }
    {
        tt::Deck deck;
        deck.prepare(sr, tt::Style::Studio, 2.0);
        deck.transport().seek(0.0);
        deck.transport().setLoop(1.0, 1.2, true);
        deck.transport().setTargetSpeed(1.0);
        for (int i = 0; i < int(sr * 1.5); ++i) { float l = 0, r = 0; deck.processSample(l, r); }
        const double pos = deck.transport().position();
        check(pos >= 1.0 && pos < 1.25, "loop wraps inside its region", std::to_string(pos));
    }

    // 6 — noise floor: eight silent tracks with hiss must stay well below music.
    std::printf("noise floor\n");
    {
        tt::Deck deck;
        deck.prepare(sr, tt::Style::Porta, 2.0);
        for (int t = 0; t < tt::Deck::kTracks; ++t) deck.track(t).setDropouts(false);
        deck.transport().seek(0.0);
        deck.transport().setTargetSpeed(1.0);
        const double floor = deckLevelDb([&](float& l, float& r) { deck.processSample(l, r); }, 1.0);
        check(floor < -30.0 && floor > -80.0, "audible hiss, not a wall of noise",
              std::to_string(floor) + " dBFS");
    }

    std::printf("\n%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
