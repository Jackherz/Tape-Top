// Tape-Top — tape machine model
//
// The organising idea, taken from how the OP-1 presents tape: a *deck* is a
// shared transport plus eight independent strips over one piece of medium.
// Audio is imprinted by a record path (bias, hysteresis, pre-emphasis), stored
// in the medium, and recovered by a reproduce path (gap loss, head bump,
// playback EQ) at a position the transport dictates. Nothing is a "lofi
// filter" bolted onto the input: every colour is a consequence of speed,
// wavelength or wear, so changing tape speed changes the tone for the right
// reason.
//
// References: Chowdhury, "Real-time Physical Modelling for Analog Tape
// Machines", DAFx-19 (hysteresis shape, bias, head bump, deadzone);
// Jiles & Atherton (the magnetisation we approximate); Krasnosel'skii play
// operator (the rate-independent memory element used here instead of the full
// J-A ODE, because it is unconditionally stable at any drive and any rate).
#pragma once

#include "Dsp.h"

#include <array>
#include <string>
#include <vector>

namespace tt {

// ---------------------------------------------------------------------------
// Tape stock / machine character. The four styles mirror the four tape styles
// on the OP-1 field, but they are described physically (inches per second,
// gap length, bias, noise floor) rather than as presets of a "colour" knob.
// ---------------------------------------------------------------------------
enum class Style { Studio, Vintage, Porta, DiscMini };
enum class Mode { Normal, Reverse, Break, Chop };

struct TapeSpec {
    const char* name;
    double ips;            // tape speed, inches per second
    double gapMicrons;     // reproduce head gap
    double bumpHz;         // head bump centre (scales with speed)
    double bumpDb;
    double hfLossDb;       // reproduce HF loss applied at 10 kHz
    double bias;           // record bias, 0..1 (0.5 = nominal)
    double driveDb;        // record drive into the hysteresis element
    double wowDepth;       // pitch deviation, fraction (0.003 = ~5 cents)
    double wowRateHz;
    double flutterDepth;
    double flutterRateHz;
    double hissDb;
    double compander;      // 0..1, dbx-ish companding (DiscMini)
    double trackWidthDb;   // crosstalk between neighbouring tracks
};

inline TapeSpec specFor(Style style)
{
    switch (style) {
    case Style::Studio:
        // 15 ips, wide track, well aligned: the cleanest thing the machine does.
        return { "STUDIO 4TRK", 15.0, 1.2, 42.0, 2.0, 0.5, 0.50, -2.0,
                 0.0004, 0.55, 0.00012, 9.5, -86.0, 0.0, -70.0 };
    case Style::Vintage:
        // 7.5 ips, the classic: warmth from the bump and the gap, a breath of wobble.
        return { "VINTAGE 4TRK", 7.5, 2.2, 62.0, 3.0, 3.0, 0.58, 1.5,
                 0.0012, 0.45, 0.0004, 7.0, -68.0, 0.0, -62.0 };
    case Style::Porta:
        // 3.75 ips cassette: narrow track, heavy bias, real hiss, real wow.
        return { "PORTA 4TRK", 3.75, 3.6, 92.0, 4.0, 7.0, 0.72, 5.0,
                 0.0035, 0.38, 0.0011, 6.0, -52.0, 0.0, -48.0 };
    case Style::DiscMini:
        // Magneto-optical: psychoacoustic companding and a hard 15 kHz wall,
        // which is where its odd upper-harmonic artefacts come from.
        return { "DISK MINI", 7.5, 2.6, 70.0, 1.0, 9.0, 0.55, 1.0,
                 0.0002, 0.9, 0.0, 0.0, -78.0, 0.85, -66.0 };
    }
    return specFor(Style::Studio);
}

// ---------------------------------------------------------------------------
// Record head: bias + rate-independent hysteresis + the asymmetry that makes
// tape generate even harmonics. Play-operator Preisach: the state is clamped
// between (input - width) and (input + width), which gives memory without an
// ODE to integrate and never blows up.
// ---------------------------------------------------------------------------
class RecordHead {
public:
    void prepare(double sampleRate, const TapeSpec& spec)
    {
        mSpec = spec;
        mBias = (spec.bias - 0.5) * 0.9;                 // 0.45..0.72 -> -0.05..+0.20
        mDrive = dbToGain(spec.driveDb);
        mBiasOffset = std::tanh(mBias * 2.0);

        // The AC bias is what makes tape linear: it averages the magnetisation
        // loop away for everything below the bias frequency. We do not simulate
        // a 100 kHz oscillator, so the bias is represented by how much of the
        // signal goes through the memory (the play operators) at all. Higher
        // bias -> more magnetic work -> a little more of the loop survives.
        mHystMix = static_cast<float>(std::clamp(0.10 + 0.055 * spec.driveDb, 0.04, 0.42));
        // Level trim: `drive` sets how hard the magnetisation curve is pushed, not
        // how loud the track comes back, so the comparison between stocks is
        // about character rather than about who was printed hotter.
        mMakeup = static_cast<float>(1.0 / mDrive);
        mWidths = { 0.0018f, 0.0055f, 0.014f, 0.034f };
        mWeights = { 0.50f, 0.28f, 0.16f, 0.06f };
        mState.fill(0.0f);

        mDcBlocker.prepare(sampleRate);
    }

