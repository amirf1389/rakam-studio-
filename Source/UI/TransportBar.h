#pragma once

#include "Engine/AudioEngine.h"

/** Top bar: browser toggle, transport, loop, time display, tempo, metronome, undo/redo and main actions. */
class TransportBar : public juce::Component,
                     private juce::Timer
{
public:
    explicit TransportBar (AudioEngine& engine);

    std::function<void()> onRecord, onExport, onSeparate, onToggleMixer, onToggleBrowser, onAddTrack, onSettings,
                          onUndo, onRedo, onToggleLoop;

    void setPanelStates (bool browserVisible, bool mixerVisible);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    AudioEngine& engine;

    std::unique_ptr<juce::DrawableButton> browserButton, rewindButton, playButton, stopButton, recordButton, loopButton,
                                          metronomeButton, undoButton, redoButton;
    juce::Label barsLabel, timeLabel, bpmLabel;
    juce::Slider bpmSlider;
    juce::TextButton addTrackButton { "+ Track" }, mixerButton { "Mixer" }, separateButton { "AI Stems" },
                     exportButton { "Export" }, settingsButton { "Audio" };

    juce::Rectangle<int> timeWell;
    bool lastPlaying = false, lastRecording = false;
};
