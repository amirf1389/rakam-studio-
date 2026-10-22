#include "UI/ArrangementView.h"

using namespace Rakam::Colours;

namespace
{
    void showOkCancel (const juce::String& title, const juce::String& message, const juce::String& okText,
                       std::function<void()> onOk)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon,
                                                                                     title, message, okText, "Cancel"),
                                      [onOk] (int result) { if (result == 1 && onOk) onOk(); });
    }
}

//==============================================================================
class ArrangementView::Playhead : public juce::Component
{
public:
    Playhead() { setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics& g) override
    {
        const float cx = (float) getWidth() * 0.5f;
        g.setColour (colour);
        g.fillRect (cx - 0.75f, 0.0f, 1.5f, (float) getHeight());

        juce::Path head;
        head.addTriangle (cx - 6.0f, 0.0f, cx + 6.0f, 0.0f, cx, 9.0f);
        g.fillPath (head);
    }

    juce::Colour colour { accent };
};

//==============================================================================
class ArrangementView::Ruler : public juce::Component
{
public:
    explicit Ruler (ArrangementView& v) : view (v) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (panel);
        auto r = getLocalBounds();
        auto strip = r.removeFromBottom (loopStripHeight);
        view.drawGrid (g, r, true);

        // Loop strip
        g.setColour (background.brighter (0.03f));
        g.fillRect (strip);

        const float x1 = view.secToX (view.engine.loopStartSec.load());
        const float x2 = view.secToX (view.engine.loopEndSec.load());
        if (x2 > x1)
        {
            const bool on = view.engine.loopEnabled.load();
            auto region = juce::Rectangle<float> (x1, (float) strip.getY() + 2.0f, x2 - x1, (float) strip.getHeight() - 4.0f);
            g.setColour (on ? accent.withAlpha (0.85f) : gridStrong);
            g.fillRoundedRectangle (region, 3.0f);
        }

        g.setColour (grid);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        inLoopStrip = e.y >= getHeight() - loopStripHeight;

        if (! inLoopStrip)
        {
            setPos (e);
            return;
        }

        const double sec = view.xToSec ((float) e.x);
        origStart = view.engine.loopStartSec.load();
        origEnd = view.engine.loopEndSec.load();
        movingLoop = sec > origStart && sec < origEnd && origEnd > origStart;
        anchor = view.snap (sec, ! e.mods.isAltDown());
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! inLoopStrip)
        {
            setPos (e);
            return;
        }

        const double sec = view.snap (view.xToSec ((float) e.x), ! e.mods.isAltDown());

        if (movingLoop)
        {
            const double delta = sec - anchor;
            const double len = origEnd - origStart;
            const double start = juce::jmax (0.0, origStart + delta);
            view.engine.loopStartSec = start;
            view.engine.loopEndSec = start + len;
        }
        else if (std::abs (e.getDistanceFromDragStartX()) > 3)
        {
            view.engine.loopStartSec = juce::jmax (0.0, juce::jmin (anchor, sec));
            view.engine.loopEndSec = juce::jmax (anchor, sec);
            view.engine.loopEnabled = true;
        }

        view.engine.markEdited();
        repaint();
        view.repaintLanes();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.y >= getHeight() - loopStripHeight)
        {
            view.engine.loopEnabled = ! view.engine.loopEnabled.load();
            repaint();
            view.repaintLanes();
        }
    }

private:
    void setPos (const juce::MouseEvent& e)
    {
        view.engine.setPositionSec (view.snap (view.xToSec ((float) e.x), ! e.mods.isAltDown()));
    }

    ArrangementView& view;
    bool inLoopStrip = false, movingLoop = false;
    double origStart = 0.0, origEnd = 0.0, anchor = 0.0;
};
//==============================================================================
/** The Structure lane: coloured blocks for Intro / Verse / Chorus... under the ruler. */
class ArrangementView::SectionLane : public juce::Component
{
public:
    explicit SectionLane (ArrangementView& v) : view (v) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (background.brighter (0.015f));
        auto& secs = view.engine.sections;

        if (secs.empty())
        {
            g.setColour (textDim.withAlpha (0.7f));
            g.setFont (juce::FontOptions (11.5f));
            g.drawText ("Song structure: right-click or press \"+ Section\" to mark Intro, Verse, Chorus...",
                        getLocalBounds().reduced (10, 0), juce::Justification::centredLeft, true);
        }

        for (int i = 0; i < (int) secs.size(); ++i)
        {
            auto& s = secs[(size_t) i];
            auto r = blockFor (s);
            if (r.getRight() < 0.0f || r.getX() > (float) getWidth())
                continue;

            const bool selected = i == view.selectedSection;
            g.setColour (s.colour.withAlpha (selected ? 0.95f : 0.75f));
            g.fillRoundedRectangle (r, 4.0f);
            if (selected)
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (r, 4.0f, 1.5f);
            }

            g.setColour (background);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            const auto bars = s.length() * view.engine.bpm.load() / 240.0;
            auto text = s.name;
            if (r.getWidth() > 90.0f)
                text << "   " << juce::String (bars, std::abs (bars - std::round (bars)) < 0.01 ? 0 : 1) << " bars";
            g.drawText (text, r.reduced (7.0f, 0.0f), juce::Justification::centredLeft, true);
        }

        g.setColour (grid);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        int edge = 0;
        hit (e.position, edge);
        setMouseCursor (edge != 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragEdge = 0;
        dragIndex = hit (e.position, dragEdge);
        view.selectedSection = dragIndex;
        repaint();

        const double sec = view.xToSec (e.position.x);

        if (e.mods.isPopupMenu())
        {
            dragEdge = 0;
            showMenu (dragIndex, sec);
            return;
        }

        if (dragIndex >= 0)
        {
            original = view.engine.sections[(size_t) dragIndex];
            if (dragEdge == 0 && ! view.engine.isRecording())
                view.engine.setPositionSec (original.startSec);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragIndex < 0 || dragEdge == 0)
            return;

        auto s = original;
        const double t = view.snap (view.xToSec (e.position.x), ! e.mods.isAltDown());
        if (dragEdge < 0) s.startSec = juce::jlimit (0.0, s.endSec - 0.1, t);
        else              s.endSec   = juce::jmax (s.startSec + 0.1, t);

        // Live preview while dragging; committed (undoable) on mouse up
        view.engine.sections[(size_t) dragIndex] = s;
        repaint();
        view.repaintLanes();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragIndex >= 0 && dragEdge != 0)
        {
            auto finalValue = view.engine.sections[(size_t) dragIndex];
            view.engine.sections[(size_t) dragIndex] = original;
            if (! (finalValue == original))
                view.engine.setSection (dragIndex, finalValue);
        }
        dragEdge = 0;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        int edge = 0;
        const int index = hit (e.position, edge);
        if (index >= 0)
            view.renameSection (index);
        else
            view.addSectionAt (view.xToSec (e.position.x));
    }