    inline float process(float in)
    {
        const float x = static_cast<float>(in * mDrive) + static_cast<float>(mBias);

        float sum = 0.0f;
        for (std::size_t i = 0; i < mState.size(); ++i)
        {
            const float lo = x - mWidths[i];
            const float hi = x + mWidths[i];
            mState[i] = std::min(std::max(mState[i], lo), hi);
            sum += mWeights[i] * mState[i];
        }

        // Magnetisation curve: mostly the smooth saturation, with a weighted
        // amount of loop memory left in. Both halves of this matter -- the tanh
        // alone is a waveshaper, the loop alone is a gate.
        const float mixed = (1.0f - mHystMix) * x + mHystMix * sum;
        float y = std::tanh(mixed) - mBiasOffset * 0.5f;
        y = mDcBlocker.process(y);
        return y * mMakeup;
    }

private:
    TapeSpec mSpec {};
    float mBias { 0 }, mBiasOffset { 0 }, mHystMix { 0.1f };
    float mMakeup { 1.0f };
    double mDrive { 1 };
    std::array<float, 4> mWidths {}, mWeights {};
    std::array<float, 4> mState {};
    DcBlocker mDcBlocker;
};

// ---------------------------------------------------------------------------
// Record / reproduce equalisation. Wavelength is the physical quantity, so the
// corner frequencies are derived from speed: at half speed every wavelength
// doubles and every corner halves.
// ---------------------------------------------------------------------------
class TapeEq {
public:
    void prepare(double sampleRate, const TapeSpec& spec)
    {
        const double speedScale = spec.ips / 15.0;      // 1.0 at 15 ips
        // Record pre-emphasis: boost HF before hysteresis so that loud HF does
        // not saturate the tape, then undo it on reproduce.
        mRecEmphasis.set(Biquad::Type::HighShelf, sampleRate, 3200.0, 0.7, 9.0);
        // Reproduce: gap loss + spacing loss compose into a steep-ish shelf;
        // the model uses a shelf plus a one-pole so the slope is not a cliff.
        // Reproduce undoes the record pre-emphasis first, then adds the losses
        // the medium and head impose. Skipping the undo is how you get a
        // brighter-than-digital "tape" -- i.e. a mistake, not a sound.
        mDeEmphasis.set(Biquad::Type::HighShelf, sampleRate, 3200.0, 0.7, -9.0);
        const double hfCorner = 2600.0 * speedScale;
        mPlayShelf.set(Biquad::Type::HighShelf, sampleRate,
                       std::max(900.0, hfCorner * 3.2), 0.7, -spec.hfLossDb);
        mGapOnePole.setCutoff(sampleRate, 9000.0 * speedScale + 1800.0);
        // Head bump: low-frequency resonance from finite head length, sits at
        // a wavelength fixed by speed.
        mBump.set(Biquad::Type::Peak, sampleRate, spec.bumpHz, 1.1, spec.bumpDb);
        // Sub-bass loss: tape does not pass DC, and the reproduce transformer
        // rolls off below ~25 Hz.
        mRumble.set(Biquad::Type::HighPass, sampleRate, 22.0, 0.7);
        mDc.prepare(sampleRate, 9.0);
    }

