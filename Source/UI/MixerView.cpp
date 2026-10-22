#include "UI/MixerView.h"

using namespace Rakam::Colours;

//==============================================================================
class MixerView::Meter : public juce::Component
{
public:
    explicit Meter (Track& t) : track (t) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (background);
        g.fillRoundedRectangle (r, 2.0f);

        auto drawBar = [&] (juce::Rectangle<float> bar, float level)
        {
            const float db = juce::Decibels::gainToDecibels (level, -60.0f);
            const float proportion = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
            auto filled = bar.removeFromBottom (bar.getHeight() * proportion);

            juce::ColourGradient grad (solo, 0.0f, r.getBottom(), record, 0.0f, r.getY(), false);
            grad.addColour (0.75, mute);
            g.setGradientFill (grad);
            g.fillRect (filled);
        };

        auto bars = r.reduced (2.0f);
        auto left = bars.removeFromLeft (bars.getWidth() * 0.5f).withTrimmedRight (1.0f);
        drawBar (left, track.meterL);
        drawBar (bars.withTrimmedLeft (1.0f), track.meterR);

        // 0 dB line
        const float zeroY = r.getY() + 2.0f + (r.getHeight() - 4.0f) * (6.0f / 66.0f);
        g.setColour (textDim.withAlpha (0.6f));
        g.drawHorizontalLine ((int) zeroY, r.getX(), r.getRight());
    }

private:
    Track& track;
};

