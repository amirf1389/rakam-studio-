#pragma once

#include "Engine/StemSeparator.h"

/**
    Window for AI stem separation: pick a song, pick a model, press Separate.
    Shows the Demucs log and hands the resulting stem files back to the DAW.
*/
class StemSeparatorDialog : public juce::Component
{
public:
    using StemsReady = std::function<void (const juce::Array<juce::File>& stems, const juce::File& source)>;

    StemSeparatorDialog (juce::PropertiesFile& settings, const juce::File& input,
                         const juce::File& outputFolder, StemsReady onStemsReady);
    ~StemSeparatorDialog() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void run (bool installOnly);
    void appendLog (const juce::String& text);
    void setBusy (bool busy);

    juce::PropertiesFile& settings;
    juce::File inputFile, outputFolder;
    StemsReady onStemsReady;

    StemSeparator separator;

    juce::Label fileLabel { {}, "Song" }, filePath, modelLabel { {}, "Model" }, pythonLabel { {}, "Python" }, hint;
    juce::ComboBox modelBox;
    juce::TextEditor pythonEditor, log;
    juce::TextButton browseButton { "Browse..." }, installButton { "Install Demucs" },
                     separateButton { "Separate" }, cancelButton { "Cancel" }, closeButton { "Close" };
    juce::ToggleButton importToggle { "Import stems as new tracks" };

    std::unique_ptr<juce::FileChooser> chooser;
};
