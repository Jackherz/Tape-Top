// Tape-Top — recorder / editor over the medium
//
// TapeMachine.h prints a prepared sketch onto tape. This adds what a *recorder*
// needs and what a tape model usually skips: takes, punch, lift/drop,
// split/join, splice crossfades and undo.
//
// The behaviour is taken from the machine rather than from a DAW:
//   * an edit is destructive on the medium; the only thing that brings audio
//     back is the undo stack (which is why tape undo mattered when it arrived);
//   * LIFT removes audio into memory and DROP puts it back where the head is;
//   * every seam is crossfaded, because a butt splice clicks;
//   * a punch has hard boundaries in the medium but a short fade at the edges,
//     blended against what was already there, so it reads as a punch and not a
//     click.
#pragma once

#include "TapeMachine.h"

#include <array>
#include <deque>
#include <vector>

namespace tt {

class TapeEditor {
public:
    static constexpr int kUndoDepth = 24;

    void prepare(double sampleRate)
    {
        mSampleRate = sampleRate;
        mTakes.clear();
        mUndo.clear();
        mArmed.fill(false);
        mRecording = false;
        mClipboard = {};
    }

    // ---- arming and punch ------------------------------------------------
    void arm(int track, bool on)
    {
        if (track < 0 || track >= Deck::kTracks) return;
        mArmed[static_cast<std::size_t>(track)] = on;
    }
    bool armed(int track) const { return mArmed[static_cast<std::size_t>(track)]; }
    int armedCount() const
    {
        int n = 0;
        for (bool a : mArmed) n += a ? 1 : 0;
        return n;
    }

    void setPunch(bool on, double inSec, double outSec) { mPunch = { on, inSec, outSec }; }
    bool punchActive() const { return mPunch.on; }

    void startRecording(double position)
    {
        mRecording = true;
        mRecordStart = position;
    }

    bool recording() const { return mRecording; }

    void stopRecording(double position)
    {
        if (!mRecording) return;
        mRecording = false;
        const double start = std::min(mRecordStart, position);
        const double end = std::max(mRecordStart, position);
        if (end - start < 1e-3) return;
        for (int t = 0; t < Deck::kTracks; ++t)
            if (armed(t)) mTakes.push_back({ t, start, end });
    }

    // One stereo frame of input into every armed track, through the record
    // chain, blended by the punch edge.
    void processRecord(Deck& deck, double position, float inL, float inR)
    {
        if (!mRecording) return;
        const float g = punchGain(position);
        if (g <= 0.0f) return;
        for (int t = 0; t < Deck::kTracks; ++t)
            if (armed(t)) deck.track(t).writeAt(position, inL * g, inR * g, g);
    }

    // ---- destructive edits ------------------------------------------------
    void erase(Deck& deck, int track, double startSec, double endSec)
    {
        if (track < 0 || track >= Deck::kTracks) return;
        pushUndo(deck, track, startSec, endSec);
        deck.track(track).eraseRegion(startSec, endSec);
        forgetTakes(track, startSec, endSec);
    }

    // LIFT: copy into memory, remove from the tape.
    Region lift(Deck& deck, int track, double startSec, double endSec)
    {
        Region r;
        if (track < 0 || track >= Deck::kTracks) return r;
        pushUndo(deck, track, startSec, endSec);
        r = deck.track(track).copyRegion(startSec, endSec);
        deck.track(track).eraseRegion(startSec, endSec);
        forgetTakes(track, startSec, endSec);
        if (!r.empty()) mClipboard = r;
        return r;
    }

    void liftAll(Deck& deck, double startSec, double endSec)
    {
        for (int t = 0; t < Deck::kTracks; ++t)
            if (deck.track(t).hasContent(startSec, endSec))
                lift(deck, t, startSec, endSec);
    }

    // DROP: place the clipboard at the head, crossfaded into what is there.
    void drop(Deck& deck, int track, double atSec, float gain = 1.0f)
    {
        if (mClipboard.empty() || track < 0 || track >= Deck::kTracks) return;
        const double len = static_cast<double>(mClipboard.frames()) / mSampleRate;
        pushUndo(deck, track, atSec, atSec + len);
        deck.track(track).spliceRegion(atSec, mClipboard, gain, mSampleRate);
        mTakes.push_back({ track, atSec, atSec + len });
    }

