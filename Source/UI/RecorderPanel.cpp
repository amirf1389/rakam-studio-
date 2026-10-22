#include "UI/RecorderPanel.h"

using namespace Rakam::Colours;

namespace
{
    juce::String modeHelpText (AudioEngine::RecordMode m)
    {
        switch (m)
        {
            case AudioEngine::RecordMode::replace:
                return "New recordings replace whatever was on the track at that spot.";
            case AudioEngine::RecordMode::punch:
                return "Plays from a bar before the loop region and only records inside it - perfect for fixing one line.";
            case AudioEngine::RecordMode::loopTakes:
                return "Cycles the loop region and keeps every pass as a take. Right-click the clip > Takes to pick the best one.";
            case AudioEngine::RecordMode::normal:
                break;
        }
        return "New recordings are layered on top of what's already there.";
    }

    juce::String formatName (int f)
    {
        switch (f)
        {
            case 2:  return "WAV 32-bit float";
            case 3:  return "WAV 16-bit";
            case 4:  return "FLAC 24-bit";
            default: return "WAV 24-bit";
        }
    }

    void drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float level)
    {
        g.setColour (background);
        g.fillRoundedRectangle (r, 2.0f);
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float w = r.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setColour (level > 0.98f ? record : level > 0.5f ? mute : solo);
        g.fillRoundedRectangle (r.withWidth (w), 2.0f);
    }
}

//==============================================================================
class RecorderPanel::TrackRow : public juce::Component
{
public:
    TrackRow (AudioEngine& e, Track& t) : track (t), engine (e)
    {
        arm.setButtonText ("R");
        arm.setClickingTogglesState (true);
        arm.setColour (juce::TextButton::buttonOnColourId, record);
        arm.setTooltip ("Arm this track for recording");
        arm.onClick = [this]
        {
            track.armed = arm.getToggleState();
            if (engine.getSettings().getBoolValue (Settings::autoMonitor, false))
                track.monitor = track.armed.load();
            engine.sendChangeMessage();
        };
        addAndMakeVisible (arm);

        mon.setButtonText ("I");
        mon.setClickingTogglesState (true);
        mon.setColour (juce::TextButton::buttonOnColourId, monitor);
        mon.setTooltip ("Input monitoring - hear yourself through the track (use headphones)");
        mon.onClick = [this] { track.monitor = mon.getToggleState(); engine.sendChangeMessage(); };
        addAndMakeVisible (mon);

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
        refresh();
    }

    void refresh()
    {
        arm.setToggleState (track.armed.load(), juce::dontSendNotification);
        mon.setToggleState (track.monitor.load(), juce::dontSendNotification);

        input.clear (juce::dontSendNotification);
        const auto names = engine.getActiveInputNames();
        if (names.isEmpty())
        {
            input.setTextWhenNothingSelected ("No inputs enabled");
            return;
        }
        for (int i = 0; i < names.size(); ++i)
            input.addItem ("In " + juce::String (i + 1) + "  " + names[i], i + 1);
        for (int i = 0; i + 1 < names.size(); i += 2)
            input.addItem ("In " + juce::String (i + 1) + "+" + juce::String (i + 2) + " stereo", 1000 + i);
        const int wanted = track.stereoInput ? 1000 + track.inputIndex : track.inputIndex + 1;
        input.setSelectedId (input.indexOfItemId (wanted) >= 0 ? wanted : 1, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (track.armed.load() ? record.withAlpha (0.08f) : panelLight.withAlpha (0.4f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0.0f, 1.0f), 4.0f);
        g.setColour (track.colour);
        g.fillRect (0, 4, 3, getHeight() - 8);

        g.setColour (text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (track.name, nameArea, juce::Justification::centredLeft, true);

        float level = engine.getInputLevel (track.inputIndex);
        if (track.stereoInput)
            level = juce::jmax (level, engine.getInputLevel (track.inputIndex + 1));
        drawMeter (g, meterArea.toFloat(), level);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 5);
        arm.setBounds (r.removeFromLeft (28));
        r.removeFromLeft (4);
        mon.setBounds (r.removeFromLeft (28));
        r.removeFromLeft (10);
        nameArea = r.removeFromLeft (juce::jmin (150, r.getWidth() / 3));
        r.removeFromLeft (8);
        input.setBounds (r.removeFromLeft (juce::jmin (200, r.getWidth() / 2)));
        r.removeFromLeft (10);
        meterArea = r.withSizeKeepingCentre (r.getWidth(), 8);
    }

    Track& track;

private:
    AudioEngine& engine;
    juce::TextButton arm, mon;
    juce::ComboBox input;
    juce::Rectangle<int> nameArea, meterArea;
};

//==============================================================================
RecorderPanel::RecorderPanel (AudioEngine& e) : engine (e)
{
    for (int m = 1; m <= 4; ++m)
        modeBox.addItem (AudioEngine::getRecordModeName ((AudioEngine::RecordMode) m), m);
    modeBox.onChange = [this]
    {
        engine.getSettings().setValue (Settings::recordMode, modeBox.getSelectedId());
        modeHelp.setText (modeHelpText ((AudioEngine::RecordMode) modeBox.getSelectedId()), juce::dontSendNotification);
    };

    countInBox.addItem ("Off", 1);
    countInBox.addItem ("1 bar", 2);
    countInBox.addItem ("2 bars", 3);
    countInBox.onChange = [this] { engine.getSettings().setValue (Settings::countInBars, countInBox.getSelectedId() - 1); };

    modeHelp.setColour (juce::Label::textColourId, textDim);
    modeHelp.setFont (juce::FontOptions (12.0f));
    modeHelp.setJustificationType (juce::Justification::topLeft);
    formatValue.setColour (juce::Label::textColourId, text);
    diskLabel.setColour (juce::Label::textColourId, textDim);
    diskLabel.setFont (juce::FontOptions (12.0f));

    recordButton.setColour (juce::TextButton::buttonColourId, record);
    recordButton.onClick = [this] { if (engine.isRecording()) engine.stop(); else if (onRecord) onRecord(); };
    stopButton.onClick = [this] { engine.stop(); };
    folderButton.onClick = [this]
    {
        auto f = engine.getRecordingFolder();
        f.createDirectory();
        f.startAsProcess();
    };
    ioButton.onClick = [this] { if (onOpenIO) onOpenIO(); };

    for (auto* c : std::initializer_list<juce::Component*> { &modeLabel, &countInLabel, &formatLabel, &formatValue, &modeHelp, &diskLabel,
                                                             &modeBox, &countInBox, &recordButton, &stopButton, &folderButton, &ioButton })
        addAndMakeVisible (c);

    for (auto* l : { &modeLabel, &countInLabel, &formatLabel })
        l->setColour (juce::Label::textColourId, textDim);

    rowsViewport.setViewedComponent (&rowsHolder, false);
    rowsViewport.setScrollBarsShown (true, false);
    rowsViewport.setScrollBarThickness (8);
    addAndMakeVisible (rowsViewport);

    engine.addChangeListener (this);
    engine.getDeviceManager().addChangeListener (this);
    rebuildRows();
    updateDiskInfo();
    startTimerHz (15);
}

RecorderPanel::~RecorderPanel()
{
    engine.removeChangeListener (this);
    engine.getDeviceManager().removeChangeListener (this);
}

void RecorderPanel::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &engine.getDeviceManager())
        for (auto* r : rows)
            r->refresh();
    rebuildRows();
}