private:
    juce::Rectangle<float> blockFor (const Section& s) const
    {
        const float x1 = view.secToX (s.startSec), x2 = view.secToX (s.endSec);
        return { x1 + 1.0f, 3.0f, juce::jmax (4.0f, x2 - x1 - 2.0f), (float) getHeight() - 7.0f };
    }

    int hit (juce::Point<float> p, int& edge) const
    {
        auto& secs = view.engine.sections;
        for (int i = (int) secs.size(); --i >= 0;)
        {
            auto r = blockFor (secs[(size_t) i]).expanded (3.0f, 0.0f);
            if (! r.contains (p))
                continue;
            edge = p.x < r.getX() + 7.0f ? -1 : p.x > r.getRight() - 7.0f ? 1 : 0;
            return i;
        }
        edge = 0;
        return -1;
    }

    void showMenu (int index, double atSec)
    {
        auto& engine = view.engine;
        juce::PopupMenu m;

        if (index >= 0)
        {
            const auto s = engine.sections[(size_t) index];
            m.addSectionHeader (s.name);
            m.addItem ("Rename...", [this, index] { view.renameSection (index); });

            juce::PopupMenu names;
            for (auto n : { "Intro", "Verse", "Pre-Chorus", "Chorus", "Hook", "Bridge", "Drop", "Breakdown", "Solo", "Outro" })
                names.addItem (n, [&engine, index, n]
                {
                    auto sec = engine.sections[(size_t) index];
                    sec.name = n;
                    sec.colour = AudioEngine::sectionColour (n);
                    engine.setSection (index, sec);
                });
            m.addSubMenu ("Quick name", names);

            m.addItem ("Loop this section", [&engine, s]
            {
                engine.loopStartSec = s.startSec;
                engine.loopEndSec = s.endSec;
                engine.loopEnabled = true;
                engine.setPositionSec (s.startSec);
            });
            m.addItem ("Select its length for recording (punch / takes)", [&engine, s]
            {
                engine.loopStartSec = s.startSec;
                engine.loopEndSec = s.endSec;
                engine.sendChangeMessage();
            });
            m.addSeparator();
            m.addItem ("Duplicate section (copy its audio after it)", [&engine, index] { engine.duplicateSection (index); });
            m.addItem ("Delete section and its audio (close the gap)", [&engine, index] { engine.deleteSectionWithAudio (index); });
            m.addItem ("Remove marker only (keep audio)", [&engine, index] { engine.removeSection (index); });
        }
        else
        {
            m.addItem ("Add section here", [this, atSec] { view.addSectionAt (atSec); });
        }

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    ArrangementView& view;
    int dragIndex = -1, dragEdge = 0;
    Section original;
};

//==============================================================================
class ArrangementView::TrackHeader : public juce::Component
{
public:
    TrackHeader (ArrangementView& v, Track& t) : view (v), track (t)
    {
        name.setText (track.name, juce::dontSendNotification);
        name.setEditable (false, true);
        name.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        name.setTooltip ("Double-click to rename");
        name.onTextChange = [this]
        {
            track.name = name.getText().trim().isEmpty() ? juce::String ("Track") : name.getText().trim();
            view.engine.markEdited();
            view.engine.sendChangeMessage();
        };
        addAndMakeVisible (name);

        setupToggle (muteButton,    "M", mute,    "Mute",                 track.mute);
        setupToggle (soloButton,    "S", solo,    "Solo",                 track.solo);
        setupToggle (armButton,     "R", record,  "Arm for recording",    track.armed);
        setupToggle (monitorButton, "I", monitor, "Input monitoring (hear your input through this track's effects)", track.monitor);

        // Optionally turn on input monitoring together with record-arm (Settings > Recording)
        armButton.onClick = [this]
        {
            track.armed = armButton.getToggleState();
            if (view.engine.getSettings().getBoolValue (Settings::autoMonitor, false))
                track.monitor = track.armed.load();
            view.engine.markEdited();
            view.engine.sendChangeMessage();
        };

        input.setTooltip ("Recording input");
        input.onChange = [this]
        {
            const int id = input.getSelectedId();
            if (id <= 0)
                return;
            track.stereoInput = id >= 1000;
            track.inputIndex  = track.stereoInput ? id - 1000 : id - 1;
            view.engine.markEdited();
        };
        addAndMakeVisible (input);
        refreshInputs();
    }

    void refreshInputs()
    {
        input.clear (juce::dontSendNotification);
        input.setTextWhenNothingSelected ("No input");
        auto* device = view.engine.getDeviceManager().getCurrentAudioDevice();

        if (device == nullptr)
        {
            return;
        }

        const auto names  = device->getInputChannelNames();
        const auto active = device->getActiveInputChannels();
        juce::StringArray activeNames;

        for (int i = 0; i < names.size(); ++i)
            if (active[i])
                activeNames.add (names[i]);

        if (activeNames.isEmpty())
        {
            input.setTextWhenNothingSelected ("No inputs enabled");
            return;
        }

        for (int i = 0; i < activeNames.size(); ++i)
            input.addItem ("In " + juce::String (i + 1) + "  (" + activeNames[i] + ")", i + 1);

        for (int i = 0; i + 1 < activeNames.size(); i += 2)
            input.addItem ("In " + juce::String (i + 1) + "+" + juce::String (i + 2) + " stereo", 1000 + i);

        const int wanted = track.stereoInput ? 1000 + track.inputIndex : track.inputIndex + 1;
        input.setSelectedId (input.indexOfItemId (wanted) >= 0 ? wanted : 1, juce::dontSendNotification);
    }