//==============================================================================
class MixerView::ChannelStrip : public juce::Component
{
public:
    ChannelStrip (MixerView& m, Track& t) : mixer (m), track (t), meter (t)
    {
        name.setJustificationType (juce::Justification::centred);
        name.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (name);

        addFxButton.setButtonText ("+ FX");
        addFxButton.setTooltip ("Add a VST3 effect");
        addFxButton.onClick = [this] { if (mixer.onAddPlugin) mixer.onAddPlugin (track, &addFxButton); };
        addAndMakeVisible (addFxButton);

        if (! track.isMaster)
        {
            pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            pan.setRange (-1.0, 1.0, 0.01);
            pan.setDoubleClickReturnValue (true, 0.0);
            pan.setTooltip ("Pan (double-click to centre)");
            pan.onValueChange = [this] { track.pan = (float) pan.getValue(); mixer.engine.markEdited(); };
            addAndMakeVisible (pan);

            for (auto* b : { &muteButton, &soloButton })
            {
                b->setClickingTogglesState (true);
                addAndMakeVisible (b);
            }

            muteButton.setButtonText ("M");
            soloButton.setButtonText ("S");
            muteButton.setColour (juce::TextButton::buttonOnColourId, mute);
            soloButton.setColour (juce::TextButton::buttonOnColourId, solo);
            muteButton.onClick = [this] { track.mute = muteButton.getToggleState(); mixer.engine.sendChangeMessage(); };
            soloButton.onClick = [this] { track.solo = soloButton.getToggleState(); mixer.engine.sendChangeMessage(); };
        }

        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange (-60.0, 6.0, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.setTooltip ("Volume (double-click for 0 dB)");
        fader.onValueChange = [this]
        {
            const double db = fader.getValue();
            track.gain = db <= -59.9 ? 0.0f : juce::Decibels::decibelsToGain ((float) db);
            updateDbLabel();
            mixer.engine.markEdited();
        };
        addAndMakeVisible (fader);

        dbLabel.setJustificationType (juce::Justification::centred);
        dbLabel.setFont (juce::FontOptions (11.0f));
        dbLabel.setColour (juce::Label::textColourId, textDim);
        addAndMakeVisible (dbLabel);

        addAndMakeVisible (meter);
        refresh();
    }

    void refresh()
    {
        name.setText (track.name, juce::dontSendNotification);

        const double db = track.gain <= 0.0f ? -60.0 : (double) juce::Decibels::gainToDecibels (track.gain.load());
        if (! fader.isMouseButtonDown())
            fader.setValue (db, juce::dontSendNotification);
        updateDbLabel();

        if (! track.isMaster)
        {
            if (! pan.isMouseButtonDown())
                pan.setValue (track.pan.load(), juce::dontSendNotification);
            muteButton.setToggleState (track.mute, juce::dontSendNotification);
            soloButton.setToggleState (track.solo, juce::dontSendNotification);
        }

        rebuildFxButtons();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (track.isMaster ? panelLight : panel);
        g.setColour (track.colour);
        g.fillRect (0, 0, getWidth(), 4);
        g.setColour (grid);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

        g.setColour (textDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("INSERTS", fxArea.withHeight (12), juce::Justification::centredLeft);
        if (! track.isMaster)
        {
            g.drawText ("L", pan.getBounds().withX (pan.getX() - 14).withWidth (12), juce::Justification::centredRight);
            g.drawText ("R", pan.getBounds().withX (pan.getRight() + 2).withWidth (12), juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 6);
        r.removeFromTop (2);
        name.setBounds (r.removeFromTop (20));
        r.removeFromTop (4);

        fxArea = r.removeFromTop (12 + maxVisibleFx * 21 + 22);
        auto fx = fxArea.withTrimmedTop (13);
        for (auto* b : fxButtons)
        {
            if (fx.getHeight() < 41) { b->setVisible (false); continue; }
            b->setVisible (true);
            b->setBounds (fx.removeFromTop (19));
            fx.removeFromTop (2);
        }
        addFxButton.setBounds (fx.removeFromBottom (20));

        if (! track.isMaster)
        {
            r.removeFromTop (2);
            pan.setBounds (r.removeFromTop (38).withSizeKeepingCentre (38, 38));
            r.removeFromTop (4);

            auto ms = r.removeFromBottom (22);
            muteButton.setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (2, 0));
            soloButton.setBounds (ms.reduced (2, 0));
            r.removeFromBottom (4);
        }

        dbLabel.setBounds (r.removeFromBottom (16));
        r.removeFromBottom (2);

        auto meterArea = r.removeFromRight (14);
        meter.setBounds (meterArea);
        fader.setBounds (r);
    }

    void updateMeter() { meter.repaint(); }
    Track& getTrack() { return track; }

private:
    static constexpr int maxVisibleFx = 3;

    void updateDbLabel()
    {
        const double db = fader.getValue();
        dbLabel.setText (db <= -59.9 ? juce::String ("-inf") : juce::String (db, 1) + " dB", juce::dontSendNotification);
    }

    void rebuildFxButtons()
    {
        fxButtons.clear();

        for (int i = 0; i < (int) track.plugins.size(); ++i)
        {
            auto& slot = *track.plugins[(size_t) i];
            auto* b = fxButtons.add (new juce::TextButton (slot.instance->getName()));
            b->setTooltip (slot.instance->getName() + " - click to open, right-click for options");
            b->setColour (juce::TextButton::buttonColourId, slot.bypassed ? background : track.colour.withAlpha (0.35f));
            b->setColour (juce::TextButton::textColourOffId, slot.bypassed ? textDim : text);
            b->onClick = [this, i]
            {
                if (! juce::isPositiveAndBelow (i, (int) track.plugins.size()))
                    return;

                if (juce::ModifierKeys::getCurrentModifiers().isPopupMenu())
                    return;

                if (mixer.onOpenPlugin)
                    mixer.onOpenPlugin (*track.plugins[(size_t) i]->instance);
            };
            b->addMouseListener (this, false);
            b->getProperties().set ("slot", i);
            addAndMakeVisible (b);
        }

        resized();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() || ! e.originalComponent->getProperties().contains ("slot"))
            return;

        const int index = e.originalComponent->getProperties()["slot"];
        if (! juce::isPositiveAndBelow (index, (int) track.plugins.size()))
            return;

        auto& slot = *track.plugins[(size_t) index];
        auto* engine = &mixer.engine;
        auto* t = &track;

        juce::PopupMenu m;
        m.addSectionHeader (slot.instance->getName());
        m.addItem ("Open editor", [this, index] { if (mixer.onOpenPlugin) mixer.onOpenPlugin (*track.plugins[(size_t) index]->instance); });
        m.addItem ("Bypass", true, slot.bypassed.load(), [engine, t, index]
        {
            if (juce::isPositiveAndBelow (index, (int) t->plugins.size()))
            {
                auto& s = *t->plugins[(size_t) index];
                s.bypassed = ! s.bypassed.load();
                engine->markEdited();
                engine->sendChangeMessage();
            }
        });
        m.addSeparator();
        m.addItem ("Remove", [engine, t, index] { engine->removePlugin (*t, index); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (e.originalComponent).withMousePosition());
    }

    MixerView& mixer;
    Track& track;
    juce::Label name, dbLabel;
    juce::Slider pan, fader;
    juce::TextButton muteButton, soloButton, addFxButton;
    juce::OwnedArray<juce::TextButton> fxButtons;
    juce::Rectangle<int> fxArea;
    Meter meter;
};

//==============================================================================
MixerView::MixerView (AudioEngine& e) : engine (e)
{
    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);

    masterStrip = std::make_unique<ChannelStrip> (*this, engine.getMaster());
    addAndMakeVisible (*masterStrip);

    engine.addChangeListener (this);
    rebuild();
}

MixerView::~MixerView()
{
    engine.removeChangeListener (this);
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (gridStrong);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    if (strips.isEmpty())
    {
        g.setColour (textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Add a track to start mixing", viewport.getBounds(), juce::Justification::centred);
    }
}

void MixerView::resized()
{
    auto r = getLocalBounds().withTrimmedTop (1);
    masterStrip->setBounds (r.removeFromRight (stripWidth + 8));
    viewport.setBounds (r);

    const int h = viewport.getMaximumVisibleHeight() - (strips.size() * stripWidth > r.getWidth() ? 10 : 0);
    stripHolder.setSize (juce::jmax (r.getWidth(), strips.size() * stripWidth), juce::jmax (100, h));

    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * stripWidth, 0, stripWidth, stripHolder.getHeight());
}

void MixerView::rebuild()
{
    std::vector<Track*> current;
    for (int i = 0; i < engine.getNumTracks(); ++i)
        current.push_back (engine.getTrack (i));

    if (current != shownTracks)
    {
        shownTracks = current;
        strips.clear();

        for (auto* t : current)
            stripHolder.addAndMakeVisible (strips.add (new ChannelStrip (*this, *t)));

        resized();
    }
    else
    {
        for (auto* s : strips)
            s->refresh();
    }

    masterStrip->refresh();
    repaint();
}

void MixerView::updateMeters()
{
    for (auto* s : strips)
        s->updateMeter();
    masterStrip->updateMeter();
}

void MixerView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuild();
}
