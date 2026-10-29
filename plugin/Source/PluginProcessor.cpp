#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace {
constexpr int kTracks = tt::Deck::kTracks;
juce::String trackKey (int i) { return "T" + juce::String (i + 1); }
}

TapeTopProcessor::TapeTopProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    styleParam = new juce::AudioParameterChoice (
        juce::ParameterID { "style", 1 }, "Tape stock",
        juce::StringArray { "Studio", "Vintage", "Porta", "Disk Mini" }, 1);
    speedParam = new juce::AudioParameterFloat (
        juce::ParameterID { "speed", 1 }, "Tape speed",
        juce::NormalisableRange<float> (0.25f, 2.0f, 0.001f), 1.0f);
    masterParam = new juce::AudioParameterFloat (
        juce::ParameterID { "master", 1 }, "Output (dB)",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f);
    loopParam = new juce::AudioParameterBool (juce::ParameterID { "loop", 1 }, "Loop", false);

    addParameter (styleParam);
    addParameter (speedParam);
    addParameter (masterParam);
    addParameter (loopParam);

    styleParam->addListener (&mStyleWatcher);

    for (auto& a : mArmed) a.store (false);
    for (auto& l : mLevel) l.store (0.0f);
}

TapeTopProcessor::~TapeTopProcessor()
{
    styleParam->removeListener (&mStyleWatcher);
}

// ---------------------------------------------------------------------------
void TapeTopProcessor::prepareToPlay (double sampleRate, int)
{
    mSampleRate = sampleRate;
    mDeck.prepare (sampleRate, static_cast<tt::Style> (styleParam->getIndex()), mTapeSeconds);
    mEditor.prepare (sampleRate);
    mPosition.store (0.0);
    mDeck.transport().seek (0.0);
    mPlaying.store (false);
}

void TapeTopProcessor::releaseResources() {}

bool TapeTopProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return in == out || in.isDisabled();
}

void TapeTopProcessor::handleAsyncUpdate()
{
    // Called on the message thread when the stock parameter changes: swapping a
    // stock re-prepares the medium, which must not happen on the audio thread.
    const juce::ScopedLock sl (mEditLock);
    const auto style = static_cast<tt::Style> (juce::jlimit (0, 3, styleParam->getIndex()));
    const double pos = mPosition.load();
    mDeck.setStyle (style);
    mDeck.transport().seek (pos);
    mDeck.transport().setDirection (mReversed.load() ? -1.0 : 1.0);
}

void TapeTopProcessor::setStyle (int index)
{
    styleParam->setValueNotifyingHost (styleParam->convertTo0to1 (static_cast<float> (index)));
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------
double TapeTopProcessor::hostPositionSeconds() const
{
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto seconds = pos->getTimeInSeconds())
                return *seconds;
    return -1.0;
}

void TapeTopProcessor::play()
{
    mPlaying.store (true);
    mDeck.transport().setTargetSpeed (1.0);
}

void TapeTopProcessor::stop()
{
    mPlaying.store (false);
    mRecordRequested.store (false);
    if (mEditor.recording()) mEditor.stopRecording (mPosition.load());
    mTakeCount.store (static_cast<int> (mEditor.takes().size()));
}

void TapeTopProcessor::seek (double seconds)
{
    const double clamped = juce::jmax (0.0, seconds);
    mPosition.store (clamped);
    mDeck.transport().seek (clamped);
}

void TapeTopProcessor::toggleReverse()
{
    mReversed.store (! mReversed.load());
    mDeck.transport().setDirection (mReversed.load() ? -1.0 : 1.0);
}

void TapeTopProcessor::setBreak (bool held)
{
    mBreakTarget.store (held ? 0.0 : 1.0);
    mDeck.transport().setTargetSpeed (held ? 0.0 : mSpeedTarget.load());
}

void TapeTopProcessor::toggleChop()
{
    const bool on = ! mChop.load();
    mChop.store (on);
    if (on)
    {
        const double at = mPosition.load();
        mLoopIn.store (at);
        mLoopOut.store (at + 0.25);
        mDeck.transport().setLoop (at, at + 0.25, true);
        loopParam->setValueNotifyingHost (1.0f);
    }
    else
    {
        mDeck.transport().setLoop (mLoopIn.load(), mLoopOut.load(), false);
        loopParam->setValueNotifyingHost (0.0f);
    }
}

void TapeTopProcessor::setLoop (bool on)
{
    mDeck.transport().setLoop (mLoopIn.load(), mLoopOut.load(), on);
}

void TapeTopProcessor::setLoopIn()
{
    const double pos = mPosition.load();
    mLoopIn.store (pos);
    if (mLoopOut.load() <= pos) mLoopOut.store (juce::jmin (mTapeSeconds, pos + 1.0));
    setLoop (loopParam->get());
}

