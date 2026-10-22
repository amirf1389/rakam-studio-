#include "Plugins/PluginEditor.h"

using namespace Rakam::Colours;

RakamPluginEditor::RakamPluginEditor (BuiltInPlugin& p) : juce::AudioProcessorEditor (p), plugin (p)
{
    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;
        if (index >= 0 && index != plugin.getCurrentProgram())
            plugin.setCurrentProgram (index);
    };
    addAndMakeVisible (presetBox);
    refreshPresetBox();

    resetButton.onClick = [this] { plugin.resetToDefaults(); };
    addAndMakeVisible (resetButton);

    for (auto* param : plugin.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (ranged == nullptr)
            continue;

        auto c = std::make_unique<Control>();
        const auto id = ranged->getParameterID();

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (ranged))
        {
            auto box = std::make_unique<juce::ComboBox>();
            box->addItemList (choice->choices, 1);
            c->comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (plugin.state, id, *box);
            c->component = std::move (box);
        }
        else if (dynamic_cast<juce::AudioParameterBool*> (ranged) != nullptr)
        {
            auto button = std::make_unique<juce::TextButton> ("Off");
            button->setClickingTogglesState (true);
            button->onStateChange = [b = button.get()] { b->setButtonText (b->getToggleState() ? "On" : "Off"); };
            c->buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (plugin.state, id, *button);
            button->setButtonText (button->getToggleState() ? "On" : "Off");
            c->component = std::move (button);
        }
        else
        {
            auto knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobWidth - 8, 18);
            knob->setColour (juce::Slider::textBoxTextColourId, text);
            knob->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            knob->setDoubleClickReturnValue (true, ranged->convertFrom0to1 (ranged->getDefaultValue()));
            c->sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (plugin.state, id, *knob);
            c->component = std::move (knob);
        }

        c->label.setText (ranged->getName (32), juce::dontSendNotification);
        c->label.setJustificationType (juce::Justification::centred);
        c->label.setColour (juce::Label::textColourId, textDim);
        c->label.setFont (juce::FontOptions (12.5f));

        addAndMakeVisible (c->label);
        addAndMakeVisible (*c->component);
        controls.push_back (std::move (c));
    }

    const int cols = juce::jmin (columns, (int) controls.size());
    const int rows = ((int) controls.size() + columns - 1) / columns;
    const int display = plugin.getDisplayHeight();
    setSize (juce::jmax (380, cols * knobWidth + 32),
             headerHeight + (display > 0 ? display + 12 : 0) + rows * knobHeight + 20);

    startTimerHz (display > 0 ? 30 : 4);
}

RakamPluginEditor::~RakamPluginEditor()
{
    stopTimer();
}

void RakamPluginEditor::refreshPresetBox()
{
    presetBox.clear (juce::dontSendNotification);
    for (int i = 0; i < plugin.getNumPrograms(); ++i)
        presetBox.addItem (plugin.getProgramName (i), i + 1);
    presetBox.setSelectedId (plugin.getCurrentProgram() + 1, juce::dontSendNotification);
}

void RakamPluginEditor::timerCallback()
{
    if (presetBox.getSelectedId() != plugin.getCurrentProgram() + 1)
        presetBox.setSelectedId (plugin.getCurrentProgram() + 1, juce::dontSendNotification);

    if (! displayArea.isEmpty())
        repaint (displayArea);
}

void RakamPluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    auto header = getLocalBounds().removeFromTop (headerHeight);
    g.setColour (background);
    g.fillRect (header);
    g.setColour (accent);
    g.fillRect (header.removeFromBottom (2));

    g.setColour (text);
    g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
    g.drawText (plugin.getName(), 16, 0, 200, headerHeight, juce::Justification::centredLeft);
    g.setColour (textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("RAKAM", 16, headerHeight - 16, 100, 12, juce::Justification::centredLeft);

    if (! displayArea.isEmpty())
        plugin.paintDisplay (g, displayArea.toFloat());
}

void RakamPluginEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight).reduced (12, 12);
    resetButton.setBounds (header.removeFromRight (64));
    header.removeFromRight (8);
    presetBox.setBounds (header.removeFromRight (170));

    r.reduce (16, 10);

    const int display = plugin.getDisplayHeight();
    displayArea = display > 0 ? r.removeFromTop (display) : juce::Rectangle<int>();
    if (display > 0)
        r.removeFromTop (12);

    const int cols = juce::jmin (columns, (int) controls.size());
    const int gridWidth = cols * knobWidth;
    int x0 = r.getX() + (r.getWidth() - gridWidth) / 2;

    for (size_t i = 0; i < controls.size(); ++i)
    {
        const int col = (int) i % columns, row = (int) i / columns;
        juce::Rectangle<int> cell (x0 + col * knobWidth, r.getY() + row * knobHeight, knobWidth, knobHeight);
        auto& c = *controls[i];
        c.label.setBounds (cell.removeFromTop (18));

        if (dynamic_cast<juce::Slider*> (c.component.get()) != nullptr)
            c.component->setBounds (cell.reduced (4, 2));
        else
            c.component->setBounds (cell.withSizeKeepingCentre (knobWidth - 12, 28).translated (0, -10));
    }
}
