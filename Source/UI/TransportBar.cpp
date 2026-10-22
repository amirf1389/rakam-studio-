#include "UI/TransportBar.h"

using namespace Rakam::Colours;

namespace Icons
{
    juce::Path stroked (const juce::Path& p, float thickness = 1.6f)
    {
        juce::Path out;
        juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (out, p);
        return out;
    }

    juce::Path play()   { juce::Path p; p.addTriangle (1, 0, 1, 12, 11, 6); return p; }
    juce::Path stop()   { juce::Path p; p.addRoundedRectangle (0, 0, 11, 11, 1.5f); return p; }
    juce::Path record() { juce::Path p; p.addEllipse (0, 0, 12, 12); return p; }

    juce::Path rewind()
    {
        juce::Path p;
        p.addRectangle (0, 0, 1.8f, 12);
        p.addTriangle (12, 0, 12, 12, 2.5f, 6);
        return p;
    }

    juce::Path loop()
    {
        juce::Path p;
        p.addRoundedRectangle (0.0f, 2.0f, 14.0f, 9.0f, 3.5f);
        auto s = stroked (p);
        s.addTriangle (8.5f, -0.5f, 8.5f, 4.5f, 12.0f, 2.0f);
        s.addTriangle (5.5f, 8.5f, 5.5f, 13.5f, 2.0f, 11.0f);
        return s;
    }

    juce::Path metronome()
    {
        juce::Path p;
        p.startNewSubPath (3.0f, 13.0f);
        p.lineTo (5.5f, 0.0f);
        p.lineTo (8.5f, 0.0f);
        p.lineTo (11.0f, 13.0f);
        p.closeSubPath();
        auto s = stroked (p, 1.5f);
        juce::Path arm;
        arm.startNewSubPath (7.0f, 10.0f);
        arm.lineTo (11.5f, 2.5f);
        s.addPath (stroked (arm, 1.5f));
        return s;
    }

    juce::Path undo (bool mirrored)
    {
        juce::Path p;
        p.startNewSubPath (3.0f, 5.0f);
        p.cubicTo (8.0f, 1.0f, 14.0f, 4.0f, 13.0f, 9.0f);
        p.cubicTo (12.5f, 11.5f, 10.0f, 13.0f, 7.0f, 13.0f);
        auto s = stroked (p, 1.7f);
        s.addTriangle (0.0f, 6.0f, 6.0f, 1.5f, 6.5f, 8.0f);
        if (mirrored)
            s.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f).translated (14.0f, 0.0f));
        return s;
    }

    juce::Path sidebar()
    {
        juce::Path p;
        p.addRoundedRectangle (0.0f, 0.0f, 14.0f, 12.0f, 2.0f);
        auto s = stroked (p, 1.4f);
        s.addRectangle (4.0f, 0.0f, 1.4f, 12.0f);
        return s;
    }

    std::unique_ptr<juce::DrawableButton> button (const juce::String& tooltip, const juce::Path& icon, juce::Colour colour,
                                                  bool toggles = false)
    {
        auto make = [&icon] (juce::Colour c)
        {
            auto d = std::make_unique<juce::DrawablePath>();
            d->setPath (icon);
            d->setFill (c);
            return d;
        };

        auto normal = make (colour), over = make (colour.brighter (0.35f)), down = make (colour.darker (0.2f));
        auto on = make (toggles ? accent : colour);

        auto b = std::make_unique<juce::DrawableButton> (tooltip, juce::DrawableButton::ImageOnButtonBackground);
        b->setImages (normal.get(), over.get(), down.get(), nullptr, on.get(), on.get(), on.get());
        b->setTooltip (tooltip);
        b->setEdgeIndent (10);
        b->setColour (juce::DrawableButton::backgroundColourId, panelLight);
        b->setColour (juce::TextButton::buttonColourId, panelLight);
        b->setColour (juce::TextButton::buttonOnColourId, toggles ? juce::Colour (0xff3a2a24) : panelLight.brighter (0.15f));
        b->setColour (juce::DrawableButton::backgroundOnColourId, toggles ? juce::Colour (0xff3a2a24) : panelLight.brighter (0.15f));
        b->setClickingTogglesState (false);
        return b;
    }
}

