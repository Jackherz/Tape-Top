// Tape-Top — offline demo renderer
//
// Renders a small eight-track sketch, prints it through the tape engine in each
// of the four tape styles, and writes the results as WAV so the model can be
// listened to before a single plugin UI pixel exists. Also renders a "tricks"
// pass that exercises the transport: tape stop, reverse, chop, and a brake.
//
// Build:  make -C dsp          Run: dsp/out/tape_demo
// Out:    dsp/out/*.wav  (44.1 kHz, 16-bit, stereo)

#include "tt/TapeMachine.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr double kBpm = 96.0;
constexpr double kBeats = 16.0;                     // 4 bars of 4/4
constexpr double kLenSec = kBeats * 60.0 / kBpm;    // 10.0 s
constexpr double kTailSec = 1.2;

// ---------------------------------------------------------------------------
// Tiny synth voice, just enough to make musical material to print to tape.
// ---------------------------------------------------------------------------
struct Voice {
    double phase = 0, phase2 = 0;
    double freq = 440, freq2 = 440;
    double attack = 0.005, decay = 0.2, sustain = 0.0, release = 0.2;
    double gate = 0, env = 0;
    double gain = 1;
    bool active = false;

    void trigger(double f, double g, double len = 0)
    {
        freq = f;
        freq2 = f * 1.005;
        gain = g;
        phase = phase2 = 0;
        gate = len > 0 ? len : 1e9;
        active = true;
    }

    double tick()
    {
        if (!active) return 0.0;
        // Naive saw pair; the tape's head losses do the filtering anyway.
        const double s = (std::fmod(phase, 1.0) * 2.0 - 1.0) * 0.5
                       + (std::fmod(phase2, 1.0) * 2.0 - 1.0) * 0.5;
        const double dt = 1.0 / kSampleRate;
        phase += freq * dt;
        phase2 += freq2 * dt;

        if (gate > 0) { env += (1.0 - env) / (attack * kSampleRate); gate -= dt; }
        else { env *= 1.0 - 1.0 / (release * kSampleRate); if (env < 1e-4) { env = 0; active = false; } }
        return s * env * gain;
    }
};

struct StereoBuffer {
    std::vector<float> l, r;
    explicit StereoBuffer(std::size_t n) : l(n, 0.0f), r(n, 0.0f) {}
    std::size_t size() const { return l.size(); }
};

// MIDI note -> Hz
double noteHz(double n) { return 440.0 * std::pow(2.0, (n - 69.0) / 12.0); }

inline void addStereo(StereoBuffer& buf, std::size_t i, float v, double pan)
{
    if (i >= buf.size()) return;
    const float gl = static_cast<float>(std::sqrt(0.5 * (1.0 - pan)));
    const float gr = static_cast<float>(std::sqrt(0.5 * (1.0 + pan)));
    buf.l[i] += v * gl;
    buf.r[i] += v * gr;
}

double unitNoise(std::uint32_t& state)
{
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return static_cast<double>(static_cast<std::int32_t>(state)) * 4.6566129e-10;
}

