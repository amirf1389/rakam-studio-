#pragma once

#include "Engine/AudioEngine.h"

/**
    I/O tab: the audio device at a glance, live meters for every input and output,
    a test tone per output pair, and a routing table (each track's input and output).
*/
class IOPanel : public juce::Component,
                private juce::ChangeListener,
                private juce::Timer
{
public:
    explicit IOPanel (AudioEngine&);
    ~IOPanel() override;

    std::function<void()> onOpenAudioSettings;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class RouteRow;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuild();

    AudioEngine& engine;

    juce::Label deviceLabel;
    juce::TextButton settingsButton { "Audio / ASIO settings..." };
    juce::OwnedArray<juce::TextButton> toneButtons;

    juce::Viewport routesViewport;
    juce::Component routesHolder;
    juce::OwnedArray<RouteRow> routes;
    std::vector<Track*> shownTracks;

    juce::StringArray inputNames, outputPairs;
    juce::Rectangle<int> inputsArea, outputsArea;
};
