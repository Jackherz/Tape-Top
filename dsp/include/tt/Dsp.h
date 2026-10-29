// Tape-Top — small DSP toolkit (header only, no dependencies, C++17)
//
// Deliberately boring primitives: a biquad, a one-pole, a DC blocker, a
// deterministic noise source, an interpolator and a play-operator hysteresis
// element. Everything the tape engine needs and nothing it doesn't.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tt {

constexpr double kPi = 3.14159265358979323846;

inline double dbToGain(double db) { return std::pow(10.0, db / 20.0); }
inline double gainToDb(double g) { return 20.0 * std::log10(std::max(g, 1e-12)); }

// ---------------------------------------------------------------------------
// Biquad (RBJ cookbook). Coefficients are recalculated the slow way, offline.
// ---------------------------------------------------------------------------
class Biquad {
public:
    enum class Type { LowPass, HighPass, Peak, LowShelf, HighShelf };

    void set(Type type, double sampleRate, double freq, double q, double gainDb = 0.0)
    {
        const double w0 = 2.0 * kPi * std::min(freq, sampleRate * 0.49) / sampleRate;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * q);
        const double A = std::pow(10.0, gainDb / 40.0);

        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
        switch (type) {
        case Type::LowPass:
            b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
            a0 = 1 + alpha;    a1 = -2 * cw; a2 = 1 - alpha;
            break;
        case Type::HighPass:
            b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
            a0 = 1 + alpha;    a1 = -2 * cw;   a2 = 1 - alpha;
            break;
        case Type::Peak:
            b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
            break;
        case Type::LowShelf: {
            const double s = 2 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1) - (A - 1) * cw + s);
            b1 = 2 * A * ((A - 1) - (A + 1) * cw);
            b2 = A * ((A + 1) - (A - 1) * cw - s);
            a0 = (A + 1) + (A - 1) * cw + s;
            a1 = -2 * ((A - 1) + (A + 1) * cw);
            a2 = (A + 1) + (A - 1) * cw - s;
            break;
        }
        case Type::HighShelf: {
            const double s = 2 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1) + (A - 1) * cw + s);
            b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 = A * ((A + 1) + (A - 1) * cw - s);
            a0 = (A + 1) - (A - 1) * cw + s;
            a1 = 2 * ((A - 1) - (A + 1) * cw);
            a2 = (A + 1) - (A - 1) * cw - s;
            break;
        }
        }
        mB0 = b0 / a0; mB1 = b1 / a0; mB2 = b2 / a0;
        mA1 = a1 / a0; mA2 = a2 / a0;
    }

    // Transposed direct form II: state carries the *output* recursion. Using
    // input history in the denominator term (an easy mistake) gives a filter
    // that looks plausible and measures wrong, so the engine tests check the
    // magnitude response of every design against the analog prototype.
    inline float process(float in)
    {
        const double y = mB0 * in + mS1;
        mS1 = mB1 * in - mA1 * y + mS2;
        mS2 = mB2 * in - mA2 * y;
        return static_cast<float>(y);
    }

    void reset() { mS1 = mS2 = 0.0; }

private:
    double mB0 { 1 }, mB1 { 0 }, mB2 { 0 }, mA1 { 0 }, mA2 { 0 };
    double mS1 { 0 }, mS2 { 0 };
};

// ---------------------------------------------------------------------------
// One-pole low-pass, used for head/gap loss and hiss shaping.
// ---------------------------------------------------------------------------
class OnePole {
public:
    void setCutoff(double sampleRate, double freq)
    {
        mCoeff = 1.0 - std::exp(-2.0 * kPi * std::min(freq, sampleRate * 0.49) / sampleRate);
    }
    inline float process(float in)
    {
        mState += mCoeff * (in - mState);
        return static_cast<float>(mState);
    }
    void reset() { mState = 0.0; }

private:
    double mCoeff { 1.0 }, mState { 0.0 };
};

// Removes the DC that a biased (asymmetric) saturator leaves behind.
class DcBlocker {
public:
    void prepare(double sampleRate, double cutoffHz = 12.0)
    {
        mR = 1.0 - 2.0 * kPi * cutoffHz / sampleRate;
    }
    inline float process(float in)
    {
        const double y = in - mX1 + mR * mY1;
        mX1 = in;
        mY1 = y;
        return static_cast<float>(y);
    }

private:
    double mR { 0.999 }, mX1 { 0 }, mY1 { 0 };
};

// ---------------------------------------------------------------------------
// Deterministic noise (xorshift). Tape hiss must be repeatable so that A/B
// renders differ only because of the thing being tested.
// ---------------------------------------------------------------------------
class Noise {
public:
    explicit Noise(std::uint32_t seed = 0x9E3779B9u) : mState(seed ? seed : 1u) {}
    inline float white()
    {
        mState ^= mState << 13;
        mState ^= mState >> 17;
        mState ^= mState << 5;
        return static_cast<float>(static_cast<std::int32_t>(mState)) * 4.6566129e-10f;
    }

private:
    std::uint32_t mState;
};

// ---------------------------------------------------------------------------
// 4-point Lagrange interpolation. Good enough for tape warp (< 1 sample of
// jitter) and cheap; the production engine swaps in a windowed-sinc
// resampler for varispeed ranges beyond +/-20%.
// ---------------------------------------------------------------------------
inline float interpolate4(const float* buffer, std::size_t size, double position)
{
    const auto i = static_cast<long long>(std::floor(position));
    const double f = position - static_cast<double>(i);
    const auto clampIndex = [size](long long n) -> std::size_t {
        if (n < 0) return 0;
        if (n >= static_cast<long long>(size)) return size - 1;
        return static_cast<std::size_t>(n);
    };
    const float xm1 = buffer[clampIndex(i - 1)];
    const float x0  = buffer[clampIndex(i)];
    const float x1  = buffer[clampIndex(i + 1)];
    const float x2  = buffer[clampIndex(i + 2)];

    const double c0 = x0;
    const double c1 = -xm1 / 3.0 - 0.5 * x0 + x1 - x1 / 6.0;
    const double c2 = 0.5 * xm1 - x0 + 0.5 * x1;
    const double c3 = -xm1 / 6.0 + 0.5 * x0 - 0.5 * x1 + x2 / 6.0;
    return static_cast<float>(((c3 * f + c2) * f + c1) * f + c0);
}

} // namespace tt