    inline float record(float in) { return mRecEmphasis.process(in); }
    inline float reproduce(float in)
    {
        float y = mDeEmphasis.process(in);
        y = mPlayShelf.process(y);
        y = mGapOnePole.process(y);
        y = mBump.process(y);
        y = mRumble.process(y);
        y = mDc.process(y);
        return y;
    }

private:
    Biquad mRecEmphasis, mDeEmphasis, mPlayShelf, mBump, mRumble;
    OnePole mGapOnePole;
    DcBlocker mDc;
};

// ---------------------------------------------------------------------------
// dbx-style companding used by DiscMini (and by the Porta's noise reduction if
// we ever add it): 2:1 compression on record, 1:2 expansion on reproduce. The
// non-ideal tracking error is what makes it audible rather than transparent.
// ---------------------------------------------------------------------------
class Compander {
public:
    void prepare(double amount, double threshold = 0.06)
    {
        mAmount = amount;
        mThreshold = threshold;
    }
    inline float encode(float in)
    {
        if (mAmount <= 0.0f) return in;
        const float a = std::fabs(in);
        if (a <= mThreshold) return in;
        // Deliberate tracking error: the record half is 3% eager about where
        // the knee is, so the pair does not cancel exactly.
        return std::copysign(mThreshold * 0.97f + (a - mThreshold * 0.97f) * 0.5f, in);
    }
    inline float decode(float in)
    {
        if (mAmount <= 0.0f) return in;
        const float a = std::fabs(in);
        if (a <= mThreshold) return in;
        return std::copysign(mThreshold + (a - mThreshold) * 2.02f, in);
    }

private:
    float mAmount { 0 }, mThreshold { 0.06f };
};

// ---------------------------------------------------------------------------
// Transport: one playhead for all eight tracks, which is the reason a tape
// machine sounds like a machine. Wow is slow (reel eccentricity), flutter is
// fast (capstan), scrape is faster still (tape against the head).
// ---------------------------------------------------------------------------
class Transport {
public:
    void prepare(double sampleRate, const TapeSpec& spec, double tapeLengthSec)
    {
        mSampleRate = sampleRate;
        mLength = tapeLengthSec;
        mWowRate = spec.wowRateHz;
        mWowDepth = spec.wowDepth;
        mFlutterRate = spec.flutterRateHz;
        mFlutterDepth = spec.flutterDepth;
        mPosition = 0.0;
        mSpeed = 1.0;
        mTargetSpeed = 1.0;
        mWowPhase = 0.0;
        mFlutterPhase = 0.0;
        mScrapePhase = 0.0;
        mDirection = 1.0;
        mNoise = Noise(0x5EED1234u);
        mSmooth = 0.0;
    }

    // 0..1, where 0 is a dead stop. The lag makes Stop and Break feel
    // mechanical instead of like a mute.
    void setTargetSpeed(double speed) { mTargetSpeed = speed; }
    void setDirection(double dir) { mDirection = dir < 0 ? -1.0 : 1.0; }
    void setLoop(double inSec, double outSec, bool enabled)
    {
        mLoopIn = inSec;
        mLoopOut = outSec;
        mLoop = enabled && outSec > inSec;
    }
    void seek(double seconds) { mPosition = seconds; }

