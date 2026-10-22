#pragma once

#include "Common.h"

//==============================================================================
/** One piece of audio placed on the timeline. */
struct AudioClip
{
    juce::String name;
    juce::File file;

    double startSec        = 0.0;   // where the clip begins on the timeline
    double offsetSec       = 0.0;   // where playback begins inside the source file
    double lengthSec       = 0.0;   // how much of the source is played
    double sourceLengthSec = 0.0;   // full length of the source file
    double fadeInSec       = 0.0;
    double fadeOutSec      = 0.0;
    float  gain            = 1.0f;  // clip volume (linear), e.g. from Normalize
    bool   muted           = false; // muted clips are skipped (inactive takes are muted)
    int    takeGroup       = 0;     // > 0: belongs to a set of loop-recorded takes
    int    takeNumber      = 0;     // 1, 2, 3... inside the take group

    // Decoded audio at the engine's current sample rate. Shared so it can be swapped safely.
    std::shared_ptr<const juce::AudioBuffer<float>> audio;
    double audioRate = 0.0;

    std::unique_ptr<juce::AudioThumbnail> thumbnail;   // waveform drawing (UI only)

    double endSec() const noexcept { return startSec + lengthSec; }
};

using ClipPtr = std::shared_ptr<AudioClip>;

//==============================================================================
/** A named part of the song (Intro, Verse, Chorus...) shown in the Structure lane. */
struct Section
{
    juce::String name;
    double startSec = 0.0, endSec = 0.0;
    juce::Colour colour { 0xff4c9aff };

    double length() const noexcept { return endSec - startSec; }
    bool operator== (const Section& o) const
    {
        return name == o.name && juce::exactlyEqual (startSec, o.startSec) && juce::exactlyEqual (endSec, o.endSec) && colour == o.colour;
    }
};

//==============================================================================
/** An insert effect slot on a track or on the master bus. */
struct PluginSlot
{
    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::atomic<bool> bypassed { false };
};

//==============================================================================
/** An audio track (or the master bus when isMaster is true). */
class Track
{
public:
    explicit Track (const juce::String& trackName, bool master = false)
        : name (trackName), isMaster (master) {}

    juce::String name;
    juce::Colour colour { 0xff4c9aff };
    const bool isMaster;

    // Mixer controls — atomics so the UI can change them while audio runs
    std::atomic<float> gain   { 1.0f };   // linear
    std::atomic<float> pan    { 0.0f };   // -1 (left) .. +1 (right)
    std::atomic<bool>  mute   { false };
    std::atomic<bool>  solo   { false };
    std::atomic<bool>  armed  { false };
    std::atomic<bool>  monitor { false };  // hear the input live through the track

    // Input routing: index into the audio device's active input channels
    int  inputIndex  = 0;
    bool stereoInput = false;

    // Output routing: -1 = Master bus, 0 = hardware Out 1+2, 1 = Out 3+4 ...
    // (for the master track: the hardware pair it plays through, default 0)
    std::atomic<int> outputPair { -1 };

    // Peak levels written by the audio thread, read (and reset) by the meters
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };

    // Smoothed meter levels for drawing (message thread only)
    float meterL = 0.0f, meterR = 0.0f;

    void updateMeters() noexcept
    {
        meterL = juce::jmax (peakL.exchange (0.0f), meterL * 0.82f);
        meterR = juce::jmax (peakR.exchange (0.0f), meterR * 0.82f);
    }

    std::vector<ClipPtr> clips;
    std::vector<std::unique_ptr<PluginSlot>> plugins;

    // Audio-thread scratch space
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;

    // Recording state
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> recorder;
    juce::File recordingFile;
    int recordingChannels = 1;

    double getEndSec() const
    {
        double end = 0.0;
        for (auto& c : clips)
            end = juce::jmax (end, c->endSec());
        return end;
    }

    bool hasClips() const noexcept { return ! clips.empty(); }

    int getRequiredChannels() const
    {
        int ch = 2;
        for (auto& p : plugins)
            if (p->instance != nullptr)
                ch = juce::jmax (ch, p->instance->getTotalNumInputChannels(), p->instance->getTotalNumOutputChannels());
        return ch;
    }
};
