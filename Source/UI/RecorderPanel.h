#pragma once

#include "Engine/AudioEngine.h"

/**
    Recorder tab: record mode, count-in, a big status display (time, take number),
    every track's arm button with its input and a live input meter, and disk space.
*/
class RecorderPanel : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit RecorderPanel (AudioEngine&);
    ~RecorderPanel() override;

    std::function<void()> onRecord;
    std::function<void()> onOpenIO;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class TrackRow;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuildRows();
    void updateDiskInfo();

    AudioEngine& engine;

    juce::Label modeLabel { {}, "Record mode" }, countInLabel { {}, "Count-in" }, formatLabel { {}, "Format" },
                formatValue, modeHelp, diskLabel;
    juce::ComboBox modeBox, countInBox;
    juce::TextButton recordButton { "Record" }, stopButton { "Stop" }, folderButton { "Recordings folder" }, ioButton { "Inputs & outputs..." };

    juce::Viewport rowsViewport;
    juce::Component rowsHolder;
    juce::OwnedArray<TrackRow> rows;
    std::vector<Track*> shownTracks;

    juce::Rectangle<int> statusArea;
    juce::String statusTitle, statusTime, statusDetail;
    juce::Colour statusColour;
    int diskCounter = 0;
};
