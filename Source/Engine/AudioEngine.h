#pragma once

#include "Engine/Track.h"
#include "Plugins/BuiltInPlugins.h"
#include "Settings.h"

//==============================================================================
/**
    The heart of the DAW: owns the audio device (ASIO / WASAPI / DirectSound),
    the tracks, the plugin hosting and the transport. It renders audio in real
    time for playback and offline for exporting mixes and stems.

    Threading rules:
      - Structural changes (adding tracks, clips, plugins) happen on the
        message thread while holding getLock().
      - The audio callback only *tries* the lock; if the UI is busy it outputs
        a block of silence instead of waiting.
      - Simple mixer values (gain, pan, mute...) are atomics and need no lock.
*/
class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::AudioPlayHead,
                    public juce::ChangeBroadcaster,
                    private juce::AsyncUpdater
{
public:
    explicit AudioEngine (juce::PropertiesFile& settings);
    ~AudioEngine() override;

    //==============================================================================
    juce::AudioDeviceManager&       getDeviceManager()       { return deviceManager; }
    juce::AudioFormatManager&       getFormatManager()       { return formatManager; }
    juce::AudioPluginFormatManager& getPluginFormatManager() { return pluginFormatManager; }
    juce::KnownPluginList&          getKnownPlugins()        { return knownPlugins; }
    juce::AudioThumbnailCache&      getThumbnailCache()      { return thumbnailCache; }
    juce::CriticalSection&          getLock()                { return lock; }
    juce::PropertiesFile&           getSettings()            { return settings; }

    void saveSettings();

    //==============================================================================
    // Tracks
    int    getNumTracks() const noexcept       { return (int) tracks.size(); }
    Track* getTrack (int index) const noexcept { return juce::isPositiveAndBelow (index, getNumTracks()) ? tracks[(size_t) index].get() : nullptr; }
    Track& getMaster() noexcept                { return *master; }
    int    indexOf (const Track* t) const;

    Track* addTrack (const juce::String& name = {}, int insertIndex = -1);
    void   removeTrack (int index);
    Track* duplicateTrack (int index);
    void   moveTrack (int fromIndex, int toIndex);
    void   clearProject();

    //==============================================================================
    // Clips
    /** Decodes an audio file into a clip (resampled to the engine rate). */
    ClipPtr loadClip (const juce::File& file, juce::String& error);
    /** Loads a file and places it on a track. Returns the new clip or nullptr. */
    ClipPtr importFile (const juce::File& file, Track& track, double startSec, juce::String& error);

    void addClip (Track& track, ClipPtr clip);
    void removeClip (Track& track, const AudioClip* clip);
    void splitClip (Track& track, const AudioClip* clip, double atSec);
    Track* findTrackForClip (const AudioClip* clip) const;
    ClipPtr duplicateClip (Track& track, const AudioClip* clip);
    void normalizeClip (AudioClip& clip);

    //==============================================================================
    // Undo / redo for clip edits (move, trim, split, fades, delete, import, record)
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }
    /** Wrap a clip edit in begin/end to make it undoable. Calls may be nested. */
    void beginClipEdit();
    void endClipEdit (const juce::String& actionName);
    bool undo();
    bool redo();
    void clearUndoHistory() { undoManager.clearUndoHistory(); }

    struct ClipState
    {
        ClipPtr clip;
        double start, offset, length, fadeIn, fadeOut;
        float gain;
        bool muted;
        bool operator== (const ClipState& o) const
        {
            using juce::exactlyEqual;
            return clip == o.clip && exactlyEqual (start, o.start) && exactlyEqual (offset, o.offset)
                && exactlyEqual (length, o.length) && exactlyEqual (fadeIn, o.fadeIn)
                && exactlyEqual (fadeOut, o.fadeOut) && exactlyEqual (gain, o.gain) && muted == o.muted;
        }
    };

    struct ClipLayout
    {
        std::vector<std::pair<Track*, std::vector<ClipState>>> tracks;
        std::vector<Section> sections;
        bool operator== (const ClipLayout& o) const { return tracks == o.tracks && sections == o.sections; }
    };

    //==============================================================================
    // Song structure (sections). Message thread only; edits are undoable.
    std::vector<Section> sections;
    int  addSection (const juce::String& name, double startSec, double endSec);   // returns its index
    void removeSection (int index);                                              // marker only, audio stays
    void setSection (int index, const Section& newValue);
    void duplicateSection (int index);            // copies its audio right after it, pushing the rest of the song later
    void deleteSectionWithAudio (int index);      // removes its audio and closes the gap
    int  sectionAt (double sec) const;
    static juce::String suggestSectionName (int index);
    static juce::Colour sectionColour (const juce::String& name);

    /** Removes audio between two times on a track (splitting clips at the edges). Wrap in begin/endClipEdit. */
    void clearRange (Track& track, double fromSec, double toSec);
    /** Moves every clip (and section) that starts at or after fromSec by delta seconds. Wrap in begin/endClipEdit. */
    void shiftFrom (double fromSec, double delta);

    // Takes
    void chooseTake (Track& track, int takeGroup, int takeNumber);
    void flattenTakes (Track& track, int takeGroup);     // keeps the active take, deletes the others
    void setClipMuted (AudioClip& clip, bool muted);

    ClipLayout captureLayout() const;
    void applyLayout (const ClipLayout&);

    //==============================================================================
    // Plugins (VST3 inserts)
    bool addPlugin (Track& track, const juce::PluginDescription& desc, juce::String& error,
                    const juce::MemoryBlock* state = nullptr, bool bypassed = false);
    void removePlugin (Track& track, int slotIndex);

    /** Called just before a plugin is destroyed, so the UI can close its window. */
    std::function<void (juce::AudioProcessor*)> onPluginAboutToBeDeleted;

    //==============================================================================
    // Transport
    void play();
    void stop();
    void togglePlay()                       { isPlaying() ? stop() : play(); }
    bool isPlaying() const noexcept         { return playing.load(); }
    bool isRecording() const noexcept       { return recording.load(); }

    /** How new recordings are placed:
          normal   - a new clip on top of whatever is there
          replace  - the new recording replaces existing audio under it
          punch    - plays from a bar before the loop region and records only inside it (replace)
          loopTakes- cycles the loop region; every pass becomes a take you can choose from */
    enum class RecordMode { normal = 1, replace = 2, punch = 3, loopTakes = 4 };
    static juce::String getRecordModeName (RecordMode);
    RecordMode getRecordMode() const { return (RecordMode) juce::jlimit (1, 4, Settings::getInt (settings, Settings::recordMode, 1)); }

    /** Starts recording on all armed tracks, optionally after a count-in of a few bars.
        Returns an error message, or empty on success. */
    juce::String startRecording (int countInBars = 0);
    RecordMode getActiveRecordMode() const noexcept { return activeRecordMode; }
    int getCurrentTakeNumber() const;    // loop takes: which pass is being recorded

    /** Places a finished recording file on a track according to the record mode (used by stop; public for tests). */
    void placeRecording (Track& track, const juce::File& file, RecordMode mode, double startSec, double latencySec,
                         double loopLengthSec, double punchLengthSec);
    bool isCountingIn() const noexcept { return recording.load() && position.load() < recordStartSample; }

    double getPositionSec() const noexcept  { return (double) position.load() / sampleRate.load(); }

    /** Song details written into exported files (WAV / MP3 / OGG tags). */
    struct ProjectInfo
    {
        juce::String title, artist, album, genre, comment;
    };
    ProjectInfo info;
    double getRecordStartSec() const noexcept { return (double) recordStartSample / sampleRate.load(); }
    void   setPositionSec (double seconds);

    double getSampleRate() const noexcept   { return sampleRate.load(); }
    int    getBlockSize() const noexcept    { return blockSize; }
    double getProjectLengthSec() const;

    std::atomic<double> bpm          { 120.0 };
    std::atomic<bool>   metronomeOn  { false };

    // Loop (cycle) playback
    std::atomic<bool>   loopEnabled  { false };
    std::atomic<double> loopStartSec { 0.0 };
    std::atomic<double> loopEndSec   { 8.0 };

    // Browser preview player (plays straight to the output, bypassing the mixer)
    bool previewFile (const juce::File& file);
    void stopPreview();
    bool isPreviewing() const { return previewTransport.isPlaying(); }

    double getCpuUsage() const { return deviceManager.getCpuUsage(); }

    //==============================================================================
    // I/O
    static constexpr int maxChannels = 64;
    juce::StringArray getActiveInputNames() const;     // names of the enabled device inputs, in order
    juce::StringArray getOutputPairNames() const;      // "Out 1+2 (Speakers)", ...
    float getInputLevel (int index) const noexcept  { return juce::isPositiveAndBelow (index, maxChannels) ? inputLevel[(size_t) index] : 0.0f; }
    float getOutputLevel (int index) const noexcept { return juce::isPositiveAndBelow (index, maxChannels) ? outputLevel[(size_t) index] : 0.0f; }
    int   getNumActiveInputs() const noexcept  { return numActiveInputs; }
    int   getNumActiveOutputs() const noexcept { return numActiveOutputs; }
    /** Call regularly from the UI: turns the audio thread's peaks into smooth meter levels. */
    void updateIOMeters();
    /** Plays a quiet 440 Hz tone on an output pair (-1 = off) to check your speakers/headphones. */
    std::atomic<int> testTonePair { -1 };

    /** Folder where new recordings are written. */
    juce::File getRecordingFolder() const;
    void setProjectFolder (const juce::File& folder)   { projectFolder = folder; }

    /** True if something changed since the last save. */
    bool hasUnsavedChanges() const noexcept { return edited; }
    void markEdited (bool e = true) noexcept { edited = e; }

    //==============================================================================
    // Offline rendering (export)
    struct RenderOptions
    {
        juce::File file;
        int    trackIndex = -1;      // -1 = full mix through the master bus, otherwise one track (a stem)
        double startSec   = 0.0;
        double endSec     = 0.0;
        juce::String format = "WAV"; // "WAV", "FLAC", "OGG" or "MP3"
        int  bitDepth      = 24;
        bool floatingPoint = false;  // 32-bit float WAV
        bool normalize     = false;  // scale so the loudest peak hits normalizeDb
        float normalizeDb  = -1.0f;
        bool writeTags     = true;   // embed project info (title, artist...)
        juce::File lameExecutable;   // needed for MP3
    };

    /** Renders to a file. progress() receives 0..1 and returns false to cancel. */
    bool renderToFile (const RenderOptions& options,
                       const std::function<bool (double)>& progress,
                       juce::String& error);

    //==============================================================================
    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    // AudioPlayHead (lets plugins sync delays/LFOs to the tempo)
    juce::Optional<PositionInfo> getPosition() const override;