    void syncButtons()
    {
        muteButton.setToggleState (track.mute, juce::dontSendNotification);
        soloButton.setToggleState (track.solo, juce::dontSendNotification);
        armButton.setToggleState (track.armed, juce::dontSendNotification);
        monitorButton.setToggleState (track.monitor, juce::dontSendNotification);

        if (name.getText() != track.name && ! name.isBeingEdited())
            name.setText (track.name, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        const bool selected = view.getSelectedTrack() == &track;
        g.fillAll (selected ? panelLight : panel);

        g.setColour (track.colour);
        g.fillRect (0, 0, selected ? 6 : 4, getHeight());

        // mini meter along the bottom
        auto meter = getLocalBounds().removeFromBottom (5).withTrimmedLeft (12).withTrimmedRight (10).toFloat();
        g.setColour (background);
        g.fillRect (meter);
        const bool showInput = track.armed.load() || track.monitor.load();
        float level = juce::jmax (track.meterL, track.meterR);
        if (showInput)
            level = juce::jmax (view.engine.getInputLevel (track.inputIndex),
                                track.stereoInput ? view.engine.getInputLevel (track.inputIndex + 1) : 0.0f);
        level = juce::jlimit (0.0f, 1.0f, level);
        g.setColour (level > 0.98f ? record : showInput ? monitor : solo);
        g.fillRect (meter.withWidth (meter.getWidth() * std::sqrt (level)));

        g.setColour (grid);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedLeft (12).withTrimmedRight (8).withTrimmedBottom (8);
        r.removeFromTop (6);
        name.setBounds (r.removeFromTop (22));
        r.removeFromTop (4);

        auto buttons = r.removeFromTop (22);
        for (auto* b : { &muteButton, &soloButton, &armButton, &monitorButton })
        {
            b->setBounds (buttons.removeFromLeft (26));
            buttons.removeFromLeft (4);
        }

        r.removeFromTop (5);
        input.setVisible (r.getHeight() >= 18);
        input.setBounds (r.removeFromTop (20));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        view.select (&track, nullptr);

        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem ("Rename", [this] { name.showEditor(); });
            m.addItem ("Import audio file...", [this] { if (view.onImportRequest) view.onImportRequest (&track, view.engine.getPositionSec()); });
            m.addItem ("Duplicate track", [this] { view.engine.duplicateTrack (view.engine.indexOf (&track)); });

            const int index = view.engine.indexOf (&track);
            m.addItem ("Move up", index > 0, false, [this] { auto i = view.engine.indexOf (&track); view.engine.moveTrack (i, i - 1); });
            m.addItem ("Move down", index < view.engine.getNumTracks() - 1, false,
                       [this] { auto i = view.engine.indexOf (&track); view.engine.moveTrack (i, i + 1); });

            juce::PopupMenu colours;
            const juce::Colour palette[] = { juce::Colour (0xff4c9aff), juce::Colour (0xff3ec27b), juce::Colour (0xfff5a524),
                                             juce::Colour (0xffb16cff), juce::Colour (0xffff6a3d), juce::Colour (0xff2ec5c5),
                                             juce::Colour (0xffff5c8a), juce::Colour (0xffc9d14a) };
            const char* names[] = { "Blue", "Green", "Amber", "Purple", "Orange", "Teal", "Pink", "Lime" };
            for (int i = 0; i < 8; ++i)
            {
                juce::PopupMenu::Item item (names[i]);
                item.colour = palette[i];
                item.action = [this, c = palette[i]] { track.colour = c; view.engine.markEdited(); view.engine.sendChangeMessage(); };
                colours.addItem (item);
            }
            m.addSubMenu ("Colour", colours);
            m.addSeparator();
            m.addItem ("Delete track", [this]
            {
                auto* engine = &view.engine;
                auto* t = &track;
                if (! engine->getSettings().getBoolValue (Settings::confirmDelete, true))
                    engine->removeTrack (engine->indexOf (t));
                else
                    showOkCancel ("Delete track", "Delete \"" + track.name + "\" and its clips?", "Delete",
                                  [engine, t] { engine->removeTrack (engine->indexOf (t)); });
            });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        }
    }

    Track& getTrack() noexcept { return track; }

private:
    void setupToggle (juce::TextButton& b, const juce::String& text, juce::Colour onColour,
                      const juce::String& tip, std::atomic<bool>& flag)
    {
        b.setButtonText (text);
        b.setTooltip (tip);
        b.setClickingTogglesState (true);
        b.setToggleState (flag.load(), juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonOnColourId, onColour);
        b.onClick = [this, &b, &flag]
        {
            flag = b.getToggleState();
            view.engine.markEdited();
            view.engine.sendChangeMessage();
        };
        addAndMakeVisible (b);
    }

    ArrangementView& view;
    Track& track;
    juce::Label name;
    juce::TextButton muteButton, soloButton, armButton, monitorButton;
    juce::ComboBox input;
};

//==============================================================================
class ArrangementView::TrackLane : public juce::Component
{
public:
    TrackLane (ArrangementView& v, Track& t) : view (v), track (t) {}