    double position() const { return mPosition; }
    double speed() const { return mSpeed * mDirection; }
    bool looping() const { return mLoop; }

    // Returns the fractional medium position for this sample and advances.
    double advance()
    {
        const double coeff = 1.0 - std::exp(-1.0 / (0.045 * mSampleRate)); // ~45 ms spin-up
        mSpeed += (mTargetSpeed - mSpeed) * coeff;

        double w = 1.0;
        if (mWowDepth > 0)
        {
            mWowPhase += 2.0 * kPi * mWowRate / mSampleRate;
            w += mWowDepth * std::sin(mWowPhase)
               + mWowDepth * 0.35 * std::sin(mWowPhase * 2.03 + 1.1);
        }
        if (mFlutterDepth > 0)
        {
            mFlutterPhase += 2.0 * kPi * mFlutterRate / mSampleRate;
            mScrapePhase += 2.0 * kPi * 23.7 / mSampleRate;
            w += mFlutterDepth * std::sin(mFlutterPhase)
               + mFlutterDepth * 0.22 * std::sin(mScrapePhase)
               + mFlutterDepth * 0.05 * mNoise.white();
        }

        // mPosition is in seconds; one sample advances it by speed / rate.
        const double pos = mPosition;
        mPosition += (mSpeed * w * mDirection) / mSampleRate;

        if (mPosition >= mLength) mPosition = std::fmod(mPosition, mLength);
        if (mPosition < 0) mPosition += mLength;

        if (mLoop && mDirection > 0 && mPosition > mLoopOut) mPosition = mLoopIn;
        if (mLoop && mDirection < 0 && mPosition < mLoopIn) mPosition = mLoopOut;

        return pos * mSampleRate;
    }

    void setWow(double depth, double rate) { mWowDepth = depth; mWowRate = rate; }
    void setFlutter(double depth, double rate) { mFlutterDepth = depth; mFlutterRate = rate; }

private:
    double mSampleRate { 44100 };
    double mLength { 20.0 };
    double mPosition { 0 }, mSpeed { 1 }, mTargetSpeed { 1 }, mDirection { 1 };
    double mWowRate { 0.5 }, mWowDepth { 0 }, mFlutterRate { 7 }, mFlutterDepth { 0 };
    double mWowPhase { 0 }, mFlutterPhase { 0 }, mScrapePhase { 0 }, mSmooth { 0 };
    double mLoopIn { 0 }, mLoopOut { 0 };
    bool mLoop { false };
    Noise mNoise;
};

struct Take {
    int track { 0 };
    double start { 0 };
    double end { 0 };
};

// A span of medium, used for lift/drop, splice and undo snapshots.
struct Region {
    double start { 0 };
    double end { 0 };
    std::vector<float> left;
    std::vector<float> right;
    bool empty() const { return left.empty(); }
    std::size_t frames() const { return left.size(); }
};

// ---------------------------------------------------------------------------
// One track: medium + record head + reproduce chain + its own noise floor,
// dropouts and azimuth error.
// ---------------------------------------------------------------------------
class Track {
public:
    void prepare(double sampleRate, const TapeSpec& spec, double tapeLengthSec,
                 std::uint32_t seed)
    {
        mSampleRate = sampleRate;
        mSpec = spec;
        const auto n = static_cast<std::size_t>(sampleRate * tapeLengthSec) + 4;
        mLeft.assign(n, 0.0f);
        mRight.assign(n, 0.0f);
        mRecordHead.prepare(sampleRate, spec);
        mEq.prepare(sampleRate, spec);
        mCompander.prepare(static_cast<float>(spec.compander));
        mHiss.setCutoff(sampleRate, 11000.0);
        mHissTilt.set(Biquad::Type::HighShelf, sampleRate, 2500.0, 0.7, 4.0);
        mNoise = Noise(seed);
        mHissLevel = static_cast<float>(dbToGain(spec.hissDb));
        mCrosstalk = static_cast<float>(dbToGain(spec.trackWidthDb));
        mOutDc.prepare(sampleRate, 8.0);
        mDropout = 1.0f;
        mDropoutPhase = 0.0;
        mGain = 1.0f;
        mPan = 0.0f;
    }