// ---------------------------------------------------------------------------
// The sketch. Am | F | C | G, one bar each, eight tracks apart.
// ---------------------------------------------------------------------------
std::vector<StereoBuffer> makeSketch()
{
    const auto n = static_cast<std::size_t>((kLenSec + kTailSec) * kSampleRate);
    std::vector<StereoBuffer> tracks;
    tracks.reserve(tt::Deck::kTracks);
    for (int i = 0; i < tt::Deck::kTracks; ++i) tracks.emplace_back(n);

    const double beat = 60.0 / kBpm;
    const double bar = beat * 4.0;
    const double sixteenth = beat / 4.0;
    const int roots[4] = { 57, 53, 60, 55 };        // A3, F3, C4, G3
    const int chords[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 60, 64, 67 }, { 55, 59, 62 } };
    std::uint32_t rng = 0xC0FFEEu;

    // --- t1 kick -----------------------------------------------------------
    for (int b = 0; b < 16; ++b)
    {
        const std::size_t start = static_cast<std::size_t>((b * beat) * kSampleRate);
        const std::size_t len = static_cast<std::size_t>(0.28 * kSampleRate);
        for (std::size_t i = 0; i < len && start + i < n; ++i)
        {
            const double t = i / kSampleRate;
            const double f = 48.0 + 90.0 * std::exp(-t * 28.0);
            const double env = std::exp(-t * 9.0);
            const double click = (i < 60 ? 0.5 * (1.0 - i / 60.0) : 0.0);
            const double v = std::sin(2.0 * tt::kPi * f * t) * env + click * 0.3;
            addStereo(tracks[0], start + i, static_cast<float>(v * 0.9), 0.0);
        }
    }

    // --- t2 snare ----------------------------------------------------------
    for (int b = 0; b < 16; ++b)
    {
        if (b % 4 != 1 && b % 4 != 3) continue;
        const std::size_t start = static_cast<std::size_t>((b * beat) * kSampleRate);
        const std::size_t len = static_cast<std::size_t>(0.22 * kSampleRate);
        double lp = 0;
        for (std::size_t i = 0; i < len && start + i < n; ++i)
        {
            const double t = i / kSampleRate;
            lp += 0.35 * (unitNoise(rng) - lp);
            const double body = std::sin(2.0 * tt::kPi * 185.0 * t) * std::exp(-t * 22.0) * 0.5;
            const double v = (lp * std::exp(-t * 26.0) * 0.9 + body) * 0.8;
            addStereo(tracks[1], start + i, static_cast<float>(v), 0.05);
        }
    }

    // --- t3 bass -----------------------------------------------------------
    for (int barIdx = 0; barIdx < 4; ++barIdx)
    {
        for (int step = 0; step < 8; ++step)
        {
            const double t0 = barIdx * bar + step * (beat * 0.5);
            const std::size_t start = static_cast<std::size_t>(t0 * kSampleRate);
            const std::size_t len = static_cast<std::size_t>(beat * 0.42 * kSampleRate);
            const double f = noteHz(roots[barIdx] - 12 + (step == 7 ? 7 : 0));
            Voice v; v.attack = 0.004; v.release = 0.06;
            v.trigger(f, 0.5, beat * 0.4);
            double lp = 0;
            for (std::size_t i = 0; i < len && start + i < n; ++i)
            {
                const double raw = v.tick();
                lp += 0.12 * (raw - lp);
                addStereo(tracks[2], start + i, static_cast<float>(lp * 0.9), 0.0);
            }
        }
    }

    // --- t4 / t5 chords (two layers, panned, the second up an octave) ------
    for (int barIdx = 0; barIdx < 4; ++barIdx)
    {
        const double t0 = barIdx * bar;
        const std::size_t start = static_cast<std::size_t>(t0 * kSampleRate);
        const std::size_t len = static_cast<std::size_t>(bar * 0.95 * kSampleRate);
        Voice low[3], high[3];
        for (int i = 0; i < 3; ++i)
        {
            low[i].attack = 0.03; low[i].release = 0.4;
            low[i].trigger(noteHz(chords[barIdx][i]), 0.16, bar * 0.9);
            high[i].attack = 0.01; high[i].release = 0.2;
            high[i].trigger(noteHz(chords[barIdx][i] + 12), 0.09, bar * 0.6);
        }
        for (std::size_t i = 0; i < len && start + i < n; ++i)
        {
            double a = 0, b = 0;
            for (int k = 0; k < 3; ++k) { a += low[k].tick(); b += high[k].tick(); }
            addStereo(tracks[3], start + i, static_cast<float>(a * 0.30), -0.55);
            addStereo(tracks[4], start + i, static_cast<float>(b * 0.26), 0.55);
        }
    }

    // --- t6 arp ------------------------------------------------------------
    for (int b = 0; b < 64; ++b)
    {
        const int barIdx = b / 16;
        const int step = b % 16;
        const int chordNote = chords[barIdx][step % 3];
        const double t0 = b * sixteenth;
        const std::size_t start = static_cast<std::size_t>(t0 * kSampleRate);
        const std::size_t len = static_cast<std::size_t>(sixteenth * 0.9 * kSampleRate);
        Voice v; v.attack = 0.002; v.release = 0.02;
        v.trigger(noteHz(chordNote + 24), 0.14, sixteenth * 0.5);
        for (std::size_t i = 0; i < len && start + i < n; ++i)
            addStereo(tracks[5], start + i, static_cast<float>(v.tick()), 0.22);
    }

    // --- t7 pad / noise sweep ---------------------------------------------
    for (int barIdx = 0; barIdx < 4; ++barIdx)
    {
        const std::size_t start = static_cast<std::size_t>((barIdx * bar) * kSampleRate);
        const std::size_t len = static_cast<std::size_t>(bar * kSampleRate);
        double lp = 0;
        Voice v; v.attack = 0.5; v.release = 0.6;
        v.trigger(noteHz(roots[barIdx] - 24), 0.10, bar);
        for (std::size_t i = 0; i < len && start + i < n; ++i)
        {
            const double t = i / kSampleRate;
            const double sweep = 0.02 + 0.5 * (t / bar);
            lp += sweep * (unitNoise(rng) - lp);
            const double v2 = v.tick();
            addStereo(tracks[6], start + i, static_cast<float>((lp * 0.35 + v2 * 0.6)), -0.2);
        }
    }

    // --- t8 hats -----------------------------------------------------------
    for (int b = 0; b < 32; ++b)
    {
        const double t0 = b * (beat * 0.5);
        const std::size_t start = static_cast<std::size_t>(t0 * kSampleRate);
        const std::size_t len = static_cast<std::size_t>(0.06 * kSampleRate);
        double hp = 0, prev = 0;
        const double amp = (b % 2 == 0) ? 0.22 : 0.12;
        for (std::size_t i = 0; i < len && start + i < n; ++i)
        {
            const double t = i / kSampleRate;
            const double w = unitNoise(rng);
            hp = 0.7 * (hp + w - prev);   // crude high-pass
            prev = w;
            addStereo(tracks[7], start + i, static_cast<float>(hp * amp * std::exp(-t * 70.0)), 0.35);
        }
    }

    return tracks;
}