    void paint (juce::Graphics& g) override
    {
        const bool selected = view.getSelectedTrack() == &track;
        g.fillAll (selected ? background.brighter (0.05f) : background.brighter (0.02f));
        view.drawGrid (g, getLocalBounds(), false);

        // Loop region tint
        if (view.engine.loopEnabled.load())
        {
            const float x1 = view.secToX (view.engine.loopStartSec.load());
            const float x2 = view.secToX (view.engine.loopEndSec.load());
            g.setColour (accent.withAlpha (0.05f));
            g.fillRect (juce::Rectangle<float> (x1, 0.0f, juce::jmax (0.0f, x2 - x1), (float) getHeight()));
        }

        // Section boundaries
        for (auto& sec : view.engine.sections)
        {
            g.setColour (sec.colour.withAlpha (0.35f));
            g.drawVerticalLine ((int) view.secToX (sec.startSec), 0.0f, (float) getHeight());
        }

        const auto visibleStart = view.xToSec (0.0f);
        const auto visibleEnd   = view.xToSec ((float) getWidth());

        for (auto& clip : track.clips)
        {
            if (clip->endSec() < visibleStart || clip->startSec > visibleEnd)
                continue;

            // Inactive takes stay hidden behind the active one
            if (clip->muted && clip->takeGroup > 0)
                continue;

            const auto clipColour = clip->muted ? textDim : track.colour;

            const auto full = clipBounds (*clip);
            auto r = full;
            const bool isSelected = view.getSelectedClip() == clip.get();

            g.setColour (clipColour.withAlpha (isSelected ? 0.32f : 0.22f));
            g.fillRoundedRectangle (r, 5.0f);

            auto title = r.removeFromTop ((float) titleHeight);
            g.setColour (clipColour.withAlpha (0.9f));
            g.fillRoundedRectangle (title, 5.0f);
            g.fillRect (title.withTrimmedTop (5.0f));
            g.setColour (background);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            auto label = clip->name;
            if (std::abs (clip->gain - 1.0f) > 0.001f)
                label << "   " << juce::String (juce::Decibels::gainToDecibels (clip->gain), 1) << " dB";
            if (clip->muted)
                label << "   (muted)";
            if (clip->takeGroup > 0)
            {
                int count = 0;
                for (auto& c : track.clips)
                    if (c->takeGroup == clip->takeGroup)
                        ++count;
                label = "TAKE " + juce::String (clip->takeNumber) + "/" + juce::String (count) + "   " + label;
            }
            g.drawText (label, title.reduced (8.0f, 0.0f), juce::Justification::centredLeft, true);

            // Waveform (only the visible part)
            if (clip->thumbnail != nullptr && clip->thumbnail->getTotalLength() > 0.0)
            {
                const double from = juce::jmax (clip->startSec, visibleStart);
                const double to   = juce::jmin (clip->endSec(), visibleEnd);
                const float x1 = view.secToX (from), x2 = view.secToX (to);

                juce::Rectangle<int> wave ((int) x1, (int) r.getY() + 2, juce::jmax (1, (int) (x2 - x1)), (int) r.getHeight() - 4);
                g.setColour (clipColour.brighter (0.4f));
                clip->thumbnail->drawChannels (g, wave,
                                               clip->offsetSec + (from - clip->startSec),
                                               clip->offsetSec + (to - clip->startSec),
                                               juce::jlimit (0.05f, 4.0f, clip->gain));
            }

            // Fades: shaded curves and handles
            const float fin  = (float) (clip->fadeInSec * view.pixelsPerSecond);
            const float fout = (float) (clip->fadeOutSec * view.pixelsPerSecond);
            auto body = full.withTrimmedTop ((float) titleHeight);

            if (fin > 1.0f)
            {
                juce::Path p;
                p.startNewSubPath (body.getX(), body.getBottom());
                p.quadraticTo (body.getX() + fin * 0.1f, body.getY(), body.getX() + fin, body.getY());
                p.lineTo (body.getX(), body.getY());
                p.closeSubPath();
                g.setColour (background.withAlpha (0.55f));
                g.fillPath (p);
            }
            if (fout > 1.0f)
            {
                juce::Path p;
                p.startNewSubPath (body.getRight(), body.getBottom());
                p.quadraticTo (body.getRight() - fout * 0.1f, body.getY(), body.getRight() - fout, body.getY());
                p.lineTo (body.getRight(), body.getY());
                p.closeSubPath();
                g.setColour (background.withAlpha (0.55f));
                g.fillPath (p);
            }

            if (isSelected || full.getWidth() > 40.0f)
            {
                g.setColour (juce::Colours::white.withAlpha (isSelected ? 0.95f : 0.55f));
                g.fillEllipse (fadeInHandle (*clip).toFloat());
                g.fillEllipse (fadeOutHandle (*clip).toFloat());
            }

            g.setColour (isSelected ? juce::Colours::white : clipColour.withAlpha (0.9f));
            g.drawRoundedRectangle (full, 5.0f, isSelected ? 2.0f : 1.0f);
        }

        // Live recording region
        if (view.engine.isRecording() && track.armed)
        {
            const float x1 = view.secToX (view.engine.getRecordStartSec());
            float x2 = view.secToX (juce::jmax (view.engine.getRecordStartSec(), view.engine.getPositionSec()));
            if (view.engine.getActiveRecordMode() == AudioEngine::RecordMode::loopTakes && view.engine.getCurrentTakeNumber() > 1)
                x2 = view.secToX (view.engine.loopEndSec.load());
            g.setColour (record.withAlpha (0.3f));
            g.fillRoundedRectangle (juce::Rectangle<float> (x1, 4.0f, juce::jmax (1.0f, x2 - x1), (float) getHeight() - 9.0f), 5.0f);
        }

        g.setColour (grid);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    bool hasPendingThumbnails() const
    {
        for (auto& c : track.clips)
            if (c->thumbnail != nullptr && ! c->thumbnail->isFullyLoaded())
                return true;
        return false;
    }

    //==============================================================================
    void mouseMove (const juce::MouseEvent& e) override
    {
        auto mode = DragMode::none;
        clipAt (e.position, mode);
        setMouseCursor (mode == DragMode::trimLeft || mode == DragMode::trimRight || mode == DragMode::fadeIn || mode == DragMode::fadeOut
                          ? juce::MouseCursor::LeftRightResizeCursor
                          : mode == DragMode::move ? juce::MouseCursor::DraggingHandCursor
                                                   : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragMode = DragMode::none;
        auto* clip = clipAt (e.position, dragMode);
        view.select (&track, clip);

        if (e.mods.isPopupMenu())
        {
            dragMode = DragMode::none;
            showMenu (clip, view.xToSec (e.position.x));
            return;
        }

        if (clip == nullptr)
        {
            if (! view.engine.isRecording())
                view.engine.setPositionSec (view.snap (view.xToSec (e.position.x), ! e.mods.isAltDown()));
            return;
        }

        dragClip = clip;
        assignOrig (*clip);
        view.engine.beginClipEdit();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragClip == nullptr || dragMode == DragMode::none)
            return;

        const double delta = (double) e.getDistanceFromDragStartX() / view.pixelsPerSecond;
        const bool allowSnap = ! e.mods.isAltDown();
        const double origEnd = orig.startSec + orig.lengthSec;

        const juce::ScopedLock sl (view.engine.getLock());

        switch (dragMode)
        {
            case DragMode::move:
                dragClip->startSec = juce::jmax (0.0, view.snap (orig.startSec + delta, allowSnap));
                break;

            case DragMode::trimLeft:
            {
                const double minStart = juce::jmax (0.0, orig.startSec - orig.offsetSec);
                const double newStart = juce::jlimit (minStart, origEnd - 0.02, view.snap (orig.startSec + delta, allowSnap));
                dragClip->startSec  = newStart;
                dragClip->offsetSec = orig.offsetSec + (newStart - orig.startSec);
                dragClip->lengthSec = origEnd - newStart;
                dragClip->fadeInSec = juce::jmin (dragClip->fadeInSec, dragClip->lengthSec);
                break;
            }

            case DragMode::trimRight:
            {
                const double maxEnd = orig.startSec + (dragClip->sourceLengthSec - orig.offsetSec);
                const double newEnd = juce::jlimit (orig.startSec + 0.02, maxEnd, view.snap (origEnd + delta, allowSnap));
                dragClip->lengthSec = newEnd - orig.startSec;
                dragClip->fadeOutSec = juce::jmin (dragClip->fadeOutSec, dragClip->lengthSec);
                break;
            }

            case DragMode::fadeIn:
                dragClip->fadeInSec = juce::jlimit (0.0, dragClip->lengthSec - dragClip->fadeOutSec, orig.fadeInSec + delta);
                break;

            case DragMode::fadeOut:
                dragClip->fadeOutSec = juce::jlimit (0.0, dragClip->lengthSec - dragClip->fadeInSec, orig.fadeOutSec - delta);
                break;

            case DragMode::none:
                break;
        }

        view.engine.markEdited();
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragClip != nullptr)
        {
            static const char* names[] = { "", "Move clip", "Trim clip", "Trim clip", "Fade in", "Fade out" };
            view.engine.endClipEdit (names[(int) dragMode]);
            view.engine.sendChangeMessage();
        }
        dragClip = nullptr;
        dragMode = DragMode::none;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto mode = DragMode::none;
        if (clipAt (e.position, mode) == nullptr && view.onImportRequest)
            view.onImportRequest (&track, view.snap (view.xToSec (e.position.x)));
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (e.mods.isCtrlDown() || e.mods.isCommandDown())
            view.zoomBy (wheel.deltaY > 0 ? 1.25 : 0.8, view.xToSec (e.position.x));
        else if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
            view.scrollToSec (view.scrollSec - (double) (std::abs (wheel.deltaX) > 0.0f ? wheel.deltaX : wheel.deltaY) * view.getVisibleSeconds() * 0.25);
        else
            Component::mouseWheelMove (e, wheel);  // vertical scroll via the viewport
    }

private:
    enum class DragMode { none, move, trimLeft, trimRight, fadeIn, fadeOut };
    static constexpr int titleHeight = 16;

