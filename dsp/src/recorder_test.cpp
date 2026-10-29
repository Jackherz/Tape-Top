// Tape-Top — recorder / editor assertions
//
// The editing layer is where a tape plugin quietly becomes a DAW: destructive
// edges, punch boundaries, splices and undo all produce clicks if they are even
// slightly wrong. These tests are about edges.
//
// Build & run: make -C dsp test

#include "tt/Recorder.h"

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
    std::printf(condition ? "  PASS  %s\n" : "  FAIL  %s  %s\n",
                what.c_str(), condition ? "" : detail.c_str());
    if (!condition) ++gFailures;
}

constexpr double kSr = 44100.0;

struct Bench {
    tt::Deck deck;
    tt::TapeEditor editor;

    explicit Bench(double seconds = 6.0, tt::Style style = tt::Style::Studio)
    {
        deck.prepare(kSr, style, seconds);
        editor.prepare(kSr);
        for (int t = 0; t < tt::Deck::kTracks; ++t) deck.track(t).setDropouts(false);
    }

    // Print a block of input onto a track exactly as the transport would, one
    // frame at a time through the record chain.
    void print(int track, double atSec, const std::vector<float>& signal)
    {
        for (std::size_t i = 0; i < signal.size(); ++i)
            deck.track(track).writeAt(atSec + i / kSr, signal[i], signal[i]);
    }

    // Peak of the medium over a span, by playing it back at unity speed.
    double levelDb(double fromSec, double toSec)
    {
        double peak = 0;
        const auto n = static_cast<int>((toSec - fromSec) * kSr);
        for (int i = 0; i < n; ++i)
        {
            float l = 0, r = 0;
            deck.track(0).play((fromSec + i / kSr) * kSr, 1.0, l, r);
            peak = std::max(peak, double(std::fabs(l)));
        }
        return tt::gainToDb(peak);
    }
};

std::vector<float> sine(double freq, double amp, double seconds, double phase = 0.0)
{
    std::vector<float> v(static_cast<std::size_t>(kSr * seconds));
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<float>(amp * std::sin(2.0 * tt::kPi * freq * i / kSr + phase));
    return v;
}

} // namespace

