#pragma once

#include "Plugins/BuiltInPlugins.h"

/**
    Clean editor shared by every built-in effect: preset menu, an optional live
    display (EQ curve / gain-reduction meter) and a grid of knobs built
    automatically from the plugin's parameters.
*/
class RakamPluginEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit RakamPluginEditor (BuiltInPlugin&);
    ~RakamPluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPresetBox();

    struct Control
    {
        std::unique_ptr<juce::Component> component;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    };

    BuiltInPlugin& plugin;
    juce::ComboBox presetBox;
    juce::TextButton resetButton { "Reset" };
    std::vector<std::unique_ptr<Control>> controls;
    juce::Rectangle<int> displayArea;

    static constexpr int knobWidth = 92, knobHeight = 112, headerHeight = 52, columns = 5;
};