    juce::Rectangle<float> clipBounds (const AudioClip& c) const
    {
        const float x1 = view.secToX (c.startSec), x2 = view.secToX (c.endSec());
        return { x1, 4.0f, juce::jmax (2.0f, x2 - x1), (float) getHeight() - 9.0f };
    }

    juce::Rectangle<int> fadeInHandle (const AudioClip& c) const
    {
        auto r = clipBounds (c);
        const float x = r.getX() + (float) (c.fadeInSec * view.pixelsPerSecond);
        return juce::Rectangle<int> (8, 8).withCentre ({ (int) juce::jlimit (r.getX() + 6.0f, r.getRight() - 6.0f, x), (int) r.getY() + titleHeight + 5 });
    }

    juce::Rectangle<int> fadeOutHandle (const AudioClip& c) const
    {
        auto r = clipBounds (c);
        const float x = r.getRight() - (float) (c.fadeOutSec * view.pixelsPerSecond);
        return juce::Rectangle<int> (8, 8).withCentre ({ (int) juce::jlimit (r.getX() + 6.0f, r.getRight() - 6.0f, x), (int) r.getY() + titleHeight + 5 });
    }

    AudioClip* clipAt (juce::Point<float> p, DragMode& mode) const
    {
        for (auto it = track.clips.rbegin(); it != track.clips.rend(); ++it)
        {
            if ((*it)->muted && (*it)->takeGroup > 0)
                continue;

            auto r = clipBounds (**it);
            if (! r.contains (p))
                continue;

            if (fadeInHandle (**it).toFloat().expanded (4.0f).contains (p))       mode = DragMode::fadeIn;
            else if (fadeOutHandle (**it).toFloat().expanded (4.0f).contains (p)) mode = DragMode::fadeOut;
            else
            {
                const float edge = juce::jmin (7.0f, r.getWidth() * 0.25f);
                mode = p.x < r.getX() + edge     ? DragMode::trimLeft
                     : p.x > r.getRight() - edge ? DragMode::trimRight
                                                 : DragMode::move;
            }
            return it->get();
        }

        mode = DragMode::none;
        return nullptr;
    }

    void setClipGain (AudioClip* clip, float gain)
    {
        auto& engine = view.engine;
        engine.beginClipEdit();
        {
            const juce::ScopedLock sl (engine.getLock());
            clip->gain = juce::jlimit (0.0f, 16.0f, gain);
        }
        engine.endClipEdit ("Clip gain");
        engine.sendChangeMessage();
    }

