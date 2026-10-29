#pragma once

// Tape-Top — plugin processor
//
// A stereo insert that owns a tape. Audio in goes through the record head onto
// the armed tracks at the transport position; the deck's eight strips are mixed
// and come out of the outputs. That is the hybrid in the brief: a deck you can
// record into, with per-track tape character — neither a plain effect nor a
// DAW-in-a-window.

#include <juce_audio_processors/juce_audio_processors.h>

#include "tt/Recorder.h"

#include <array>
#include <atomic>
#include <vector>

class TapeTopProcessor : public juce::AudioProcessor,
                         private juce::AsyncUpdater
{
public:
    TapeTopProcessor();
    ~TapeTopProcessor() override;

    // ---- AudioProcessor ------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tape-Top"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- transport (message thread) -------------------------------------
    void play();
    void stop();
    void seek (double seconds);
    void toggleReverse();
    void setBreak (bool held);
    void toggleChop();
    void setLoop (bool on);
    void setLoopIn();
    void setLoopOut();
    void setTapeSeconds (double seconds);
    void setRecording (bool on);

    // ---- editing --------------------------------------------------------
    void armTrack (int track, bool on);
    bool isArmed (int track) const;
    void setPunchEnabled (bool on);
    void setPunchPoints (double inSec, double outSec);
    void lift (int track);
    void drop (int track);
    void undo();
    void eraseTrack (int track);
    void setStyle (int index);

    // ---- mixing ---------------------------------------------------------
    void setTrackGainDb (int track, float db);
    void setTrackPan (int track, float pan);
    void setMasterEq (float lowDb, float midDb, float highDb);

    // ---- readouts for the editor ----------------------------------------
    struct Snapshot
    {
        double position = 0.0;
        double duration = 0.0;
        bool playing = false;
        bool reversed = false;
        bool looping = false;
        bool recording = false;
        bool chopping = false;
        bool punching = false;
        double loopIn = 0.0, loopOut = 0.0;
        std::array<float, 8> levels { };
        std::array<bool, 8> armed { };
        int styleIndex = 0;
        int takeCount = 0;
    };

    Snapshot snapshot() const;
    void copyTakes (std::vector<tt::Take>& destination) const;

    tt::TapeEditor& editor() { return mEditor; }
    tt::Deck& deck() { return mDeck; }

    juce::AudioParameterChoice* styleParam = nullptr;
    juce::AudioParameterFloat* speedParam = nullptr;
    juce::AudioParameterFloat* masterParam = nullptr;
    juce::AudioParameterBool* loopParam = nullptr;

private:
    void handleAsyncUpdate() override;
    double hostPositionSeconds() const;

    struct StyleWatcher : juce::AudioProcessorParameter::Listener
    {
        explicit StyleWatcher (TapeTopProcessor& p) : processor (p) {}
        void parameterValueChanged (int, float) override { processor.triggerAsyncUpdate(); }
        void parameterGestureChanged (int, bool) override {}
        TapeTopProcessor& processor;
    };

    tt::Deck mDeck;
    tt::TapeEditor mEditor;
    StyleWatcher mStyleWatcher { *this };

    double mSampleRate = 44100.0;
    double mTapeSeconds = 120.0;

    std::atomic<bool> mPlaying { false };
    std::atomic<bool> mReversed { false };
    std::atomic<bool> mChop { false };
    std::atomic<bool> mRecordRequested { false };
    std::atomic<double> mPosition { 0.0 };
    std::atomic<double> mSpeedTarget { 1.0 };
    std::atomic<double> mBreakTarget { 1.0 };
    std::atomic<double> mLoopIn { 0.0 }, mLoopOut { 0.0 };
    std::atomic<double> mPunchIn { 0.0 }, mPunchOut { 0.0 };

    std::array<std::atomic<bool>, tt::Deck::kTracks> mArmed { };
    std::array<std::atomic<float>, tt::Deck::kTracks> mLevel { };

    mutable juce::CriticalSection mEditLock;
    std::atomic<int> mTakeCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeTopProcessor)
};