    // Write a block through the record path. Positions are absolute samples.
    void record(double startSample, const float* left, const float* right, std::size_t n)
    {
        const auto base = static_cast<std::size_t>(startSample);
        for (std::size_t i = 0; i < n && base + i < mLeft.size(); ++i)
        {
            float l = mEq.record(left[i]);
            float r = mEq.record(right[i]);
            l = mRecordHead.process(l);
            r = mRecordHead.process(r);
            l = mCompander.encode(l);
            r = mCompander.encode(r);
            mLeft[base + i] = l;
            mRight[base + i] = r;
        }
    }

    // Read at a fractional position through the reproduce path. `speed` is the
    // absolute transport speed: a reproduce head answers the *rate of change* of
    // flux, so a tape that has stopped returns silence. That is the whole
    // mechanism behind a tape stop, not a volume ramp.
    inline void play(double position, double speed, float& outL, float& outR)
    {
        const auto headGain = static_cast<float>(std::clamp(speed, 0.0, 1.6));
        const float l = interpolate4(mLeft.data(), mLeft.size(), position);
        const float r = interpolate4(mRight.data(), mRight.size(), position);

        const float crosstalk = (mNeighbour ? (mNeighbour->peek(position)) : 0.0f) * mCrosstalk;
        float sl = mEq.reproduce(l + crosstalk);
        float sr = mEq.reproduce(r + crosstalk);
        sl = mCompander.decode(sl);
        sr = mCompander.decode(sr);
        // The compander pair is deliberately not an exact inverse, and what it
        // leaves behind is DC. Remove it after the decoder, not before.
        sl = mOutDc.process(sl);
        sr = mOutDc.process(sr);

        // Hiss + modulation noise, then a rare dropout so the tape sounds used.
        const float n = mHissTilt.process(mHiss.process(mNoise.white()));

        if (mDropoutEnabled)
        {
            mDropoutPhase += 1.0 / mSampleRate;
            if (mDropoutPhase > 2.7)
            {
                mDropoutPhase = 0.0;
                mDropoutTimer = 0.004 * mSampleRate;
            }
            if (mDropoutTimer > 0)
            {
                --mDropoutTimer;
                mDropout = 0.72f;
            }
            else
            {
                mDropout += (1.0f - mDropout) * 0.0008f;
            }
        }

        const float gl = mGain * headGain * mDropout * (mPan <= 0 ? 1.0f : 1.0f - mPan);
        const float gr = mGain * headGain * mDropout * (mPan >= 0 ? 1.0f : 1.0f + mPan);
        // The hiss does not scale with speed: a stopped tape still hisses.
        outL += sl * gl + n * mHissLevel * mGain;
        outR += sr * gr + n * mHissLevel * 0.92f * mGain;
    }

    // ---- editing ---------------------------------------------------------
    // `blend` 0 keeps what is on the tape, 1 replaces it, in between is the fade
    // that makes a punch boundary inaudible without making it un-punch-like.
    void writeAt(double positionSec, float inL, float inR, float blend = 1.0f)
    {
        const double pos = positionSec * mSampleRate;
        if (pos < 0) return;
        const auto i = static_cast<long long>(std::floor(pos));
        const float frac = static_cast<float>(pos - static_cast<double>(i));
        const float l = mCompander.encode(mRecordHead.process(mEq.record(inL)));
        const float r = mCompander.encode(mRecordHead.process(mEq.record(inR)));
        scatter(i, frac, l, r, blend);
    }

