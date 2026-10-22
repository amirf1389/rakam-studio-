#pragma once

#include "Engine/AudioEngine.h"

/**
    Left-hand browser with two tabs:
      - Files:   browse your sample folders, click to preview, double-click or drag onto the timeline to import.
      - Effects: every built-in and VST3 effect; double-click to add it to the selected track.
*/
class BrowserPanel : public juce::Component,
                     private juce::FileBrowserListener,
                     private juce::ListBoxModel,
                     private juce::ChangeListener
{
public:
    explicit BrowserPanel (AudioEngine&);
    ~BrowserPanel() override;

    std::function<void (const juce::File&)> onImportFile;
    std::function<void (const juce::PluginDescription&)> onAddEffect;

    void refreshEffects();

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr const char* dragDescription = "RakamBrowserFile";

private:
    // FileBrowserListener
    void selectionChanged() override;
    void fileClicked (const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked (const juce::File&) override;
    void browserRootChanged (const juce::File&) override {}

    // ListBoxModel
    int getNumRows() override { return (int) effects.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshEffects(); }

    void showTab (int index);
    void setRoot (const juce::File& folder);

    struct EffectRow
    {
        juce::String header;               // non-empty = section header row
        juce::PluginDescription desc;
    };

    AudioEngine& engine;
    juce::TextButton filesTab { "Files" }, effectsTab { "Effects" };
    juce::ComboBox locationBox;
    juce::TextButton previewButton { "Preview" };
    juce::Label hint;

    juce::TimeSliceThread directoryThread { "Browser scan" };
    juce::WildcardFileFilter audioFilter { "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3", "*", "Audio files" };
    juce::DirectoryContentsList contents { &audioFilter, directoryThread };
    juce::FileTreeComponent fileTree { contents };

    juce::ListBox effectList { "Effects", this };
    std::vector<EffectRow> effects;
    juce::Array<juce::File> locations;
    std::unique_ptr<juce::FileChooser> chooser;
    int currentTab = 0;
};