private:
    void handleAsyncUpdate() override;

    void prepareTrack (Track&);
    void renderTracks (juce::AudioBuffer<float>& out, int numSamples, juce::int64 pos,
                       bool realtime, int onlyTrack,
                       const float* const* input, int numInputs,
                       juce::AudioBuffer<float>* hardwareOut = nullptr);
    void renderMaster (juce::AudioBuffer<float>& mix, int numSamples, bool realtime);
    void mixClips (Track&, int numSamples, juce::int64 pos);
    bool renderPass (const RenderOptions&, juce::AudioFormatWriter* writer, float gain, float& peak,
                     const std::function<bool (double)>& progress, double progressStart, double progressScale, bool& failed);
    void processPlugins (Track&, int numSamples);
    void addMetronome (juce::AudioBuffer<float>& out, int numSamples, juce::int64 pos);
    void finishRecording();
    void resampleClipsIfNeeded();

    static std::shared_ptr<const juce::AudioBuffer<float>> resample (const juce::AudioBuffer<float>& src,
                                                                     double srcRate, double dstRate);

    juce::PropertiesFile& settings;

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;
    juce::AudioPluginFormatManager pluginFormatManager;
    juce::KnownPluginList knownPlugins;
    juce::AudioThumbnailCache thumbnailCache { 64 };
    juce::TimeSliceThread writerThread { "Recording writer" };

    juce::CriticalSection lock;
    std::vector<std::unique_ptr<Track>> tracks;
    std::unique_ptr<Track> master;

    juce::AudioBuffer<float> mixBuffer;

    std::atomic<bool> playing { false }, recording { false }, offline { false };
    std::atomic<juce::int64> position { 0 };
    std::atomic<juce::int64> renderPosition { 0 };
    std::atomic<double> sampleRate { 44100.0 };
    int blockSize = 512;
    int roundTripLatency = 0;
    juce::int64 recordStartSample = 0;

    // Metronome click state
    int    clickSamplesLeft = 0;
    double clickPhase = 0.0, clickFreq = 1000.0;

    std::array<std::atomic<float>, maxChannels> inputPeak {}, outputPeak {};
    std::array<float, maxChannels> inputLevel {}, outputLevel {};
    int numActiveInputs = 0, numActiveOutputs = 0;
    juce::AudioBuffer<float> hardwareBuffer;
    double testTonePhase = 0.0;

    RecordMode activeRecordMode = RecordMode::normal;
    juce::int64 recordEndSample = std::numeric_limits<juce::int64>::max();
    double recordLoopLengthSec = 0.0, recordPunchLengthSec = 0.0;
    bool countInClick = false;
    int nextTakeGroup = 1;
    std::atomic<juce::int64> recordedSamples { 0 };

    juce::UndoManager undoManager { 30000, 200 };
    ClipLayout pendingBefore;
    int editDepth = 0;

    juce::AudioTransportSource previewTransport;
    std::unique_ptr<juce::AudioFormatReaderSource> previewReader;
    juce::AudioBuffer<float> previewBuffer;

    juce::File projectFolder;
    bool edited = false;
    int trackCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