    void showMenu (AudioClip* clip, double atSec)
    {
        juce::PopupMenu m;
        auto* engine = &view.engine;
        auto* t = &track;

        if (clip != nullptr)
        {
            m.addItem ("Split at playhead  (S)", [this] { view.splitSelectedClipAtPlayhead(); });
            m.addItem ("Split here", [engine, t, clip, atSec] { engine->splitClip (*t, clip, atSec); });
            m.addItem ("Duplicate  (Ctrl+D)", [this] { view.duplicateSelectedClip(); });
            m.addItem ("Mute clip", true, clip->muted, [engine, clip] { engine->setClipMuted (*clip, ! clip->muted); });

            if (clip->takeGroup > 0)
            {
                juce::PopupMenu takes;
                const int group = clip->takeGroup;
                std::vector<AudioClip*> groupClips;
                for (auto& c : track.clips)
                    if (c->takeGroup == group)
                        groupClips.push_back (c.get());
                std::sort (groupClips.begin(), groupClips.end(), [] (auto* a, auto* b) { return a->takeNumber < b->takeNumber; });

                for (auto* c : groupClips)
                    takes.addItem ("Take " + juce::String (c->takeNumber), true, ! c->muted,
                                   [engine, t, group, n = c->takeNumber] { engine->chooseTake (*t, group, n); });
                takes.addSeparator();
                takes.addItem ("Keep this take, delete the others", [engine, t, group] { engine->flattenTakes (*t, group); });
                m.addSubMenu ("Takes (" + juce::String ((int) groupClips.size()) + ")", takes);
            }
            m.addSeparator();

            juce::PopupMenu gain;
            gain.addItem ("Normalize (peak -0.3 dB)", [engine, clip] { engine->normalizeClip (*clip); });
            gain.addItem ("+3 dB", [this, clip] { setClipGain (clip, clip->gain * juce::Decibels::decibelsToGain (3.0f)); });
            gain.addItem ("-3 dB", [this, clip] { setClipGain (clip, clip->gain * juce::Decibels::decibelsToGain (-3.0f)); });
            gain.addItem ("Reset to 0 dB", [this, clip] { setClipGain (clip, 1.0f); });
            m.addSubMenu ("Clip gain", gain);

            m.addItem ("Remove fades", clip->fadeInSec > 0.0 || clip->fadeOutSec > 0.0, false, [engine, clip]
            {
                engine->beginClipEdit();
                {
                    const juce::ScopedLock sl (engine->getLock());
                    clip->fadeInSec = clip->fadeOutSec = 0.0;
                }
                engine->endClipEdit ("Remove fades");
                engine->sendChangeMessage();
            });
            m.addSeparator();
            m.addItem ("Separate stems with AI...", [this, clip] { if (view.onSeparateClip) view.onSeparateClip (track, *clip); });
            m.addItem ("Show file in folder", [f = clip->file] { f.revealToUser(); });
            m.addSeparator();
            m.addItem ("Delete clip  (Del)", [this] { view.deleteSelectedClip(); });
        }
        else
        {
            m.addItem ("Import audio here...", [this, atSec] { if (view.onImportRequest) view.onImportRequest (&track, view.snap (atSec)); });
            m.addItem ("Set playhead here", [engine, atSec] { engine->setPositionSec (atSec); });
        }

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    ArrangementView& view;
    Track& track;

    AudioClip* dragClip = nullptr;
    DragMode dragMode = DragMode::none;
    struct { double startSec = 0, offsetSec = 0, lengthSec = 0, fadeInSec = 0, fadeOutSec = 0; } orig;

    template <typename T> void assignOrig (const T& c) { orig = { c.startSec, c.offsetSec, c.lengthSec, c.fadeInSec, c.fadeOutSec }; }
};
//==============================================================================
class ArrangementView::TrackRow : public juce::Component
{
public:
    TrackRow (ArrangementView& v, Track& t) : header (v, t), lane (v, t), track (t)
    {
        addAndMakeVisible (header);
        addAndMakeVisible (lane);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        header.setBounds (r.removeFromLeft (headerWidth));
        lane.setBounds (r);
    }

    TrackHeader header;
    TrackLane lane;
    Track& track;
};

//==============================================================================
ArrangementView::ArrangementView (AudioEngine& e) : engine (e)
{
    ruler = std::make_unique<Ruler> (*this);
    addAndMakeVisible (*ruler);
    sectionLane = std::make_unique<SectionLane> (*this);
    addAndMakeVisible (*sectionLane);

    structureLabel.setText ("STRUCTURE", juce::dontSendNotification);
    structureLabel.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    structureLabel.setColour (juce::Label::textColourId, textDim);
    addAndMakeVisible (structureLabel);
    addSectionButton.setTooltip ("Add a section (Intro, Verse, Chorus...) at the playhead (Ctrl+M)");
    addSectionButton.onClick = [this] { addSectionAt (engine.getPositionSec()); };
    addAndMakeVisible (addSectionButton);

    viewport.setViewedComponent (&rowsHolder, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (scrollBarHeight);
    addAndMakeVisible (viewport);

    hScroll.addListener (this);
    hScroll.setAutoHide (false);
    addAndMakeVisible (hScroll);

    playhead = std::make_unique<Playhead>();
    addAndMakeVisible (*playhead);

    snapButton.setClickingTogglesState (true);
    snapButton.setToggleState (snapToGrid, juce::dontSendNotification);
    snapButton.setTooltip ("Snap clips and the playhead to beats (hold Alt to bypass)");
    snapButton.onClick = [this] { snapToGrid = snapButton.getToggleState(); };
    zoomInButton.onClick  = [this] { zoomBy (1.4, xToSec ((float) (getWidth() - headerWidth) * 0.5f)); };
    zoomOutButton.onClick = [this] { zoomBy (1.0 / 1.4, xToSec ((float) (getWidth() - headerWidth) * 0.5f)); };
    zoomInButton.setTooltip ("Zoom in (Ctrl + wheel)");
    zoomOutButton.setTooltip ("Zoom out (Ctrl + wheel)");

    for (auto* b : { &snapButton, &zoomInButton, &zoomOutButton })
        addAndMakeVisible (b);

    engine.addChangeListener (this);
    rebuildRows();
    startTimerHz (30);
}

ArrangementView::~ArrangementView()
{
    engine.removeChangeListener (this);
    hScroll.removeListener (this);
}

//==============================================================================
void ArrangementView::select (Track* track, AudioClip* clip)
{
    if (track == selectedTrack && clip == selectedClip)
        return;

    selectedTrack = track;
    selectedClip = clip;

    for (auto* row : rows)
    {
        row->header.repaint();
        row->lane.repaint();
    }
}

void ArrangementView::deleteSelectedClip()
{
    if (selectedClip == nullptr)
        return;

    if (auto* t = engine.findTrackForClip (selectedClip))
    {
        auto* clip = selectedClip;
        selectedClip = nullptr;
        engine.removeClip (*t, clip);
    }
}

void ArrangementView::splitSelectedClipAtPlayhead()
{
    if (selectedClip == nullptr)
        return;

    if (auto* t = engine.findTrackForClip (selectedClip))
        engine.splitClip (*t, selectedClip, engine.getPositionSec());
}

void ArrangementView::addSectionAt (double sec)
{
    const double bar = 4.0 * 60.0 / engine.bpm.load();
    double start = juce::jmax (0.0, std::floor (sec / bar + 1.0e-6) * bar);

    // Inside an existing section? Start a new one where it ends instead.
    if (const int inside = engine.sectionAt (start); inside >= 0)
        start = engine.sections[(size_t) inside].endSec;

    double end = start + 8.0 * bar;
    for (auto& s : engine.sections)
        if (s.startSec > start + 1.0e-6)
            end = juce::jmin (end, s.startSec);

    selectedSection = engine.addSection (AudioEngine::suggestSectionName ((int) engine.sections.size()), start, end);
    sectionLane->repaint();
}

void ArrangementView::renameSection (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) engine.sections.size()))
        return;

    auto* aw = new juce::AlertWindow ("Rename section", {}, juce::MessageBoxIconType::NoIcon, this);
    aw->addTextEditor ("name", engine.sections[(size_t) index].name);
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<ArrangementView> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw, index] (int result)
    {
        if (safe == nullptr || result != 1 || ! juce::isPositiveAndBelow (index, (int) safe->engine.sections.size()))
            return;
        auto sec = safe->engine.sections[(size_t) index];
        const auto name = aw->getTextEditorContents ("name").trim();
        if (name.isEmpty() || name == sec.name)
            return;
        // Keep a custom colour if the user picked one; otherwise colour by the new name
        if (sec.colour == AudioEngine::sectionColour (sec.name))
            sec.colour = AudioEngine::sectionColour (name);
        sec.name = name;
        safe->engine.setSection (index, sec);
    }), true);
}

