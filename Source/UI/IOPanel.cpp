#include "UI/IOPanel.h"

using namespace Rakam::Colours;

namespace
{
    void drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float level)
    {
        g.setColour (panelLight);
        g.fillRoundedRectangle (r, 2.0f);
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float w = r.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setColour (level > 0.98f ? record : level > 0.5f ? mute : solo);
        g.fillRoundedRectangle (r.withWidth (w), 2.0f);
    }

    constexpr int lineHeight = 22;
}

//==============================================================================
class IOPanel::RouteRow : public juce::Component
{
public:
    RouteRow (AudioEngine& e, Track& t) : engine (e), track (t)
    {
        if (! track.isMaster)
        {
            input.onChange = [this]
            {
                const int id = input.getSelectedId();
                if (id <= 0)
                    return;
                track.stereoInput = id >= 1000;
                track.inputIndex  = track.stereoInput ? id - 1000 : id - 1;
                engine.markEdited();
                engine.sendChangeMessage();
            };
            addAndMakeVisible (input);
        }

        output.onChange = [this]
        {
            const int id = output.getSelectedId();
            track.outputPair = track.isMaster ? id - 1 : (id == 1 ? -1 : id - 2);
            engine.markEdited();
            engine.sendChangeMessage();
        };
        output.setTooltip (track.isMaster ? "Which outputs your speakers / monitors are connected to"
                                          : "Master = normal mix. Choose an output pair to send this track somewhere else "
                                            "(e.g. a headphone mix) - it then skips the master bus and the full-mix export.");
        addAndMakeVisible (output);
        refresh();
    }