void RecorderPanel::rebuildRows()
{
    std::vector<Track*> current;
    for (int i = 0; i < engine.getNumTracks(); ++i)
        current.push_back (engine.getTrack (i));

    if (current != shownTracks)
    {
        shownTracks = current;
        rows.clear();
        for (auto* t : current)
            rowsHolder.addAndMakeVisible (rows.add (new TrackRow (engine, *t)));
        resized();
    }
    else
    {
        for (auto* r : rows)
            r->refresh();
    }

    modeBox.setSelectedId ((int) engine.getRecordMode(), juce::dontSendNotification);
    modeHelp.setText (modeHelpText (engine.getRecordMode()), juce::dontSendNotification);
    countInBox.setSelectedId (Settings::getInt (engine.getSettings(), Settings::countInBars, 0) + 1, juce::dontSendNotification);
    formatValue.setText (formatName (Settings::getInt (engine.getSettings(), Settings::recordFormat, 1)) + "   (change in Settings)",
                         juce::dontSendNotification);
    repaint();
}

void RecorderPanel::updateDiskInfo()
{
    auto folder = engine.getRecordingFolder();
    auto probe = folder;
    while (! probe.exists() && probe.getParentDirectory() != probe)
        probe = probe.getParentDirectory();

    const auto freeBytes = probe.getBytesFreeOnVolume();
    int armedChannels = 0;
    for (int i = 0; i < engine.getNumTracks(); ++i)
        if (auto* t = engine.getTrack (i); t->armed)
            armedChannels += t->stereoInput ? 2 : 1;

    const int format = Settings::getInt (engine.getSettings(), Settings::recordFormat, 1);
    const double bytesPerSample = format == 2 ? 4.0 : format == 3 ? 2.0 : format == 4 ? 1.8 : 3.0;
    const double bytesPerSecond = engine.getSampleRate() * bytesPerSample * juce::jmax (1, armedChannels);
    const double hours = (double) freeBytes / bytesPerSecond / 3600.0;

    diskLabel.setText ("Free space: " + juce::File::descriptionOfSizeInBytes (freeBytes)
                       + (armedChannels > 0 ? "   (about " + (hours < 10.0 ? juce::String (hours, 1) : juce::String (juce::roundToInt (hours))) + " hours of recording)" : juce::String()),
                       juce::dontSendNotification);
}