// ---------------------------------------------------------------------------
// WAV out (16-bit PCM, stereo)
// ---------------------------------------------------------------------------
void writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    const auto n = l.size();
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(n * 2 * 2);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::printf("cannot open %s\n", path.c_str()); return; }

    const auto u32 = [f](std::uint32_t v) { std::fwrite(&v, 4, 1, f); };
    const auto u16 = [f](std::uint16_t v) { std::fwrite(&v, 2, 1, f); };

    std::fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(2);
    u32(static_cast<std::uint32_t>(kSampleRate)); u32(static_cast<std::uint32_t>(kSampleRate) * 4);
    u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(dataBytes);

    for (std::size_t i = 0; i < n; ++i)
    {
        const auto toInt = [](float x) {
            x = std::clamp(x, -1.0f, 1.0f);
            return static_cast<std::int16_t>(std::lrint(x * 32767.0f));
        };
        const std::int16_t sl = toInt(l[i]);
        const std::int16_t sr = toInt(r[i]);
        std::fwrite(&sl, 2, 1, f);
        std::fwrite(&sr, 2, 1, f);
    }
    std::fclose(f);
    std::printf("wrote %-34s %5.1f s\n", path.c_str(), n / kSampleRate);
}

struct Stats { double peak = 0, rms = 0; };
Stats measure(const std::vector<float>& l, const std::vector<float>& r)
{
    Stats s;
    double sum = 0;
    for (std::size_t i = 0; i < l.size(); ++i)
    {
        s.peak = std::max(s.peak, static_cast<double>(std::max(std::fabs(l[i]), std::fabs(r[i]))));
        sum += l[i] * l[i] + r[i] * r[i];
    }
    s.rms = std::sqrt(sum / (2.0 * l.size()));
    return s;
}

// ---------------------------------------------------------------------------
// Render a full pass of the deck through a buffer.
// ---------------------------------------------------------------------------
void renderPass(tt::Deck& deck, std::vector<float>& outL, std::vector<float>& outR,
                double seconds)
{
    const auto n = static_cast<std::size_t>(seconds * kSampleRate);
    outL.assign(n, 0.0f);
    outR.assign(n, 0.0f);
    deck.transport().seek(0.0);
    for (std::size_t i = 0; i < n; ++i)
    {
        float l = 0, r = 0;
        deck.processSample(l, r);
        outL[i] = l;
        outR[i] = r;
    }
}

} // namespace