    void eraseRegion(double startSec, double endSec)
    {
        const auto [a, b] = sampleRange(startSec, endSec);
        for (auto i = a; i < b; ++i)
        {
            mLeft[i] = 0.0f;
            mRight[i] = 0.0f;
        }
    }

    Region copyRegion(double startSec, double endSec) const
    {
        Region region;
        const auto [a, b] = sampleRange(startSec, endSec);
        if (b <= a) return region;
        region.start = startSec;
        region.end = endSec;
        region.left.assign(mLeft.begin() + static_cast<long>(a), mLeft.begin() + static_cast<long>(b));
        region.right.assign(mRight.begin() + static_cast<long>(a), mRight.begin() + static_cast<long>(b));
        return region;
    }

    // Butt splices click; this one fades the old take out as the new one fades in.
    void spliceRegion(double atSec, const Region& region, float gain, double sampleRate)
    {
        const auto n = region.frames();
        if (n == 0) return;
        const auto base = static_cast<long long>(atSec * mSampleRate);
        const double fadeSec = 0.008;
        const auto fade = std::max<std::size_t>(1, static_cast<std::size_t>(fadeSec * sampleRate));

        for (std::size_t k = 0; k < n; ++k)
        {
            const auto idx = base + static_cast<long long>(k);
            if (idx < 0 || idx >= static_cast<long long>(mLeft.size())) continue;
            float g = gain;
            if (k < fade) g *= static_cast<float>(k) / static_cast<float>(fade);
            if (k + fade > n) g *= static_cast<float>(n - k) / static_cast<float>(fade);
            const float inv = 1.0f - std::min(1.0f, g);
            mLeft[static_cast<std::size_t>(idx)] = mLeft[static_cast<std::size_t>(idx)] * inv + region.left[k] * g;
            mRight[static_cast<std::size_t>(idx)] = mRight[static_cast<std::size_t>(idx)] * inv + region.right[k] * g;
        }
    }

    void restoreRegion(const Region& region)
    {
        const auto base = static_cast<long long>(region.start * mSampleRate);
        for (std::size_t k = 0; k < region.frames(); ++k)
        {
            const auto idx = base + static_cast<long long>(k);
            if (idx < 0 || idx >= static_cast<long long>(mLeft.size())) continue;
            mLeft[static_cast<std::size_t>(idx)] = region.left[k];
            mRight[static_cast<std::size_t>(idx)] = region.right[k];
        }
    }

    bool hasContent(double startSec, double endSec, float threshold = 1e-4f) const
    {
        const auto [a, b] = sampleRange(startSec, endSec);
        for (auto i = a; i < b; ++i)
            if (std::fabs(mLeft[i]) > threshold || std::fabs(mRight[i]) > threshold) return true;
        return false;
    }

    // Used to rebuild the take list after an undo, so the list can never claim
    // audio the medium does not have.
    std::vector<std::pair<double, double>> nonSilentSpans(float threshold = 1e-4f,
                                                          double minGapSec = 0.15) const
    {
        std::vector<std::pair<double, double>> spans;
        const std::size_t hop = std::max<std::size_t>(1, static_cast<std::size_t>(mSampleRate * 0.01));
        const auto gap = static_cast<std::size_t>(minGapSec * mSampleRate);
        bool inSpan = false;
        std::size_t spanStart = 0, lastLoud = 0;
        for (std::size_t i = 0; i < mLeft.size(); i += hop)
        {
            bool loud = false;
            for (std::size_t k = i; k < std::min(i + hop, mLeft.size()) && !loud; k += 4)
                loud = std::fabs(mLeft[k]) > threshold || std::fabs(mRight[k]) > threshold;
            if (loud)
            {
                if (!inSpan) { inSpan = true; spanStart = i; }
                lastLoud = i;
            }
            else if (inSpan && i - lastLoud > gap)
            {
                spans.emplace_back(static_cast<double>(spanStart) / mSampleRate,
                                   static_cast<double>(lastLoud + hop) / mSampleRate);
                inSpan = false;
            }
        }
        if (inSpan)
            spans.emplace_back(static_cast<double>(spanStart) / mSampleRate,
                               static_cast<double>(std::min(mLeft.size(), lastLoud + hop)) / mSampleRate);
        return spans;
    }

