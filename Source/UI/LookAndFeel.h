#pragma once

#include "Common.h"

/** Dark studio theme. */
class RakamLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RakamLookAndFeel()
    {
        using namespace Rakam::Colours;

        setColourScheme ({ background, panel, panelLight, gridStrong, text, accent,
                           juce::Colours::white, panelLight, text });

        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::DocumentWindow::backgroundColourId, background);
        setColour (juce::TextButton::buttonColourId, panelLight);
        setColour (juce::TextButton::buttonOnColourId, accent);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ComboBox::backgroundColourId, panelLight);
        setColour (juce::ComboBox::outlineColourId, gridStrong);
        setColour (juce::Slider::thumbColourId, accent);
        setColour (juce::Slider::trackColourId, gridStrong);
        setColour (juce::Slider::rotarySliderFillColourId, accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, gridStrong);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, text);
        setColour (juce::TextEditor::backgroundColourId, background);
        setColour (juce::TextEditor::outlineColourId, gridStrong);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
        setColour (juce::ScrollBar::thumbColourId, gridStrong);
        setColour (juce::ListBox::backgroundColourId, background);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::ProgressBar::foregroundColourId, accent);
        setColour (juce::ToggleButton::tickColourId, accent);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (14.0f, (float) buttonHeight * 0.55f)));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }

        // Mixer-style fader: thin slot + flat cap
        const auto cx = (float) x + (float) width * 0.5f;
        g.setColour (Rakam::Colours::background);
        g.fillRoundedRectangle (cx - 2.5f, (float) y, 5.0f, (float) height, 2.5f);

        g.setColour (Rakam::Colours::accent.withAlpha (0.55f));
        g.fillRoundedRectangle (cx - 2.5f, sliderPos, 5.0f, (float) (y + height) - sliderPos, 2.5f);

        juce::Rectangle<float> cap (cx - 14.0f, sliderPos - 8.0f, 28.0f, 16.0f);
        g.setColour (juce::Colour (0xffd8dbe2));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (Rakam::Colours::background);
        g.drawHorizontalLine ((int) sliderPos, cap.getX() + 4.0f, cap.getRight() - 4.0f);
    }
};