TransportBar::TransportBar (AudioEngine& e) : engine (e)
{
    browserButton   = Icons::button ("Show / hide browser (B)", Icons::sidebar(), text, true);
    rewindButton    = Icons::button ("Go to start (Home)", Icons::rewind(), text);
    playButton      = Icons::button ("Play / Pause (Space)", Icons::play(), text, true);
    stopButton      = Icons::button ("Stop - press twice to return to start", Icons::stop(), text);
    recordButton    = Icons::button ("Record armed tracks (R)", Icons::record(), record);
    loopButton      = Icons::button ("Loop - drag in the ruler's loop strip to set the range (L)", Icons::loop(), text, true);
    metronomeButton = Icons::button ("Metronome", Icons::metronome(), text, true);
    undoButton      = Icons::button ("Undo (Ctrl+Z)", Icons::undo (false), text);
    redoButton      = Icons::button ("Redo (Ctrl+Y)", Icons::undo (true), text);

    for (auto* b : { browserButton.get(), rewindButton.get(), playButton.get(), stopButton.get(), recordButton.get(),
                     loopButton.get(), metronomeButton.get(), undoButton.get(), redoButton.get() })
        addAndMakeVisible (b);

    recordButton->setColour (juce::DrawableButton::backgroundOnColourId, record.withAlpha (0.4f));
    recordButton->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4a2224));

    browserButton->onClick   = [this] { if (onToggleBrowser) onToggleBrowser(); };
    rewindButton->onClick    = [this] { engine.setPositionSec (engine.loopEnabled ? engine.loopStartSec.load() : 0.0); };
    playButton->onClick      = [this] { engine.togglePlay(); };
    stopButton->onClick      = [this] { engine.stop(); };
    recordButton->onClick    = [this] { if (engine.isRecording()) engine.stop(); else if (onRecord) onRecord(); };
    loopButton->onClick      = [this] { if (onToggleLoop) onToggleLoop(); };
    metronomeButton->onClick = [this] { engine.metronomeOn = ! engine.metronomeOn.load(); };
    undoButton->onClick      = [this] { if (onUndo) onUndo(); };
    redoButton->onClick      = [this] { if (onRedo) onRedo(); };

    auto setupLabel = [this] (juce::Label& l, float size, juce::Colour c, bool bold)
    {
        l.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, bold ? juce::Font::bold : juce::Font::plain));
        l.setColour (juce::Label::textColourId, c);
        l.setJustificationType (juce::Justification::centred);
        l.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (l);
    };
    setupLabel (barsLabel, 21.0f, text, true);
    setupLabel (timeLabel, 12.0f, textDim, false);

    bpmLabel.setText ("BPM", juce::dontSendNotification);
    bpmLabel.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    bpmLabel.setColour (juce::Label::textColourId, textDim);
    bpmLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (bpmLabel);

    bpmSlider.setSliderStyle (juce::Slider::LinearBarVertical);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 24);
    bpmSlider.setRange (20.0, 300.0, 0.1);
    bpmSlider.setValue (engine.bpm.load(), juce::dontSendNotification);
    bpmSlider.setColour (juce::Slider::trackColourId, juce::Colours::transparentBlack);
    bpmSlider.setColour (juce::Slider::backgroundColourId, background);
    bpmSlider.setColour (juce::Slider::textBoxTextColourId, accent);
    bpmSlider.setTooltip ("Tempo: drag up/down or double-click to type");
    bpmSlider.setVelocityBasedMode (true);
    bpmSlider.onValueChange = [this] { engine.bpm = bpmSlider.getValue(); engine.markEdited(); };
    addAndMakeVisible (bpmSlider);

    addTrackButton.onClick = [this] { if (onAddTrack) onAddTrack(); };
    mixerButton.onClick    = [this] { if (onToggleMixer) onToggleMixer(); };
    separateButton.onClick = [this] { if (onSeparate) onSeparate(); };
    exportButton.onClick   = [this] { if (onExport) onExport(); };
    settingsButton.onClick = [this] { if (onSettings) onSettings(); };

    mixerButton.setClickingTogglesState (false);
    mixerButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a2a24));
    mixerButton.setColour (juce::TextButton::textColourOnId, accent);
    settingsButton.setTooltip ("Audio / ASIO settings");
    separateButton.setTooltip ("Split a song into vocals / drums / bass / other with AI");
    exportButton.setTooltip ("Export the full mix and every track as stems (Ctrl+E)");
    exportButton.setColour (juce::TextButton::buttonColourId, accent);

    for (auto* b : { &addTrackButton, &mixerButton, &separateButton, &exportButton, &settingsButton })
        addAndMakeVisible (b);

    startTimerHz (20);
}