    bool hasClipboard() const { return !mClipboard.empty(); }

    // SPLIT: a take boundary. The audio does not change.
    void split(int track, double atSec)
    {
        for (auto& t : mTakes)
        {
            if (t.track != track) continue;
            if (atSec > t.start + 1e-3 && atSec < t.end - 1e-3)
            {
                const double end = t.end;
                t.end = atSec;
                mTakes.push_back({ track, atSec, end });
                return;
            }
        }
    }

    // JOIN: merge two takes that actually touch.
    bool join(int track, double atSec, double tolerance = 0.06)
    {
        for (std::size_t i = 0; i < mTakes.size(); ++i)
        {
            for (std::size_t j = 0; j < mTakes.size(); ++j)
            {
                if (i == j) continue;
                auto& a = mTakes[i];
                auto& b = mTakes[j];
                if (a.track != track || b.track != track) continue;
                if (std::fabs(a.end - b.start) < tolerance && std::fabs(a.end - atSec) < tolerance)
                {
                    a.end = b.end;
                    mTakes.erase(mTakes.begin() + static_cast<long>(j));
                    return true;
                }
            }
        }
        return false;
    }

    // ---- undo -------------------------------------------------------------
    bool canUndo() const { return !mUndo.empty(); }

    void undo(Deck& deck)
    {
        if (mUndo.empty()) return;
        auto entry = std::move(mUndo.back());
        mUndo.pop_back();
        deck.track(entry.track).restoreRegion(entry.region);
        rebuildTakes(deck);
    }

    // ---- takes ------------------------------------------------------------
    const std::vector<Take>& takes() const { return mTakes; }
    void clearTakes() { mTakes.clear(); }

    // Take list from what is actually on the medium. Run after undo so the list
    // cannot claim audio that is no longer there.
    void rebuildTakes(Deck& deck)
    {
        mTakes.clear();
        for (int t = 0; t < Deck::kTracks; ++t)
        {
            for (const auto& span : deck.track(t).nonSilentSpans())
                mTakes.push_back({ t, span.first, span.second });
        }
    }

private:
    struct Punch { bool on { false }; double in { 0 }, out { 0 }; };
    struct UndoEntry { int track { 0 }; Region region; };

    float punchGain(double position) const
    {
        if (!mPunch.on) return 1.0f;
        constexpr double fade = 0.004;
        if (position < mPunch.in - fade || position > mPunch.out + fade) return 0.0f;
        if (position < mPunch.in) return static_cast<float>((position - (mPunch.in - fade)) / fade);
        if (position > mPunch.out) return static_cast<float>(1.0 - (position - mPunch.out) / fade);
        return 1.0f;
    }

    void pushUndo(Deck& deck, int track, double startSec, double endSec)
    {
        Region r = deck.track(track).copyRegion(startSec, endSec);
        if (r.empty()) return;
        mUndo.push_back({ track, std::move(r) });
        if (mUndo.size() > static_cast<std::size_t>(kUndoDepth)) mUndo.pop_front();
    }

    void forgetTakes(int track, double startSec, double endSec)
    {
        std::vector<Take> kept;
        for (auto& t : mTakes)
        {
            if (t.track != track) { kept.push_back(t); continue; }
            if (t.end <= startSec || t.start >= endSec) { kept.push_back(t); continue; }
            if (t.start < startSec) kept.push_back({ track, t.start, startSec });
            if (t.end > endSec) kept.push_back({ track, endSec, t.end });
        }
        mTakes.swap(kept);
    }

    double mSampleRate { 44100 };
    bool mRecording { false };
    double mRecordStart { 0 };
    Punch mPunch;
    std::array<bool, Deck::kTracks> mArmed {};
    std::vector<Take> mTakes;
    Region mClipboard;
    std::deque<UndoEntry> mUndo;
};

} // namespace tt
