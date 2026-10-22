#pragma once

#include "Engine/AudioEngine.h"

/**
    Settings (Options > Settings, Ctrl+,): General, Audio, Recording, Files, Export and Plugins pages.
    Every change is saved straight away; onChanged lets the main window apply things like UI scale.
*/
class SettingsPanel : public juce::Component
{
public:
    SettingsPanel (AudioEngine& engine, int initialTab = 0);

    std::function<void()> onChanged;
    std::function<void()> onOpenPluginManager;
    std::function<void()> onClearRecent;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void addPage (const juce::String& name, juce::Component* page);
    void showPage (int index);

    juce::OwnedArray<juce::Component> pages;
    juce::OwnedArray<juce::TextButton> navButtons;
    int currentPage = 0;
};
