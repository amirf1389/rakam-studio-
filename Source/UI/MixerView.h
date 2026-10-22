#pragma once

#include "Engine/AudioEngine.h"

/**
    Mixing console: one channel strip per track plus the master bus.
    Each strip has insert FX slots (VST3), pan, a fader, mute/solo and a level meter.
*/
class MixerView : public juce::Component,
                  private juce::ChangeListener
{
public:
    explicit MixerView (AudioEngine&);
    ~MixerView() override;

    /** Asks the main window to show the plugin chooser for a track. */
    std::function<void (Track&, juce::Component* anchor)> onAddPlugin;
    /** Asks the main window to open a plugin's editor window. */
    std::function<void (juce::AudioPluginInstance&)> onOpenPlugin;

    void paint (juce::Graphics&) override;
    void resized() override;
    void updateMeters();

    static constexpr int stripWidth = 104;

private:
    class Meter;
    class ChannelStrip;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuild();

    AudioEngine& engine;
    juce::Viewport viewport;
    juce::Component stripHolder;
    juce::OwnedArray<ChannelStrip> strips;
    std::unique_ptr<ChannelStrip> masterStrip;
    std::vector<Track*> shownTracks;
    std::vector<int> shownPluginCounts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};