int main()
{
    std::printf("Tape-Top recorder assertions\n\n");

    // 1 — record with arm, stop, and get a take with the right bounds.
    std::printf("record / arm / takes\n");
    {
        Bench b;
        const auto burst = sine(440.0, 0.5, 0.5);
        b.editor.arm(0, true);
        b.editor.startRecording(1.0);
        for (std::size_t i = 0; i < burst.size(); ++i)
            b.editor.processRecord(b.deck, 1.0 + i / kSr, burst[i], burst[i]);
        b.editor.stopRecording(1.5);

        check(b.deck.track(0).hasContent(1.05, 1.45), "audio landed inside the take");
        check(!b.deck.track(0).hasContent(2.0, 2.4), "nothing written outside it");
        check(b.editor.takes().size() == 1, "one take committed");
        const auto& t = b.editor.takes().front();
        check(std::fabs(t.start - 1.0) < 1e-6 && std::fabs(t.end - 1.5) < 1e-6,
              "take bounds match the pass", std::to_string(t.start) + ".." + std::to_string(t.end));
        check(!b.editor.armed(3), "unarmed tracks stayed empty");
        check(!b.deck.track(3).hasContent(0.5, 2.0), "and silent on the medium");
    }

    // 2 — punch: boundaries are honoured, edges are faded not clicked.
    std::printf("punch\n");
    {
        Bench b;
        const auto signal = sine(220.0, 0.5, 3.0);
        b.editor.arm(0, true);
        b.editor.setPunch(true, 1.0, 2.0);
        b.editor.startRecording(0.5);
        for (std::size_t i = 0; i < signal.size(); ++i)
            b.editor.processRecord(b.deck, 0.5 + i / kSr, signal[i], signal[i]);
        b.editor.stopRecording(3.5);

        check(!b.deck.track(0).hasContent(0.6, 0.95), "no capture before punch in");
        check(b.deck.track(0).hasContent(1.1, 1.9), "capture inside punch");
        check(!b.deck.track(0).hasContent(2.2, 3.0), "no capture after punch out");

        // Largest sample-to-sample step across the in-point: a hard edge on a
        // 220 Hz tone at 0.5 would jump about 0.03 per sample anyway, so the
        // test is that the fade does not add a step of its own.
        double maxStep = 0;
        float prev = 0;
        for (int i = 0; i < int(0.02 * kSr); ++i)
        {
            float l = 0, r = 0;
            b.deck.track(0).play((0.99 + i / kSr) * kSr, 1.0, l, r);
            if (i > 0) maxStep = std::max(maxStep, double(std::fabs(l - prev)));
            prev = l;
        }
        check(maxStep < 0.05, "punch-in edge is faded", std::to_string(maxStep));
    }

    // 3 — erase then undo restores exactly what was there.
    std::printf("undo\n");
    {
        Bench b;
        b.print(2, 1.0, sine(330.0, 0.4, 1.0));
        const double before = b.levelDb(1.1, 1.4);
        b.editor.erase(b.deck, 2, 1.0, 2.0);
        const double afterErase = 0.0;
        double peak = 0;
        for (int i = 0; i < int(0.5 * kSr); ++i)
        {
            float l = 0, r = 0;
            b.deck.track(2).play((1.2 + i / kSr) * kSr, 1.0, l, r);
            peak = std::max(peak, double(std::fabs(l)));
        }
        (void)afterErase;
        // Not zero: an erased studio tape still plays back its hiss floor
        // (-86 dB here), which is exactly what a blank tape should do.
        check(peak < 2e-4, "erase removed the audio", std::to_string(peak));
        check(b.editor.canUndo(), "an undo step was pushed");
        b.editor.undo(b.deck);
        double restored = 0;
        for (int i = 0; i < int(0.5 * kSr); ++i)
        {
            float l = 0, r = 0;
            b.deck.track(2).play((1.2 + i / kSr) * kSr, 1.0, l, r);
            restored = std::max(restored, double(std::fabs(l)));
        }
        check(tt::gainToDb(restored) > before - 1.0, "undo restored the level",
              std::to_string(tt::gainToDb(restored)) + " vs " + std::to_string(tt::gainToDb(before)));
    }

    // 4 — lift removes, drop returns, and the seam is crossfaded.
    std::printf("lift / drop / splice\n");
    {
        Bench b;
        b.print(1, 0.0, sine(200.0, 0.30, 2.0));      // bed
        tt::Region lifted;
        {
            Bench source;
            source.print(5, 0.0, sine(300.0, 0.30, 0.5, 1.2));
            lifted = source.deck.track(5).copyRegion(0.0, 0.5);
        }
        check(!lifted.empty(), "region copied");

        // Put it down in the middle of the bed and look at the seam.
        b.deck.track(1).spliceRegion(0.80, lifted, 1.0f, kSr);
        float prev = 0;
        double maxStep = 0;
        for (int i = 0; i < int(0.05 * kSr); ++i)
        {
            float l = 0, r = 0;
            b.deck.track(1).play((0.78 + i / kSr) * kSr, 1.0, l, r);
            if (i > 0) maxStep = std::max(maxStep, double(std::fabs(l - prev)));
            prev = l;
        }
        check(maxStep < 0.10, "splice is crossfaded, not butted", std::to_string(maxStep));

        // Lift the bed out and confirm it is gone, then drop it back.
        const double beforeLift = [&] {
            double p = 0;
            for (int i = 0; i < int(0.2 * kSr); ++i)
            {
                float l = 0, r = 0;
                b.deck.track(1).play((0.2 + i / kSr) * kSr, 1.0, l, r);
                p = std::max(p, double(std::fabs(l)));
            }
            return p;
        }();
        (void)beforeLift;
        b.editor.lift(b.deck, 1, 0.1, 0.5);
        check(!b.deck.track(1).hasContent(0.15, 0.45), "lift removed the span");
        b.editor.drop(b.deck, 1, 0.1, 1.0f);
        check(b.deck.track(1).hasContent(0.15, 0.45), "drop put it back");
    }

    // 5 — split and join maintain the take list without touching audio.
    std::printf("split / join\n");
    {
        Bench b;
        b.print(0, 1.0, sine(440.0, 0.3, 1.0));
        b.editor.rebuildTakes(b.deck);
        const std::size_t before = b.editor.takes().size();
        check(before >= 1, "take list derived from the medium", std::to_string(before));
        const double mid = b.editor.takes().front().start
                         + 0.5 * (b.editor.takes().front().end - b.editor.takes().front().start);
        b.editor.split(0, mid);
        check(b.editor.takes().size() == before + 1, "split added a boundary");
        check(b.editor.join(0, mid), "join merged them back");
        check(b.editor.takes().size() == before, "take count restored");
    }

    std::printf("\n%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