int main()
{
    std::printf("Tape-Top tape engine demo\n");
    std::printf("  %.0f Hz, %.0f BPM, %.2f s sketch, 8 tracks\n\n", kSampleRate, kBpm, kLenSec);

    const auto sketch = makeSketch();
    const double totalSec = kLenSec + kTailSec;

    // A plain digital sum, for A/B.
    {
        std::vector<float> l(static_cast<std::size_t>(totalSec * kSampleRate), 0.0f), r = l;
        for (std::size_t t = 0; t < sketch.size(); ++t)
            for (std::size_t i = 0; i < l.size(); ++i)
            {
                l[i] += sketch[t].l[i] * 0.6f;
                r[i] += sketch[t].r[i] * 0.6f;
            }
        const auto s = measure(l, r);
        std::printf("dry   peak %.2f dBFS  rms %.2f dBFS\n",
                    tt::gainToDb(s.peak), tt::gainToDb(s.rms));
        writeWav("out/00-dry-digital.wav", l, r);
    }

    struct StyleJob { tt::Style style; const char* file; };
    const StyleJob jobs[] = {
        { tt::Style::Studio,   "out/01-studio-15ips.wav" },
        { tt::Style::Vintage,  "out/02-vintage-7.5ips.wav" },
        { tt::Style::Porta,    "out/03-porta-3.75ips.wav" },
        { tt::Style::DiscMini, "out/04-diskmini.wav" },
    };

    for (const auto& job : jobs)
    {
        tt::Deck deck;
        deck.prepare(kSampleRate, job.style, totalSec + 1.0);
        for (int t = 0; t < tt::Deck::kTracks; ++t)
            deck.track(t).record(0.0, sketch[static_cast<std::size_t>(t)].l.data(),
                                 sketch[static_cast<std::size_t>(t)].r.data(),
                                 sketch[static_cast<std::size_t>(t)].size());

        std::vector<float> l, r;
        deck.transport().setTargetSpeed(1.0);
        renderPass(deck, l, r, totalSec);
        // Normalise to a common loudness so the comparison is about character.
        const double peak = std::max(1e-9, measure(l, r).peak);
        const float g = static_cast<float>(0.89 / peak);
        for (auto& v : l) v *= g;
        for (auto& v : r) v *= g;
        const auto s = measure(l, r);
        std::printf("%-12s peak %.2f dBFS  rms %.2f dBFS  (ips %.2f)\n",
                    deck.spec().name, tt::gainToDb(s.peak), tt::gainToDb(s.rms), deck.spec().ips);
        writeWav(job.file, l, r);
    }

    // -----------------------------------------------------------------------
    // Transport tricks: stop, spin back up, reverse, chop, brake.
    // -----------------------------------------------------------------------
    {
        tt::Deck deck;
        deck.prepare(kSampleRate, tt::Style::Porta, totalSec + 1.0);
        for (int t = 0; t < tt::Deck::kTracks; ++t)
            deck.track(t).record(0.0, sketch[static_cast<std::size_t>(t)].l.data(),
                                 sketch[static_cast<std::size_t>(t)].r.data(),
                                 sketch[static_cast<std::size_t>(t)].size());

        const auto n = static_cast<std::size_t>(11.0 * kSampleRate);
        std::vector<float> l(n, 0.0f), r(n, 0.0f);
        auto& tr = deck.transport();
        tr.seek(0.0);
        tr.setTargetSpeed(1.0);

        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = i / kSampleRate;
            if (t > 3.0 && t < 3.05) tr.setTargetSpeed(0.0);          // tape stop
            if (t > 3.9 && t < 3.95) tr.setTargetSpeed(1.0);          // spin up
            if (t > 4.6 && t < 4.65) tr.setDirection(-1.0);           // reverse
            if (t > 6.1 && t < 6.15) tr.setDirection(1.0);            // forward
            if (t > 6.6 && t < 6.65) tr.setLoop(1.0, 1.25, true);     // chop (tempo locked repeat)
            if (t > 8.0 && t < 8.05) tr.setLoop(0, 0, false);
            if (t > 9.4 && t < 9.45) tr.setTargetSpeed(0.0);          // brake

            float sl = 0, sr = 0;
            deck.processSample(sl, sr);
            l[i] = sl;
            r[i] = sr;
        }
        const double peak = std::max(1e-9, measure(l, r).peak);
        const float g = static_cast<float>(0.89 / peak);
        for (auto& v : l) v *= g;
        for (auto& v : r) v *= g;
        writeWav("out/05-tricks-stop-reverse-chop.wav", l, r);
    }

    std::printf("\ndone. listen in ui/ (the prototype plays these files).\n");
    return 0;
}
