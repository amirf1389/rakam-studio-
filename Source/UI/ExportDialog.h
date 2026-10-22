#pragma once

#include "Engine/AudioEngine.h"

/**
    "Bring out the mix": renders the full mix and/or every track as a separate
    stem. Stems all start at 0:00 and have the same length, so they line up
    perfectly when dropped into any other DAW (FL Studio, Ableton, Cubase,
    Logic, Pro Tools...) or sent to a mixing engineer.
*/
class ExportDialog : public juce::Component
{
public:
    ExportDialog (AudioEngine& engine, const juce::File& defaultFolder, const juce::String& defaultName);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Builds the list of files to render from the current settings. */
    std::vector<AudioEngine::RenderOptions> buildJobs() const;

private:
    void startExport();
    void updateInfo();
    juce::String getExtension() const;

    AudioEngine& engine;
    juce::File folder;

    juce::Label whatLabel { {}, "Export" }, formatLabel { {}, "Format" }, tailLabel { {}, "Tail (s)" },
                nameLabel { {}, "Name" }, folderLabel { {}, "Folder" }, folderPath, info;
    juce::Label rangeLabel { {}, "Range" };
    juce::ComboBox whatBox, formatBox, rangeBox;
    juce::Slider tailSlider;
    juce::TextEditor nameEditor;
    juce::TextButton browseButton { "Browse..." }, exportButton { "Export" }, cancelButton { "Close" };
    juce::ToggleButton skipEmpty { "Skip tracks without clips" }, skipMuted { "Skip muted tracks" },
                       normalizeToggle { "Normalize full mix to -1 dB" };

    std::unique_ptr<juce::FileChooser> chooser;
};