void TapeTopProcessor::setLoopOut()
{
    const double pos = mPosition.load();
    mLoopOut.store (pos);
    if (mLoopIn.load() >= pos) mLoopIn.store (juce::jmax (0.0, pos - 1.0));
    setLoop (loopParam->get());
}

void TapeTopProcessor::setTapeSeconds (double seconds)
{
    mTapeSeconds = juce::jlimit (10.0, 900.0, seconds);
    const juce::ScopedLock sl (mEditLock);
    mDeck.prepare (mSampleRate, static_cast<tt::Style> (styleParam->getIndex()), mTapeSeconds);
    mEditor.prepare (mSampleRate);
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------
void TapeTopProcessor::armTrack (int track, bool on)
{
    if (track < 0 || track >= kTracks) return;
    mArmed[static_cast<std::size_t> (track)].store (on);
    const juce::ScopedLock sl (mEditLock);
    mEditor.arm (track, on);
}

bool TapeTopProcessor::isArmed (int track) const
{
    if (track < 0 || track >= kTracks) return false;
    return mArmed[static_cast<std::size_t> (track)].load();
}

void TapeTopProcessor::setPunchEnabled (bool on)
{
    const juce::ScopedLock sl (mEditLock);
    mEditor.setPunch (on, mPunchIn.load(), mPunchOut.load());
}

void TapeTopProcessor::setPunchPoints (double inSec, double outSec)
{
    mPunchIn.store (inSec);
    mPunchOut.store (outSec);
    const juce::ScopedLock sl (mEditLock);
    mEditor.setPunch (mEditor.punchActive(), inSec, outSec);
}

void TapeTopProcessor::setRecording (bool on)
{
    mRecordRequested.store (on);
    if (! on)
    {
        const juce::ScopedLock sl (mEditLock);
        mEditor.stopRecording (mPosition.load());
        mTakeCount.store (static_cast<int> (mEditor.takes().size()));
    }
}

void TapeTopProcessor::lift (int track)
{
    const juce::ScopedLock sl (mEditLock);
    for (const auto& take : mEditor.takes())
        if (take.track == track)
        {
            mEditor.lift (mDeck, track, take.start, take.end);
            mTakeCount.store (static_cast<int> (mEditor.takes().size()));
            return;
        }
}

void TapeTopProcessor::drop (int track)
{
    const juce::ScopedLock sl (mEditLock);
    mEditor.drop (mDeck, track, mPosition.load());
    mTakeCount.store (static_cast<int> (mEditor.takes().size()));
}

void TapeTopProcessor::undo()
{
    const juce::ScopedLock sl (mEditLock);
    mEditor.undo (mDeck);
    mTakeCount.store (static_cast<int> (mEditor.takes().size()));
}

void TapeTopProcessor::eraseTrack (int track)
{
    const juce::ScopedLock sl (mEditLock);
    mEditor.erase (mDeck, track, 0.0, mTapeSeconds);
    mTakeCount.store (static_cast<int> (mEditor.takes().size()));
}

void TapeTopProcessor::setTrackGainDb (int track, float db)
{
    if (track < 0 || track >= kTracks) return;
    mDeck.track (track).setGainDb (db);
}

void TapeTopProcessor::setTrackPan (int track, float pan)
{
    if (track < 0 || track >= kTracks) return;
    mDeck.track (track).setPan (juce::jlimit (-1.0f, 1.0f, pan));
}

void TapeTopProcessor::setMasterEq (float lowDb, float midDb, float highDb)
{
    mDeck.setMasterEq (lowDb, midDb, highDb);
}

// ---------------------------------------------------------------------------
void TapeTopProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    // Host transport drives the deck when it offers one; otherwise the panel's
    // transport buttons are the clock.
    bool rolling = mPlaying.load();
    const double hostPos = hostPositionSeconds();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (pos->getIsPlaying())
            {
                rolling = true;
                if (hostPos >= 0.0 && std::abs (hostPos - mPosition.load()) > 0.1)
                    seek (hostPos);
            }

    mSpeedTarget.store (speedParam->get());
    mDeck.transport().setTargetSpeed (rolling ? mBreakTarget.load() * mSpeedTarget.load() : 0.0);

    const float masterGain = juce::Decibels::decibelsToGain ((float) masterParam->get());
    const bool wantRecord = mRecordRequested.load() && rolling && mEditor.armedCount() > 0;

    if (wantRecord && ! mEditor.recording()) mEditor.startRecording (mPosition.load());
    if (! wantRecord && mEditor.recording()) mEditor.stopRecording (mPosition.load());

    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    const float* inL = numIn > 0 ? buffer.getReadPointer (0) : nullptr;
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : inL;

    std::array<float, kTracks> peaks { };

    for (int i = 0; i < numSamples; ++i)
    {
        if (wantRecord && inL != nullptr)
            mEditor.processRecord (mDeck, mPosition.load(), inL[i], inR != nullptr ? inR[i] : inL[i]);

        float l = 0.0f, r = 0.0f;
        std::array<float, kTracks> taps { };
        mDeck.processSample (l, r, taps.data());
        for (std::size_t t = 0; t < taps.size(); ++t)
            peaks[t] = juce::jmax (peaks[t], taps[t]);

        outL[i] = l * masterGain;
        if (outR != nullptr) outR[i] = r * masterGain;
    }

    for (int c = numOut; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, numSamples);

    mPosition.store (mDeck.transport().position());
    for (std::size_t t = 0; t < peaks.size(); ++t)
        mLevel[t].store (juce::jmax (peaks[t], mLevel[t].load() * 0.80f));
    mTakeCount.store (static_cast<int> (mEditor.takes().size()));
}