void ArrangementView::jumpSection (int direction)
{
    const double now = engine.getPositionSec();
    const auto& secs = engine.sections;
    if (secs.empty())
        return;

    if (direction > 0)
    {
        for (auto& s : secs)
            if (s.startSec > now + 0.01)
            {
                engine.setPositionSec (s.startSec);
                return;
            }
    }
    else
    {
        for (auto it = secs.rbegin(); it != secs.rend(); ++it)
            if (it->startSec < now - 0.25)
            {
                engine.setPositionSec (it->startSec);
                return;
            }
        engine.setPositionSec (secs.front().startSec);
    }
}

void ArrangementView::setTrackHeight (int height)
{
    height = juce::jlimit (56, 200, height);
    if (height == rowHeight)
        return;
    rowHeight = height;
    layoutRows();
    repaint();
}

void ArrangementView::duplicateSelectedClip()
{
    if (selectedClip == nullptr)
        return;

    if (auto* t = engine.findTrackForClip (selectedClip))
        if (auto copy = engine.duplicateClip (*t, selectedClip))
            select (t, copy.get());
}

double ArrangementView::snap (double sec, bool allowSnap) const
{
    if (! snapToGrid || ! allowSnap)
        return sec;

    const double beat = 60.0 / engine.bpm.load();
    double step = beat;
    while (step * pixelsPerSecond < 12.0)
        step *= 2.0;

    return std::round (sec / step) * step;
}

double ArrangementView::getVisibleSeconds() const
{
    return (double) juce::jmax (1, getWidth() - headerWidth) / pixelsPerSecond;
}

void ArrangementView::zoomBy (double factor, double anchorSec)
{
    const float anchorX = secToX (anchorSec);
    pixelsPerSecond = juce::jlimit (2.0, 800.0, pixelsPerSecond * factor);
    scrollSec = juce::jmax (0.0, anchorSec - (double) anchorX / pixelsPerSecond);
    updateScrollBar();
    repaintLanes();
}

void ArrangementView::scrollToSec (double sec)
{
    scrollSec = juce::jmax (0.0, sec);
    updateScrollBar();
    repaintLanes();
}

void ArrangementView::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    if (std::abs (newRangeStart - scrollSec) > 1.0e-6)
    {
        scrollSec = juce::jmax (0.0, newRangeStart);
        repaintLanes();
    }
}

void ArrangementView::updateScrollBar()
{
    const double visible = getVisibleSeconds();
    const double total = juce::jmax (engine.getProjectLengthSec() + 60.0, scrollSec + visible);
    hScroll.setRangeLimits (0.0, total, juce::dontSendNotification);
    hScroll.setCurrentRange (scrollSec, visible, juce::dontSendNotification);
}

void ArrangementView::repaintLanes()
{
    ruler->repaint();
    sectionLane->repaint();
    for (auto* row : rows)
        row->lane.repaint();
}

//==============================================================================
void ArrangementView::drawGrid (juce::Graphics& g, juce::Rectangle<int> area, bool isRuler) const
{
    const double beatSec = 60.0 / juce::jlimit (20.0, 400.0, engine.bpm.load());
    double stepBeats = 1.0;
    while (stepBeats * beatSec * pixelsPerSecond < 14.0)
        stepBeats *= 2.0;

    int labelEveryBars = 1;
    while (labelEveryBars * 4.0 * beatSec * pixelsPerSecond < 44.0)
        labelEveryBars *= 2;

    const double firstBeat = std::floor (scrollSec / beatSec / stepBeats) * stepBeats;
    g.setFont (juce::FontOptions (11.0f));

    for (double b = firstBeat;; b += stepBeats)
    {
        const float x = secToX (b * beatSec);
        if (x > (float) area.getRight())
            break;
        if (x < 0.0f)
            continue;

        const bool isBar = std::fmod (b, 4.0) < 0.001;

        if (isRuler)
        {
            const int barIndex = (int) (b / 4.0);
            if (isBar && barIndex % labelEveryBars == 0)
            {
                g.setColour (textDim);
                g.drawText (juce::String (barIndex + 1), (int) x + 4, 2, 40, area.getHeight() / 2, juce::Justification::left);
                g.setColour (gridStrong);
                g.drawVerticalLine ((int) x, (float) area.getHeight() * 0.35f, (float) area.getHeight());
            }
            else
            {
                g.setColour (grid);
                g.drawVerticalLine ((int) x, (float) area.getHeight() * 0.7f, (float) area.getHeight());
            }
        }
        else
        {
            g.setColour (isBar ? gridStrong.withAlpha (0.6f) : grid.withAlpha (0.45f));
            g.drawVerticalLine ((int) x, 0.0f, (float) area.getHeight());
        }
    }
}

void ArrangementView::paint (juce::Graphics& g)
{
    g.fillAll (background);

    // Corner above the track headers
    g.setColour (panel);
    g.fillRect (0, 0, headerWidth, rulerHeight);
}