void TransportBar::setPanelStates (bool browserVisible, bool mixerVisible)
{
    browserButton->setToggleState (browserVisible, juce::dontSendNotification);
    mixerButton.setToggleState (mixerVisible, juce::dontSendNotification);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    g.setColour (background);
    g.fillRoundedRectangle (timeWell.toFloat(), 6.0f);
    g.setColour (gridStrong);
    g.drawRoundedRectangle (timeWell.toFloat().reduced (0.5f), 6.0f, 1.0f);

    g.setColour (grid);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    const int b = r.getHeight();

    auto place = [&r, b] (juce::Component& c, int width = -1, int gap = 4)
    {
        c.setBounds (r.removeFromLeft (width < 0 ? b : width));
        r.removeFromLeft (gap);
    };

    place (*browserButton, -1, 14);
    place (*rewindButton);
    place (*playButton);
    place (*stopButton);
    place (*recordButton, -1, 10);
    place (*loopButton);
    place (*metronomeButton, -1, 14);

    timeWell = r.removeFromLeft (168);
    auto tw = timeWell.reduced (6, 2);
    barsLabel.setBounds (tw.removeFromTop (tw.getHeight() * 3 / 5).translated (0, 2));
    timeLabel.setBounds (tw);
    r.removeFromLeft (14);

    bpmLabel.setBounds (r.removeFromLeft (30));
    r.removeFromLeft (4);
    bpmSlider.setBounds (r.removeFromLeft (64));
    r.removeFromLeft (14);

    place (*undoButton);
    place (*redoButton);

    exportButton.setBounds (r.removeFromRight (96));
    r.removeFromRight (6);
    separateButton.setBounds (r.removeFromRight (84));
    r.removeFromRight (6);
    mixerButton.setBounds (r.removeFromRight (66));
    r.removeFromRight (6);
    addTrackButton.setBounds (r.removeFromRight (74));
    r.removeFromRight (6);
    settingsButton.setBounds (r.removeFromRight (66));
}

void TransportBar::timerCallback()
{
    const double secs = engine.getPositionSec();
    const double beats = secs * engine.bpm.load() / 60.0;
    const int bar = (int) (beats / 4.0) + 1;
    const int beat = (int) std::fmod (beats, 4.0) + 1;
    const int tick = (int) (std::fmod (beats, 1.0) * 100.0);

    if (secs < 0.0)
    {
        barsLabel.setText ("COUNT-IN", juce::dontSendNotification);
        timeLabel.setText (juce::String ((int) std::ceil (-beats)) + " beats", juce::dontSendNotification);
    }
    else
    {
        barsLabel.setText (juce::String (bar).paddedLeft (' ', 3) + " . " + juce::String (beat) + " . " + juce::String (tick).paddedLeft ('0', 2),
                           juce::dontSendNotification);

        const int mins = (int) (secs / 60.0);
        timeLabel.setText (juce::String (mins).paddedLeft ('0', 2) + ":" + juce::String (std::fmod (secs, 60.0), 3).paddedLeft ('0', 6),
                           juce::dontSendNotification);
    }

    if (! bpmSlider.isMouseButtonDown() && std::abs (bpmSlider.getValue() - engine.bpm.load()) > 0.01)
        bpmSlider.setValue (engine.bpm.load(), juce::dontSendNotification);

    metronomeButton->setToggleState (engine.metronomeOn.load(), juce::dontSendNotification);
    loopButton->setToggleState (engine.loopEnabled.load(), juce::dontSendNotification);
    undoButton->setEnabled (engine.getUndoManager().canUndo());
    redoButton->setEnabled (engine.getUndoManager().canRedo());

    if (engine.isPlaying() != lastPlaying || engine.isRecording() != lastRecording)
    {
        lastPlaying = engine.isPlaying();
        lastRecording = engine.isRecording();
        playButton->setToggleState (lastPlaying, juce::dontSendNotification);
        recordButton->setToggleState (lastRecording, juce::dontSendNotification);
        barsLabel.setColour (juce::Label::textColourId, lastRecording ? record : lastPlaying ? accent : text);
    }
}