// ---------------------------------------------------------------------------
TapeTopProcessor::Snapshot TapeTopProcessor::snapshot() const
{
    Snapshot s;
    s.position = mPosition.load();
    s.duration = mTapeSeconds;
    s.playing = mPlaying.load();
    s.reversed = mReversed.load();
    s.looping = loopParam->get();
    s.recording = mRecordRequested.load();
    s.chopping = mChop.load();
    s.punching = mEditor.punchActive();
    s.loopIn = mLoopIn.load();
    s.loopOut = mLoopOut.load();
    s.styleIndex = styleParam->getIndex();
    s.takeCount = mTakeCount.load();
    for (int t = 0; t < kTracks; ++t)
    {
        s.levels[static_cast<std::size_t> (t)] = mLevel[static_cast<std::size_t> (t)].load();
        s.armed[static_cast<std::size_t> (t)] = mArmed[static_cast<std::size_t> (t)].load();
    }
    return s;
}

void TapeTopProcessor::copyTakes (std::vector<tt::Take>& destination) const
{
    const juce::ScopedLock sl (mEditLock);
    destination = mEditor.takes();
}

// ---------------------------------------------------------------------------
void TapeTopProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("TapeTop");
    xml.setAttribute ("style", styleParam->getIndex());
    xml.setAttribute ("speed", speedParam->get());
    xml.setAttribute ("master", masterParam->get());
    xml.setAttribute ("loop", loopParam->get());
    xml.setAttribute ("loopIn", mLoopIn.load());
    xml.setAttribute ("loopOut", mLoopOut.load());
    xml.setAttribute ("punchIn", mPunchIn.load());
    xml.setAttribute ("punchOut", mPunchOut.load());
    xml.setAttribute ("tapeSeconds", mTapeSeconds);

    auto* armed = xml.createNewChildElement ("armed");
    for (int t = 0; t < kTracks; ++t)
        armed->setAttribute (trackKey (t), mArmed[static_cast<std::size_t> (t)].load());

    // The medium is not stored in plugin state: two minutes of eight stereo
    // tracks is ~2 GB as float32. See docs/05-roadmap.md for the storage plan.
    copyXmlToBinary (xml, destData);
}

void TapeTopProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("TapeTop")) return;

    styleParam->setValueNotifyingHost (
        styleParam->convertTo0to1 (static_cast<float> (xml->getIntAttribute ("style", 1))));
    speedParam->setValueNotifyingHost (speedParam->convertTo0to1 (
        static_cast<float> (xml->getDoubleAttribute ("speed", 1.0))));
    masterParam->setValueNotifyingHost (masterParam->convertTo0to1 (
        static_cast<float> (xml->getDoubleAttribute ("master", 0.0))));
    loopParam->setValueNotifyingHost (xml->getBoolAttribute ("loop", false) ? 1.0f : 0.0f);
    mLoopIn.store (xml->getDoubleAttribute ("loopIn", 0.0));
    mLoopOut.store (xml->getDoubleAttribute ("loopOut", 0.0));
    mPunchIn.store (xml->getDoubleAttribute ("punchIn", 0.0));
    mPunchOut.store (xml->getDoubleAttribute ("punchOut", 0.0));
    setTapeSeconds (xml->getDoubleAttribute ("tapeSeconds", 120.0));

    if (auto* armed = xml->getChildByName ("armed"))
        for (int t = 0; t < kTracks; ++t)
            armTrack (t, armed->getBoolAttribute (trackKey (t), false));

    setStyle (styleParam->getIndex());
}

juce::AudioProcessorEditor* TapeTopProcessor::createEditor()
{
    return new TapeTopEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TapeTopProcessor();
}
