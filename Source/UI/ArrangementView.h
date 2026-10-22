#pragma once

#include "Engine/AudioEngine.h"

/**
    The main timeline: a ruler, one row per track (header on the left, clip lane
    on the right), a playhead and horizontal scrolling / zooming.

    - Drag audio files from Explorer onto it to import them.
    - Drag clips to move them, drag clip edges to trim, right-click for more.
    - Ctrl + mouse wheel zooms, Shift + mouse wheel scrolls sideways.
*/
class ArrangementView : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        public juce::DragAndDropTarget,
                        private juce::ChangeListener,
                        private juce::Timer,
                        private juce::ScrollBar::Listener
{
public:
    explicit ArrangementView (AudioEngine&);
    ~ArrangementView() override;

    static constexpr int headerWidth = 236, rulerHeight = 38, loopStripHeight = 12, sectionHeight = 26, scrollBarHeight = 12;
    int rowHeight = 86;
    void setTrackHeight (int height);

    // Selection
    Track*     getSelectedTrack() const noexcept { return selectedTrack; }
    AudioClip* getSelectedClip() const noexcept  { return selectedClip; }
    void select (Track* track, AudioClip* clip);

    void deleteSelectedClip();
    void splitSelectedClipAtPlayhead();
    void duplicateSelectedClip();

    // Song structure
    void addSectionAt (double sec);
    void renameSection (int index);
    void jumpSection (int direction);     // -1 previous, +1 next
    int  selectedSection = -1;

    // Coordinate helpers (x is relative to the left edge of the lanes)
    float  secToX (double sec) const noexcept { return (float) ((sec - scrollSec) * pixelsPerSecond); }
    double xToSec (float x) const noexcept    { return scrollSec + (double) x / pixelsPerSecond; }
    double snap (double sec, bool allowSnap = true) const;
    void   zoomBy (double factor, double anchorSec);
    void   scrollToSec (double sec);
    double getVisibleSeconds() const;

    bool snapToGrid = true;

    /** Requests from the timeline that the main window handles. */
    std::function<void (Track&, AudioClip&)> onSeparateClip;
    std::function<void (Track*, double)> onImportRequest;   // track may be nullptr → new track
    std::function<void (const juce::String&)> onError;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { dragHover = false; repaint(); }

    // DragAndDropTarget (files dragged from the browser panel)
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dragHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { dragHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

    void importFilesAt (const juce::Array<juce::File>& files, int x, int y);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    void refreshInputs();
    void updateMeters();

    AudioEngine& engine;

private:
    class Ruler;
    class SectionLane;
    class TrackHeader;
    class TrackLane;
    class TrackRow;
    class Playhead;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    void rebuildRows();
    void layoutRows();
    void updateScrollBar();
    void repaintLanes();
    void drawGrid (juce::Graphics&, juce::Rectangle<int> area, bool isRuler) const;

    double pixelsPerSecond = 40.0;
    double scrollSec = 0.0;

    std::unique_ptr<Ruler> ruler;
    std::unique_ptr<SectionLane> sectionLane;
    juce::Label structureLabel;
    juce::TextButton addSectionButton { "+ Section" };
    std::unique_ptr<Playhead> playhead;
    juce::Viewport viewport;
    juce::Component rowsHolder;
    juce::OwnedArray<TrackRow> rows;
    juce::ScrollBar hScroll { false };
    juce::TextButton snapButton { "Snap" }, zoomInButton { "+" }, zoomOutButton { "-" };

    std::vector<Track*> shownTracks;
    Track* selectedTrack = nullptr;
    AudioClip* selectedClip = nullptr;
    bool dragHover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementView)
};