    double lengthSec() const { return static_cast<double>(mLeft.size()) / mSampleRate; }

    inline float peek(double position) const
    {
        return interpolate4(mLeft.data(), mLeft.size(), position) * 0.5f;
    }

    void setNeighbour(const Track* neighbour) { mNeighbour = neighbour; }
    void setGainDb(double db) { mGain = static_cast<float>(dbToGain(db)); }
    void setPan(double pan) { mPan = static_cast<float>(std::clamp(pan, -1.0, 1.0)); }
    void setDropouts(bool on) { mDropoutEnabled = on; }
    void erase()
    {
        std::fill(mLeft.begin(), mLeft.end(), 0.0f);
        std::fill(mRight.begin(), mRight.end(), 0.0f);
    }

private:
    std::pair<std::size_t, std::size_t> sampleRange(double startSec, double endSec) const
    {
        const double a = std::max(0.0, startSec) * mSampleRate;
        const double b = std::max(0.0, endSec) * mSampleRate;
        const auto ia = std::min(mLeft.size(), static_cast<std::size_t>(a));
        const auto ib = std::min(mLeft.size(), static_cast<std::size_t>(b));
        return { ia, std::max(ia, ib) };
    }

    // Deposit at a fractional position: the write-side twin of interpolation.
    void scatter(long long index, float frac, float l, float r, float blend)
    {
        const auto put = [this, blend](long long i, float g, float vl, float vr) {
            if (i < 0 || i >= static_cast<long long>(mLeft.size())) return;
            const auto u = static_cast<std::size_t>(i);
            mLeft[u] = mLeft[u] * (1.0f - g) + vl * g * blend;
            mRight[u] = mRight[u] * (1.0f - g) + vr * g * blend;
        };
        put(index, 1.0f - frac, l, r);
        put(index + 1, frac, l, r);
    }

    double mSampleRate { 44100 };
    TapeSpec mSpec {};
    std::vector<float> mLeft, mRight;
    RecordHead mRecordHead;
    TapeEq mEq;
    Compander mCompander;
    OnePole mHiss;
    Biquad mHissTilt;
    Noise mNoise;
    DcBlocker mOutDc;
    const Track* mNeighbour { nullptr };
    float mHissLevel { 0 }, mCrosstalk { 0 }, mGain { 1 }, mPan { 0 };
    float mDropout { 1 }, mDropoutPhase { 0 }, mDropoutTimer { 0 };
    bool mDropoutEnabled { true };
};

// ---------------------------------------------------------------------------
// The deck: eight tracks, one transport, plus the master section (three band
// EQ, glue, safety limiter) the OP-1 puts after the tape.
// ---------------------------------------------------------------------------
class Deck {
public:
    static constexpr int kTracks = 8;

    void prepare(double sampleRate, Style style, double tapeLengthSec = 30.0)
    {
        mSampleRate = sampleRate;
        mStyle = style;
        mSpec = specFor(style);
        mTransport.prepare(sampleRate, mSpec, tapeLengthSec);
        for (int t = 0; t < kTracks; ++t)
            mTracks[static_cast<std::size_t>(t)].prepare(sampleRate, mSpec, tapeLengthSec,
                                                        0x1000u + static_cast<std::uint32_t>(t) * 7919u);
        for (int t = 0; t < kTracks; ++t)
        {
            mTracks[static_cast<std::size_t>(t)].setNeighbour(
                t > 0 ? &mTracks[static_cast<std::size_t>(t - 1)] : nullptr);
        }
        mEqLow.set(Biquad::Type::LowShelf, sampleRate, 180.0, 0.7, 0.0);
        mEqMid.set(Biquad::Type::Peak, sampleRate, 1100.0, 0.8, 0.0);
        mEqHigh.set(Biquad::Type::HighShelf, sampleRate, 5200.0, 0.7, 0.0);
        mGlue = 0.0f;
        mGlueAttack = static_cast<float>(1.0 - std::exp(-1.0 / (0.012 * sampleRate)));
        mGlueRelease = static_cast<float>(1.0 - std::exp(-1.0 / (0.30 * sampleRate)));
    }

