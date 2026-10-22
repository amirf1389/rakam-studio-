#pragma once

#include "Engine/AudioEngine.h"

/** Welcome screen: start from a template or open a recent project. */
class StartScreen : public juce::Component,
                    private juce::ListBoxModel
{
public:
    struct Choice
    {
        juce::String name;      // starter template name, or empty for "Empty project"
        juce::File file;        // user template file (if any)
    };

    StartScreen (const juce::Array<juce::File>& recent);

    std::function<void (const Choice&)> onNew;
    std::function<void (const juce::File&)> onOpenRecent;
    std::function<void()> onOpenOther;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    void close();

    std::vector<Choice> templates;
    juce::Array<juce::File> recentFiles;
    juce::ListBox templateList { "Templates", this };

    struct RecentModel : public juce::ListBoxModel
    {
        StartScreen* owner = nullptr;
        int getNumRows() override { return owner->recentFiles.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    } recentModel;

    juce::ListBox recentList { "Recent", &recentModel };
    juce::TextButton createButton { "Create" }, openButton { "Open" }, browseButton { "Open other project..." };
};

/** Song details (title, artist...) that are written into exported files. */
class ProjectInfoDialog : public juce::Component
{
public:
    explicit ProjectInfoDialog (AudioEngine&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine;
    juce::OwnedArray<juce::Label> labels;
    juce::TextEditor title, artist, album, genre, comment;
    juce::Label note;
    juce::TextButton saveButton { "Save" }, cancelButton { "Cancel" };
};
