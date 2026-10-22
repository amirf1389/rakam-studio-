#pragma once

#include "Common.h"

/** The lower half of the window: tabs for Mixer, Recorder and I/O. */
class BottomPanel : public juce::Component
{
public:
    BottomPanel()
    {
        tabBar.setColour (juce::ResizableWindow::backgroundColourId, Rakam::Colours::panel);
    }

    void addPage (const juce::String& name, juce::Component& page)
    {
        pages.push_back (&page);
        addChildComponent (page);

        auto* b = buttons.add (new juce::TextButton (name));
        const int index = buttons.size() - 1;
        b->setClickingTogglesState (true);
        b->setRadioGroupId (777);
        b->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b->setColour (juce::TextButton::buttonOnColourId, Rakam::Colours::background);
        b->setColour (juce::TextButton::textColourOffId, Rakam::Colours::textDim);
        b->setColour (juce::TextButton::textColourOnId, Rakam::Colours::accent);
        b->setConnectedEdges (juce::Button::ConnectedOnBottom);
        b->onClick = [this, index] { showPage (index); };
        addAndMakeVisible (b);

        if (pages.size() == 1)
            showPage (0);
    }

    void showPage (int index)
    {
        current = juce::jlimit (0, (int) pages.size() - 1, index);
        for (size_t i = 0; i < pages.size(); ++i)
        {
            pages[i]->setVisible ((int) i == current);
            buttons[(int) i]->setToggleState ((int) i == current, juce::dontSendNotification);
        }
        resized();
        if (onPageChanged)
            onPageChanged (current);
    }

    int getCurrentPage() const noexcept { return current; }
    std::function<void (int)> onPageChanged;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Rakam::Colours::panel);
        g.setColour (Rakam::Colours::gridStrong);
        g.drawHorizontalLine (tabHeight, 0.0f, (float) getWidth());
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto tabs = r.removeFromTop (tabHeight).withTrimmedLeft (8);
        for (auto* b : buttons)
            b->setBounds (tabs.removeFromLeft (96).withTrimmedTop (4));

        r.removeFromTop (1);
        for (auto* p : pages)
            p->setBounds (r);
    }

private:
    static constexpr int tabHeight = 28;
    juce::Component tabBar;
    std::vector<juce::Component*> pages;
    juce::OwnedArray<juce::TextButton> buttons;
    int current = 0;
};