void ArrangementView::paintOverChildren (juce::Graphics& g)
{
    auto lanes = viewport.getBounds().withTrimmedLeft (headerWidth);

    if (rows.isEmpty())
    {
        g.setColour (textDim);
        g.setFont (juce::FontOptions (17.0f));
        g.drawFittedText ("Drag audio files here (WAV, MP3, FLAC, AIFF, OGG)\n"
                          "or press \"+ Track\", arm it (R) and hit Record.",
                          lanes.reduced (20), juce::Justification::centred, 3);
    }

    if (dragHover)
    {
        g.setColour (accent.withAlpha (0.12f));
        g.fillRect (viewport.getBounds());
        g.setColour (accent);
        g.drawRect (viewport.getBounds(), 2);
    }
}

void ArrangementView::resized()
{
    auto r = getLocalBounds();

    auto top = r.removeFromTop (rulerHeight);
    auto corner = top.removeFromLeft (headerWidth).reduced (6, 3);
    snapButton.setBounds (corner.removeFromLeft (56));
    corner.removeFromLeft (8);
    zoomOutButton.setBounds (corner.removeFromLeft (26));
    corner.removeFromLeft (4);
    zoomInButton.setBounds (corner.removeFromLeft (26));
    ruler->setBounds (top);

    auto structure = r.removeFromTop (sectionHeight);
    auto structureCorner = structure.removeFromLeft (headerWidth).reduced (8, 3);
    addSectionButton.setBounds (structureCorner.removeFromRight (84));
    structureLabel.setBounds (structureCorner);
    sectionLane->setBounds (structure);

    auto bottom = r.removeFromBottom (scrollBarHeight);
    hScroll.setBounds (bottom.withTrimmedLeft (headerWidth).withTrimmedRight (scrollBarHeight));

    viewport.setBounds (r);
    layoutRows();
    updateScrollBar();
}

void ArrangementView::layoutRows()
{
    const int width = viewport.getMaximumVisibleWidth();
    const int height = juce::jmax (viewport.getMaximumVisibleHeight(), rows.size() * rowHeight + rowHeight);

    rowsHolder.setSize (width, height);

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowHeight, width, rowHeight);
}

void ArrangementView::rebuildRows()
{
    std::vector<Track*> current;
    for (int i = 0; i < engine.getNumTracks(); ++i)
        current.push_back (engine.getTrack (i));

    if (selectedTrack != nullptr && std::find (current.begin(), current.end(), selectedTrack) == current.end())
        selectedTrack = nullptr;

    if (selectedClip != nullptr && engine.findTrackForClip (selectedClip) == nullptr)
        selectedClip = nullptr;

    if (current != shownTracks)
    {
        shownTracks = current;
        rows.clear();

        for (auto* t : current)
            rowsHolder.addAndMakeVisible (rows.add (new TrackRow (*this, *t)));

        layoutRows();
    }

    for (auto* row : rows)
        row->header.syncButtons();

    updateScrollBar();
    repaintLanes();
    repaint();
}

void ArrangementView::refreshInputs()
{
    for (auto* row : rows)
        row->header.refreshInputs();
}

void ArrangementView::updateMeters()
{
    for (auto* row : rows)
        row->header.repaint (0, rowHeight - 6, headerWidth, 6);
}

void ArrangementView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildRows();
}

void ArrangementView::timerCallback()
{
    const double pos = engine.getPositionSec();
    const double visible = getVisibleSeconds();

    // Follow the playhead while playing
    if (engine.isPlaying() && (pos > scrollSec + visible * 0.92 || pos < scrollSec))
        scrollToSec (pos - visible * 0.08);

    const float x = secToX (pos);
    const bool onScreen = x >= 0.0f && x <= (float) (getWidth() - headerWidth);
    playhead->setVisible (onScreen);

    if (onScreen)
    {
        playhead->colour = engine.isRecording() ? record : accent;
        playhead->setBounds (headerWidth + (int) x - 6, 0, 13, viewport.getBottom());
    }

    for (auto* row : rows)
        if (row->lane.hasPendingThumbnails() || (engine.isRecording() && row->track.armed))
            row->lane.repaint();
}

//==============================================================================
bool ArrangementView::isInterestedInFileDrag (const juce::StringArray& files)
{
    const auto wildcard = engine.getFormatManager().getWildcardForAllFormats();
    for (auto& f : files)
        if (juce::File (f).hasFileExtension (wildcard.replace ("*", "").replace (",", ";")))
            return true;
    return false;
}

void ArrangementView::filesDropped (const juce::StringArray& files, int x, int y)
{
    juce::Array<juce::File> list;
    for (auto& f : files)
        list.add (juce::File (f));
    importFilesAt (list, x, y);
}

bool ArrangementView::isInterestedInDragSource (const SourceDetails& details)
{
    return details.description.toString() == "RakamBrowserFile"
        && dynamic_cast<juce::FileTreeComponent*> (details.sourceComponent.get()) != nullptr;
}

void ArrangementView::itemDropped (const SourceDetails& details)
{
    if (auto* tree = dynamic_cast<juce::FileTreeComponent*> (details.sourceComponent.get()))
    {
        juce::Array<juce::File> list;
        for (int i = 0; i < tree->getNumSelectedFiles(); ++i)
            if (tree->getSelectedFile (i).existsAsFile())
                list.add (tree->getSelectedFile (i));

        engine.stopPreview();
        importFilesAt (list, details.localPosition.x, details.localPosition.y);
    }
}

void ArrangementView::importFilesAt (const juce::Array<juce::File>& files, int x, int y)
{
    dragHover = false;
    repaint();

    const auto inRows = rowsHolder.getLocalPoint (this, juce::Point<int> (x, y));
    const int rowIndex = inRows.y / rowHeight;
    Track* target = (inRows.y >= 0 && juce::isPositiveAndBelow (rowIndex, rows.size())) ? &rows[rowIndex]->track : nullptr;
    const double startSec = x > headerWidth ? snap (xToSec ((float) (x - headerWidth))) : 0.0;

    juce::MouseCursor::showWaitCursor();
    juce::StringArray errors;

    for (auto& file : files)
    {
        auto* track = target != nullptr ? target : engine.addTrack (file.getFileNameWithoutExtension());
        target = nullptr; // further files each get their own track

        juce::String error;
        if (engine.importFile (file, *track, startSec, error) == nullptr)
            errors.add (error);
    }

    juce::MouseCursor::hideWaitCursor();

    if (! errors.isEmpty() && onError)
        onError (errors.joinIntoString ("\n"));
}