void RecorderPanel::timerCallback()
{
    const bool rec = engine.isRecording();
    const double now = engine.getPositionSec();

    if (rec && engine.isCountingIn())
    {
        statusTitle = engine.getActiveRecordMode() == AudioEngine::RecordMode::punch ? "PRE-ROLL" : "COUNT-IN";
        const double beats = (engine.getRecordStartSec() - now) * engine.bpm.load() / 60.0;
        statusTime = juce::String ((int) std::ceil (beats));
        statusDetail = "beats until recording starts";
        statusColour = mute;
    }
    else if (rec)
    {
        const auto mode = engine.getActiveRecordMode();
        statusTitle = "RECORDING";
        double elapsed = now - engine.getRecordStartSec();
        if (mode == AudioEngine::RecordMode::loopTakes)
            elapsed = juce::jmax (0.0, now - engine.loopStartSec.load());
        const int mins = (int) (elapsed / 60.0);
        statusTime = juce::String (mins).paddedLeft ('0', 2) + ":" + juce::String (std::fmod (juce::jmax (0.0, elapsed), 60.0), 1).paddedLeft ('0', 4);

        int armed = 0;
        for (int i = 0; i < engine.getNumTracks(); ++i)
            if (engine.getTrack (i)->armed)
                ++armed;

        statusDetail = AudioEngine::getRecordModeName (mode) + "  -  " + juce::String (armed) + " track(s)";
        if (mode == AudioEngine::RecordMode::loopTakes)
            statusDetail = "Take " + juce::String (engine.getCurrentTakeNumber()) + "  -  " + statusDetail;
        if (mode == AudioEngine::RecordMode::punch && now > engine.loopEndSec.load())
            statusDetail = "Punched out - press Stop";
        statusColour = record;
    }
    else
    {
        int armed = 0;
        for (int i = 0; i < engine.getNumTracks(); ++i)
            if (engine.getTrack (i)->armed)
                ++armed;

        statusTitle = armed > 0 ? "READY" : "NO TRACK ARMED";
        statusTime = juce::String (armed);
        statusDetail = armed > 0 ? "armed track(s) - press Record or R" : "press R on a track below to arm it";
        statusColour = armed > 0 ? solo : textDim;
    }

    recordButton.setButtonText (rec ? "Stop recording" : "Record");

    if (++diskCounter % 30 == 0)
        updateDiskInfo();

    repaint (statusArea);
    for (auto* r : rows)
        r->repaint();
}

void RecorderPanel::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto s = statusArea.toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (s, 8.0f);
    g.setColour (statusColour.withAlpha (0.8f));
    g.drawRoundedRectangle (s.reduced (0.5f), 8.0f, 1.5f);

    auto r = statusArea.reduced (14, 10);
    g.setColour (statusColour);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (statusTitle, r.removeFromTop (18), juce::Justification::centredLeft);
    g.setColour (text);
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 34.0f, juce::Font::bold));
    g.drawText (statusTime, r.removeFromTop (44), juce::Justification::centredLeft);
    g.setColour (textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText (statusDetail, r, juce::Justification::topLeft, 2);

    g.setColour (textDim);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("TRACKS  -  ARM / MONITOR / INPUT / LEVEL", rowsViewport.getBounds().translated (0, -18).withHeight (16),
                juce::Justification::centredLeft);

    if (rows.isEmpty())
    {
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Add a track to record (+ Track)", rowsViewport.getBounds(), juce::Justification::centred);
    }
}

void RecorderPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 10);

    // Left column: settings
    auto left = r.removeFromLeft (300);
    auto row = [&left] (int h = 26) { auto x = left.removeFromTop (h); left.removeFromTop (6); return x; };
    {
        auto x = row();
        modeLabel.setBounds (x.removeFromLeft (90));
        modeBox.setBounds (x);
    }
    modeHelp.setBounds (row (44).withTrimmedLeft (90));
    {
        auto x = row();
        countInLabel.setBounds (x.removeFromLeft (90));
        countInBox.setBounds (x.removeFromLeft (110));
    }
    {
        auto x = row();
        formatLabel.setBounds (x.removeFromLeft (90));
        formatValue.setBounds (x);
    }
    diskLabel.setBounds (row (34));
    {
        auto x = row (28);
        folderButton.setBounds (x.removeFromLeft (140));
        x.removeFromLeft (6);
        ioButton.setBounds (x);
    }

    r.removeFromLeft (16);

    // Middle: status + buttons
    auto middle = r.removeFromLeft (230);
    statusArea = middle.removeFromTop (juce::jmin (130, middle.getHeight() - 44));
    middle.removeFromTop (8);
    auto buttons = middle.removeFromTop (34);
    recordButton.setBounds (buttons.removeFromLeft (140));
    buttons.removeFromLeft (6);
    stopButton.setBounds (buttons);

    r.removeFromLeft (16);

    // Right: tracks
    r.removeFromTop (20);
    rowsViewport.setBounds (r);
    const int rowH = 36;
    rowsHolder.setSize (rowsViewport.getMaximumVisibleWidth(), juce::jmax (r.getHeight(), rows.size() * rowH));
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowH, rowsHolder.getWidth(), rowH);
}