    void setStyle(Style style)
    {
        if (style == mStyle) return;
        mStyle = style;
        const double length = 30.0;
        const double pos = mTransport.position();
        prepare(mSampleRate, style, length);
        mTransport.seek(pos);
    }

    Style style() const { return mStyle; }
    Transport& transport() { return mTransport; }
    Track& track(int index) { return mTracks[static_cast<std::size_t>(index)]; }
    const TapeSpec& spec() const { return mSpec; }
    double sampleRate() const { return mSampleRate; }

    void setMasterEq(double lowDb, double midDb, double highDb)
    {
        mEqLow.set(Biquad::Type::LowShelf, mSampleRate, 180.0, 0.7, lowDb);
        mEqMid.set(Biquad::Type::Peak, mSampleRate, 1100.0, 0.8, midDb);
        mEqHigh.set(Biquad::Type::HighShelf, mSampleRate, 5200.0, 0.7, highDb);
    }

    // One stereo sample of the mix bus. Returns the summed, glued, limited pair.
    // `trackOut` (optional) receives the post-tape peak of each strip, which is
    // what the panel's per-track meters show. Cost is eight fabs per sample.
    inline void processSample(float& outL, float& outR, float* trackOut = nullptr)
    {
        float l = 0.0f, r = 0.0f;
        const double pos = mTransport.advance();
        const double speed = std::abs(mTransport.speed());
        for (std::size_t i = 0; i < mTracks.size(); ++i)
        {
            float tl = 0.0f, tr = 0.0f;
            mTracks[i].play(pos, speed, tl, tr);
            l += tl;
            r += tr;
            if (trackOut != nullptr) trackOut[i] = std::max(std::fabs(tl), std::fabs(tr));
        }

        l = mEqHigh.process(mEqMid.process(mEqLow.process(l)));
        r = mEqHigh.process(mEqMid.process(mEqLow.process(r)));

        // Tape glue: slow and level dependent, and it is what makes eight
        // separate tracks feel like one piece of tape. The detector has to be
        // slower than the lowest note or it becomes a bass compressor (which
        // reads as "no low end" and is a classic tape-model mistake).
        const float env = std::max(std::fabs(l), std::fabs(r));
        mGlue += (env - mGlue) * (env > mGlue ? mGlueAttack : mGlueRelease);
        const float reduction = 1.0f / (1.0f + mGlue * 0.55f);
        l *= reduction;
        r *= reduction;

        // Safety limiter: only the last 3 dB.
        l = softClip(l);
        r = softClip(r);
        outL = l;
        outR = r;
    }

    static inline float softClip(float x)
    {
        const float threshold = 0.708f; // -3 dBFS
        if (std::fabs(x) <= threshold) return x;
        const float sign = x < 0 ? -1.0f : 1.0f;
        const float excess = std::fabs(x) - threshold;
        return sign * (threshold + (1.0f - threshold) * std::tanh(excess / (1.0f - threshold)));
    }

private:
    double mSampleRate { 44100 };
    Style mStyle { Style::Vintage };
    TapeSpec mSpec {};
    Transport mTransport;
    std::array<Track, kTracks> mTracks;
    Biquad mEqLow, mEqMid, mEqHigh;
    float mGlue { 0 }, mTapeLevel { 0 };
    float mGlueAttack { 0.001f }, mGlueRelease { 0.0001f };
};

} // namespace tt