    void refresh()
    {
        const auto ins = engine.getActiveInputNames();
        const auto outs = engine.getOutputPairNames();

        if (! track.isMaster)
        {
            input.clear (juce::dontSendNotification);
            for (int i = 0; i < ins.size(); ++i)
                input.addItem ("In " + juce::String (i + 1) + "  " + ins[i], i + 1);
            for (int i = 0; i + 1 < ins.size(); i += 2)
                input.addItem ("In " + juce::String (i + 1) + "+" + juce::String (i + 2) + " stereo", 1000 + i);
            input.setTextWhenNothingSelected (ins.isEmpty() ? "No inputs enabled" : "Choose input");
            const int wanted = track.stereoInput ? 1000 + track.inputIndex : track.inputIndex + 1;
            if (input.indexOfItemId (wanted) >= 0)
                input.setSelectedId (wanted, juce::dontSendNotification);
        }

        output.clear (juce::dontSendNotification);
        if (track.isMaster)
        {
            for (int i = 0; i < outs.size(); ++i)
                output.addItem (outs[i], i + 1);
            output.setSelectedId (juce::jlimit (1, outs.size(), track.outputPair.load() + 1), juce::dontSendNotification);
        }
        else
        {
            output.addItem ("Master", 1);
            for (int i = 0; i < outs.size(); ++i)
                output.addItem ("Direct: " + outs[i], i + 2);
            const int pair = track.outputPair.load();
            output.setSelectedId (pair < 0 || pair >= outs.size() ? 1 : pair + 2, juce::dontSendNotification);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (track.isMaster ? panelLight.withAlpha (0.8f) : panelLight.withAlpha (0.35f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0.0f, 1.0f), 4.0f);
        g.setColour (track.colour);
        g.fillRect (0, 4, 3, getHeight() - 8);
        g.setColour (text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (track.name, nameArea, juce::Justification::centredLeft, true);

        if (track.isMaster)
        {
            g.setColour (textDim);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText ("all tracks set to Master  ->", inputArea, juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 4);
        nameArea = r.removeFromLeft (juce::jmin (150, r.getWidth() / 4));
        r.removeFromLeft (8);
        const int w = (r.getWidth() - 10) / 2;
        inputArea = r.removeFromLeft (w);
        input.setBounds (inputArea);
        r.removeFromLeft (10);
        output.setBounds (r);
    }

private:
    AudioEngine& engine;
    Track& track;
    juce::ComboBox input, output;
    juce::Rectangle<int> nameArea, inputArea;
};

//==============================================================================
IOPanel::IOPanel (AudioEngine& e) : engine (e)
{
    deviceLabel.setColour (juce::Label::textColourId, text);
    deviceLabel.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (deviceLabel);

    settingsButton.onClick = [this] { if (onOpenAudioSettings) onOpenAudioSettings(); };
    addAndMakeVisible (settingsButton);

    routesViewport.setViewedComponent (&routesHolder, false);
    routesViewport.setScrollBarsShown (true, false);
    routesViewport.setScrollBarThickness (8);
    addAndMakeVisible (routesViewport);

    engine.addChangeListener (this);
    engine.getDeviceManager().addChangeListener (this);
    rebuild();
    startTimerHz (20);
}

IOPanel::~IOPanel()
{
    engine.testTonePair = -1;
    engine.removeChangeListener (this);
    engine.getDeviceManager().removeChangeListener (this);
}

void IOPanel::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &engine.getDeviceManager())
        shownTracks.clear();   // force full refresh of the routing combos
    rebuild();
}

void IOPanel::rebuild()
{
    inputNames = engine.getActiveInputNames();
    outputPairs = engine.getOutputPairNames();

    if (auto* d = engine.getDeviceManager().getCurrentAudioDevice())
    {
        const double rate = d->getCurrentSampleRate();
        const double latency = 1000.0 * (d->getInputLatencyInSamples() + d->getOutputLatencyInSamples()) / juce::jmax (1.0, rate);
        deviceLabel.setText (d->getTypeName() + ":  " + d->getName() + "      " + juce::String (rate / 1000.0, 1) + " kHz      "
                             + juce::String (d->getCurrentBufferSizeSamples()) + " samples      "
                             + juce::String (latency, 1) + " ms round trip      "
                             + juce::String (inputNames.size()) + " in / " + juce::String (engine.getNumActiveOutputs() > 0 ? engine.getNumActiveOutputs() : outputPairs.size() * 2) + " out",
                             juce::dontSendNotification);
    }
    else
    {
        deviceLabel.setText ("No audio device open - choose one in Audio / ASIO settings", juce::dontSendNotification);
    }

    // Test tone buttons, one per output pair
    if (toneButtons.size() != outputPairs.size())
    {
        toneButtons.clear();
        for (int i = 0; i < outputPairs.size(); ++i)
        {
            auto* b = toneButtons.add (new juce::TextButton ("Test"));
            b->setClickingTogglesState (true);
            b->setTooltip ("Play a quiet 440 Hz tone on this output to check what's connected");
            b->setColour (juce::TextButton::buttonOnColourId, accent);
            b->onClick = [this, i]
            {
                const bool on = toneButtons[i]->getToggleState();
                for (int k = 0; k < toneButtons.size(); ++k)
                    if (k != i)
                        toneButtons[k]->setToggleState (false, juce::dontSendNotification);
                engine.testTonePair = on ? i : -1;
            };
            addAndMakeVisible (b);
        }
    }

    // Routing rows: every track + the master
    std::vector<Track*> current;
    for (int i = 0; i < engine.getNumTracks(); ++i)
        current.push_back (engine.getTrack (i));
    current.push_back (&engine.getMaster());

    if (current != shownTracks)
    {
        shownTracks = current;
        routes.clear();
        for (auto* t : current)
            routesHolder.addAndMakeVisible (routes.add (new RouteRow (engine, *t)));
    }
    else
    {
        for (auto* r : routes)
            r->refresh();
    }

    resized();
    repaint();
}

void IOPanel::timerCallback()
{
    repaint (inputsArea);
    repaint (outputsArea);
}

void IOPanel::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto heading = [&g] (juce::Rectangle<int> area, const juce::String& t)
    {
        g.setColour (textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (t, area.translated (0, -18).withHeight (16), juce::Justification::centredLeft);
    };

    heading (inputsArea, "INPUTS");
    heading (outputsArea, "OUTPUTS");
    heading (routesViewport.getBounds(), "ROUTING  -  TRACK / INPUT / OUTPUT");

    // Inputs: name, meter, which tracks use it
    g.setFont (juce::FontOptions (12.0f));
    auto r = inputsArea;
    if (inputNames.isEmpty())
    {
        g.setColour (textDim);
        g.drawText ("No inputs enabled", r.removeFromTop (lineHeight), juce::Justification::centredLeft);
    }
    for (int i = 0; i < inputNames.size() && r.getHeight() >= lineHeight; ++i)
    {
        auto line = r.removeFromTop (lineHeight);
        g.setColour (text);
        g.drawText ("In " + juce::String (i + 1), line.removeFromLeft (44), juce::Justification::centredLeft);
        g.setColour (textDim);
        g.drawText (inputNames[i], line.removeFromLeft (110), juce::Justification::centredLeft, true);
        auto meter = line.removeFromLeft (juce::jmax (40, line.getWidth() - 110)).withSizeKeepingCentre (juce::jmax (40, line.getWidth() - 110), 7);
        drawMeter (g, meter.toFloat(), engine.getInputLevel (i));

        juce::StringArray users;
        for (int k = 0; k < engine.getNumTracks(); ++k)
        {
            auto* t = engine.getTrack (k);
            if (t->inputIndex == i || (t->stereoInput && t->inputIndex + 1 == i))
                if (t->armed || t->monitor)
                    users.add (t->name);
        }
        g.setColour (record.withAlpha (0.9f));
        g.drawText (users.joinIntoString (", "), line.withTrimmedLeft (8), juce::Justification::centredLeft, true);
    }

    // Outputs: pair name, L/R meters (test buttons are components)
    r = outputsArea;
    for (int i = 0; i < outputPairs.size() && r.getHeight() >= lineHeight * 2; ++i)
    {
        auto line = r.removeFromTop (lineHeight * 2);
        auto label = line.removeFromLeft (150);
        g.setColour (text);
        g.drawText (outputPairs[i], label, juce::Justification::centredLeft, true);

        juce::StringArray users;
        if (engine.getMaster().outputPair.load() == i)
            users.add ("Master");
        for (int k = 0; k < engine.getNumTracks(); ++k)
            if (engine.getTrack (k)->outputPair.load() == i)
                users.add (engine.getTrack (k)->name);

        line.removeFromRight (60);
        auto meters = line.reduced (6, 10);
        auto top = meters.removeFromTop (meters.getHeight() / 2).reduced (0, 1);
        drawMeter (g, top.toFloat(), engine.getOutputLevel (i * 2));
        drawMeter (g, meters.reduced (0, 1).toFloat(), engine.getOutputLevel (i * 2 + 1));

        g.setColour (textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (users.joinIntoString (", "), label.translated (0, 12), juce::Justification::centredLeft, true);
        g.setFont (juce::FontOptions (12.0f));
    }
}

void IOPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 10);

    auto top = r.removeFromTop (26);
    settingsButton.setBounds (top.removeFromRight (190));
    deviceLabel.setBounds (top);
    r.removeFromTop (26);

    const int third = r.getWidth() / 3;
    inputsArea = r.removeFromLeft (third - 10);
    r.removeFromLeft (16);
    outputsArea = r.removeFromLeft (third - 10);
    r.removeFromLeft (16);
    routesViewport.setBounds (r);

    // Test tone buttons line up with their output rows
    for (int i = 0; i < toneButtons.size(); ++i)
    {
        const int y = outputsArea.getY() + i * lineHeight * 2 + 9;
        toneButtons[i]->setBounds (outputsArea.getRight() - 56, y, 54, 26);
        toneButtons[i]->setVisible (y + 26 <= outputsArea.getBottom());
    }

    const int rowH = 34;
    routesHolder.setSize (routesViewport.getMaximumVisibleWidth(), juce::jmax (r.getHeight(), routes.size() * rowH));
    for (int i = 0; i < routes.size(); ++i)
        routes[i]->setBounds (0, i * rowH, routesHolder.getWidth(), rowH);
}
