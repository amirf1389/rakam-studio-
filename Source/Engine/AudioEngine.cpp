#include "Engine/AudioEngine.h"

namespace
{
    const juce::Colour trackPalette[] = {
        juce::Colour (0xff4c9aff), juce::Colour (0xff3ec27b), juce::Colour (0xfff5a524),
        juce::Colour (0xffb16cff), juce::Colour (0xffff6a3d), juce::Colour (0xff2ec5c5),
        juce::Colour (0xffff5c8a), juce::Colour (0xffc9d14a)
    };

    // Decodes a whole file into memory (max 2 channels). Returns nullptr on failure.
    std::unique_ptr<juce::AudioBuffer<float>> decodeFile (juce::AudioFormatManager& fm, const juce::File& file,
                                                          double& rateOut, juce::String& error)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));

        if (reader == nullptr)
        {
            error = "Can't open \"" + file.getFileName() + "\". Supported: WAV, AIFF, FLAC, OGG, MP3.";
            return nullptr;
        }

        const auto maxSamples = (juce::int64) (reader->sampleRate * 60.0 * 60.0); // 1 hour limit

        if (reader->lengthInSamples <= 0 || reader->lengthInSamples > maxSamples)
        {
            error = "\"" + file.getFileName() + "\" is empty or longer than 1 hour.";
            return nullptr;
        }

        const int numChannels = (int) juce::jlimit (1u, 2u, reader->numChannels);
        const int numSamples  = (int) reader->lengthInSamples;

        auto buffer = std::make_unique<juce::AudioBuffer<float>> (numChannels, numSamples);
        reader->read (buffer.get(), 0, numSamples, 0, true, numChannels > 1);
        rateOut = reader->sampleRate;
        return buffer;
    }

    std::unique_ptr<juce::AudioThumbnail> makeThumbnail (AudioEngine& engine, const juce::File& file)
    {
        auto thumb = std::make_unique<juce::AudioThumbnail> (512, engine.getFormatManager(), engine.getThumbnailCache());
        thumb->setSource (new juce::FileInputSource (file));
        return thumb;
    }

    ClipPtr copyClip (AudioEngine& engine, const AudioClip& c)
    {
        auto n = std::make_shared<AudioClip>();
        n->name = c.name;               n->file = c.file;
        n->startSec = c.startSec;       n->offsetSec = c.offsetSec;     n->lengthSec = c.lengthSec;
        n->sourceLengthSec = c.sourceLengthSec;
        n->fadeInSec = c.fadeInSec;     n->fadeOutSec = c.fadeOutSec;   n->gain = c.gain;
        n->muted = c.muted;             n->takeGroup = c.takeGroup;     n->takeNumber = c.takeNumber;
        n->audio = c.audio;             n->audioRate = c.audioRate;
        n->thumbnail = makeThumbnail (engine, c.file);
        return n;
    }

    // Equal-power style fade curve, 0..1 -> 0..1
    inline float fadeCurve (double x) { return (float) std::sin (juce::jlimit (0.0, 1.0, x) * juce::MathConstants<double>::halfPi); }

    struct LayoutAction : public juce::UndoableAction
    {
        LayoutAction (AudioEngine& e, AudioEngine::ClipLayout b, AudioEngine::ClipLayout a)
            : engine (e), before (std::move (b)), after (std::move (a)) {}
        bool perform() override { engine.applyLayout (after); return true; }
        bool undo() override    { engine.applyLayout (before); return true; }
        int getSizeInUnits() override { return 10; }
        AudioEngine& engine;
        AudioEngine::ClipLayout before, after;
    };
}

//==============================================================================
AudioEngine::AudioEngine (juce::PropertiesFile& s) : settings (s)
{
    formatManager.registerBasicFormats();
    juce::addDefaultFormatsToManager (pluginFormatManager);
    pluginFormatManager.addFormat (std::make_unique<BuiltInPluginFormat>());

    if (auto xml = settings.getXmlValue ("knownPlugins"))
        knownPlugins.recreateFromXml (*xml);

    master = std::make_unique<Track> ("Master", true);
    master->colour = Rakam::Colours::accent;
    master->outputPair = 0;

    writerThread.startThread();

    auto savedState = settings.getXmlValue ("audioDevice");
    deviceManager.initialise (2, 2, savedState.get(), true);

    // First run: prefer ASIO when an ASIO driver is installed (lowest latency on Windows)
    if (savedState == nullptr)
    {
        for (auto* type : deviceManager.getAvailableDeviceTypes())
        {
            if (type->getTypeName() == "ASIO")
            {
                type->scanForDevices();
                if (type->getDeviceNames().size() > 0)
                    deviceManager.setCurrentAudioDeviceType ("ASIO", true);
                break;
            }
        }
    }

    deviceManager.addAudioCallback (this);
}

AudioEngine::~AudioEngine()
{
    if (recording)
        finishRecording();

    deviceManager.removeAudioCallback (this);
    stopPreview();
    saveSettings();
    cancelPendingUpdate();

    tracks.clear();
    master.reset();
    writerThread.stopThread (2000);
}

void AudioEngine::saveSettings()
{
    if (auto xml = deviceManager.createStateXml())
        settings.setValue ("audioDevice", xml.get());

    if (auto xml = knownPlugins.createXml())
        settings.setValue ("knownPlugins", xml.get());

    settings.saveIfNeeded();
}

//==============================================================================
int AudioEngine::indexOf (const Track* t) const
{
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].get() == t)
            return (int) i;
    return -1;
}

Track* AudioEngine::addTrack (const juce::String& name, int insertIndex)
{
    ++trackCounter;
    auto track = std::make_unique<Track> (name.isNotEmpty() ? name : "Track " + juce::String (trackCounter));
    track->colour = trackPalette[(size_t) (trackCounter - 1) % std::size (trackPalette)];
    prepareTrack (*track);

    auto* raw = track.get();
    {
        const juce::ScopedLock sl (lock);
        if (juce::isPositiveAndBelow (insertIndex, (int) tracks.size()))
            tracks.insert (tracks.begin() + insertIndex, std::move (track));
        else
            tracks.push_back (std::move (track));
    }

    markEdited();
    sendChangeMessage();
    return raw;
}

Track* AudioEngine::duplicateTrack (int index)
{
    auto* src = getTrack (index);
    if (src == nullptr)
        return nullptr;

    auto* t = addTrack (src->name + " copy", index + 1);
    t->colour = src->colour;
    t->gain = src->gain.load();
    t->pan = src->pan.load();
    t->mute = src->mute.load();
    t->inputIndex = src->inputIndex;
    t->stereoInput = src->stereoInput;
    t->outputPair = src->outputPair.load();

    for (auto& slot : src->plugins)
    {
        juce::MemoryBlock state;
        slot->instance->getStateInformation (state);
        juce::String error;
        addPlugin (*t, slot->instance->getPluginDescription(), error, &state, slot->bypassed);
    }

    beginClipEdit();
    {
        std::vector<ClipPtr> copies;
        for (auto& c : src->clips)
            copies.push_back (copyClip (*this, *c));
        const juce::ScopedLock sl (lock);
        t->clips = std::move (copies);
    }
    endClipEdit ("Duplicate track");
    sendChangeMessage();
    return t;
}

void AudioEngine::moveTrack (int fromIndex, int toIndex)
{
    const int n = getNumTracks();
    if (! juce::isPositiveAndBelow (fromIndex, n) || ! juce::isPositiveAndBelow (toIndex, n) || fromIndex == toIndex)
        return;

    {
        const juce::ScopedLock sl (lock);
        auto t = std::move (tracks[(size_t) fromIndex]);
        tracks.erase (tracks.begin() + fromIndex);
        tracks.insert (tracks.begin() + toIndex, std::move (t));
    }
    markEdited();
    sendChangeMessage();
}

void AudioEngine::removeTrack (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumTracks()))
        return;

    if (onPluginAboutToBeDeleted)
        for (auto& p : tracks[(size_t) index]->plugins)
            onPluginAboutToBeDeleted (p->instance.get());

    std::unique_ptr<Track> removed;
    {
        const juce::ScopedLock sl (lock);
        removed = std::move (tracks[(size_t) index]);
        tracks.erase (tracks.begin() + index);
    }

    removed.reset(); // destroy plugins & flush any recorder outside the lock
    undoManager.clearUndoHistory();   // undo steps may refer to the deleted track
    markEdited();
    sendChangeMessage();
}

void AudioEngine::clearProject()
{
    stop();

    while (getNumTracks() > 0)
        removeTrack (getNumTracks() - 1);

    while (! master->plugins.empty())
        removePlugin (*master, (int) master->plugins.size() - 1);

    master->gain = 1.0f;
    bpm = Settings::getDouble (settings, Settings::defaultBpm, 120.0);
    info = {};
    metronomeOn = false;
    trackCounter = 0;
    position = 0;
    projectFolder = juce::File();
    sections.clear();
    nextTakeGroup = 1;
    loopEnabled = false;
    loopStartSec = 0.0;
    loopEndSec = 8.0;
    undoManager.clearUndoHistory();
    edited = false;
    sendChangeMessage();
}

//==============================================================================
std::shared_ptr<const juce::AudioBuffer<float>> AudioEngine::resample (const juce::AudioBuffer<float>& src,
                                                                       double srcRate, double dstRate)
{
    if (std::abs (srcRate - dstRate) < 0.5)
        return std::make_shared<const juce::AudioBuffer<float>> (src);

    const double ratio = srcRate / dstRate;
    const int outLength = (int) std::ceil (src.getNumSamples() / ratio);
    auto out = std::make_shared<juce::AudioBuffer<float>> (src.getNumChannels(), outLength);

    for (int ch = 0; ch < src.getNumChannels(); ++ch)
    {
        juce::LagrangeInterpolator interpolator;
        interpolator.process (ratio, src.getReadPointer (ch), out->getWritePointer (ch),
                              outLength, src.getNumSamples(), 0);
    }

    return out;
}

ClipPtr AudioEngine::loadClip (const juce::File& file, juce::String& error)
{
    double fileRate = 44100.0;
    auto decoded = decodeFile (formatManager, file, fileRate, error);

    if (decoded == nullptr)
        return nullptr;

    auto clip = std::make_shared<AudioClip>();
    clip->name            = file.getFileNameWithoutExtension();
    clip->file            = file;
    clip->sourceLengthSec = decoded->getNumSamples() / fileRate;
    clip->lengthSec       = clip->sourceLengthSec;
    clip->audioRate       = sampleRate.load();
    clip->audio           = resample (*decoded, fileRate, clip->audioRate);
    clip->thumbnail       = makeThumbnail (*this, file);
    return clip;
}

ClipPtr AudioEngine::importFile (const juce::File& file, Track& track, double startSec, juce::String& error)
{
    auto clip = loadClip (file, error);

    if (clip != nullptr)
    {
        clip->startSec = juce::jmax (0.0, startSec);
        addClip (track, clip);
    }

    return clip;
}

void AudioEngine::addClip (Track& track, ClipPtr clip)
{
    beginClipEdit();
    {
        const juce::ScopedLock sl (lock);
        track.clips.push_back (std::move (clip));
    }
    endClipEdit ("Add clip");
    markEdited();
    sendChangeMessage();
}

void AudioEngine::removeClip (Track& track, const AudioClip* clip)
{
    ClipPtr removed;
    beginClipEdit();
    {
        const juce::ScopedLock sl (lock);
        auto it = std::find_if (track.clips.begin(), track.clips.end(), [clip] (auto& c) { return c.get() == clip; });
        if (it != track.clips.end())
        {
            removed = *it;
            track.clips.erase (it);
        }
    }
    endClipEdit ("Delete clip");
    markEdited();
    sendChangeMessage();
}

void AudioEngine::splitClip (Track& track, const AudioClip* clip, double atSec)
{
    auto it = std::find_if (track.clips.begin(), track.clips.end(), [clip] (auto& c) { return c.get() == clip; });
    if (it == track.clips.end())
        return;

    auto& first = *it;
    if (atSec <= first->startSec + 0.01 || atSec >= first->endSec() - 0.01)
        return;

    auto second = copyClip (*this, *first);
    second->startSec   = atSec;
    second->offsetSec  = first->offsetSec + (atSec - first->startSec);
    second->lengthSec  = first->endSec() - atSec;
    second->fadeInSec  = 0.0;

    beginClipEdit();
    {
        const juce::ScopedLock sl (lock);
        first->lengthSec  = atSec - first->startSec;
        first->fadeOutSec = 0.0;
        track.clips.push_back (second);
    }
    endClipEdit ("Split clip");
    markEdited();
    sendChangeMessage();
}

ClipPtr AudioEngine::duplicateClip (Track& track, const AudioClip* clip)
{
    auto it = std::find_if (track.clips.begin(), track.clips.end(), [clip] (auto& c) { return c.get() == clip; });
    if (it == track.clips.end())
        return nullptr;

    auto copy = copyClip (*this, **it);
    copy->startSec = (*it)->endSec();

    beginClipEdit();
    {
        const juce::ScopedLock sl (lock);
        track.clips.push_back (copy);
    }
    endClipEdit ("Duplicate clip");
    markEdited();
    sendChangeMessage();
    return copy;
}

void AudioEngine::normalizeClip (AudioClip& clip)
{
    auto audio = clip.audio;
    if (audio == nullptr)
        return;

    const double rate = clip.audioRate;
    const int start = juce::jlimit (0, audio->getNumSamples(), (int) (clip.offsetSec * rate));
    const int num = juce::jlimit (0, audio->getNumSamples() - start, (int) (clip.lengthSec * rate));
    const float peak = num > 0 ? audio->getMagnitude (start, num) : 0.0f;

    if (peak < 1.0e-6f)
        return;

    beginClipEdit();
    {
        const juce::ScopedLock sl (lock);
        clip.gain = juce::Decibels::decibelsToGain (-0.3f) / peak;
    }
    endClipEdit ("Normalize clip");
    markEdited();
    sendChangeMessage();
}

//==============================================================================
AudioEngine::ClipLayout AudioEngine::captureLayout() const
{
    ClipLayout layout;
    for (auto& t : tracks)
    {
        std::vector<ClipState> states;
        for (auto& c : t->clips)
            states.push_back ({ c, c->startSec, c->offsetSec, c->lengthSec, c->fadeInSec, c->fadeOutSec, c->gain, c->muted });
        layout.tracks.emplace_back (t.get(), std::move (states));
    }
    layout.sections = sections;
    return layout;
}

void AudioEngine::applyLayout (const ClipLayout& layout)
{
    {
        const juce::ScopedLock sl (lock);
        for (auto& [track, states] : layout.tracks)
        {
            if (indexOf (track) < 0)
                continue;

            track->clips.clear();
            for (auto& s : states)
            {
                s.clip->startSec   = s.start;
                s.clip->offsetSec  = s.offset;
                s.clip->lengthSec  = s.length;
                s.clip->fadeInSec  = s.fadeIn;
                s.clip->fadeOutSec = s.fadeOut;
                s.clip->gain       = s.gain;
                s.clip->muted      = s.muted;
                track->clips.push_back (s.clip);
            }
        }
    }
    sections = layout.sections;
    markEdited();
    sendChangeMessage();
}

void AudioEngine::beginClipEdit()
{
    if (editDepth++ == 0)
        pendingBefore = captureLayout();
}

void AudioEngine::endClipEdit (const juce::String& actionName)
{
    if (editDepth == 0 || --editDepth > 0)
        return;

    auto after = captureLayout();
    if (after == pendingBefore)
        return;

    undoManager.beginNewTransaction (actionName);
    undoManager.perform (new LayoutAction (*this, std::move (pendingBefore), std::move (after)), actionName);
    pendingBefore = {};
}

bool AudioEngine::undo()
{
    const bool ok = undoManager.undo();
    sendChangeMessage();
    return ok;
}

bool AudioEngine::redo()
{
    const bool ok = undoManager.redo();
    sendChangeMessage();
    return ok;
}

//==============================================================================
bool AudioEngine::previewFile (const juce::File& file)
{
    stopPreview();

    auto* reader = formatManager.createReaderFor (file);
    if (reader == nullptr)
        return false;

    const double fileRate = reader->sampleRate;
    auto source = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    previewTransport.setSource (source.get(), 32768, &writerThread, fileRate, 2);
    {
        const juce::ScopedLock sl (lock);
        previewReader = std::move (source);
    }
    previewTransport.setPosition (0.0);
    previewTransport.start();
    return true;
}

void AudioEngine::stopPreview()
{
    previewTransport.stop();
    previewTransport.setSource (nullptr);
    const juce::ScopedLock sl (lock);
    previewReader.reset();
}

//==============================================================================
juce::String AudioEngine::suggestSectionName (int index)
{
    static const char* names[] = { "Intro", "Verse 1", "Chorus", "Verse 2", "Chorus", "Bridge", "Chorus", "Outro" };
    return juce::isPositiveAndBelow (index, 8) ? juce::String (names[index]) : "Section " + juce::String (index + 1);
}

juce::Colour AudioEngine::sectionColour (const juce::String& name)
{
    const auto n = name.toLowerCase();
    if (n.contains ("intro"))  return juce::Colour (0xff6b7a99);
    if (n.contains ("verse"))  return juce::Colour (0xff4c9aff);
    if (n.contains ("pre"))    return juce::Colour (0xff2ec5c5);
    if (n.contains ("chorus") || n.contains ("hook")) return juce::Colour (0xffff6a3d);
    if (n.contains ("bridge")) return juce::Colour (0xffb16cff);
    if (n.contains ("drop"))   return juce::Colour (0xffff5c8a);
    if (n.contains ("break"))  return juce::Colour (0xff3ec27b);
    if (n.contains ("outro"))  return juce::Colour (0xff8b93a3);
    return juce::Colour (0xfff5a524);
}

int AudioEngine::addSection (const juce::String& name, double startSec, double endSec)
{
    if (endSec <= startSec)
        return -1;

    beginClipEdit();
    Section sec { name, startSec, endSec, sectionColour (name) };
    auto it = std::lower_bound (sections.begin(), sections.end(), startSec,
                                [] (const Section& a, double t) { return a.startSec < t; });
    const int index = (int) std::distance (sections.begin(), it);
    sections.insert (it, sec);
    endClipEdit ("Add section");
    markEdited();
    sendChangeMessage();
    return index;
}

void AudioEngine::removeSection (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) sections.size()))
        return;
    beginClipEdit();
    sections.erase (sections.begin() + index);
    endClipEdit ("Remove section");
    markEdited();
    sendChangeMessage();
}

void AudioEngine::setSection (int index, const Section& newValue)
{
    if (! juce::isPositiveAndBelow (index, (int) sections.size()) || newValue.endSec <= newValue.startSec)
        return;
    beginClipEdit();
    sections[(size_t) index] = newValue;
    std::stable_sort (sections.begin(), sections.end(), [] (const Section& a, const Section& b) { return a.startSec < b.startSec; });
    endClipEdit ("Edit section");
    markEdited();
    sendChangeMessage();
}

int AudioEngine::sectionAt (double sec) const
{
    for (int i = (int) sections.size(); --i >= 0;)
        if (sec >= sections[(size_t) i].startSec && sec < sections[(size_t) i].endSec)
            return i;
    return -1;
}

void AudioEngine::clearRange (Track& track, double fromSec, double toSec)
{
    if (toSec <= fromSec)
        return;

    // Split clips that cross the edges, then remove everything inside
    std::vector<AudioClip*> crossing;
    for (auto& c : track.clips)
        if ((c->startSec < fromSec && c->endSec() > fromSec) || (c->startSec < toSec && c->endSec() > toSec))
            crossing.push_back (c.get());

    for (auto* c : crossing)
    {
        if (c->startSec < fromSec && c->endSec() > fromSec)
            splitClip (track, c, fromSec);
    }
    crossing.clear();
    for (auto& c : track.clips)
        if (c->startSec < toSec && c->endSec() > toSec)
            crossing.push_back (c.get());
    for (auto* c : crossing)
        splitClip (track, c, toSec);

    const juce::ScopedLock sl (lock);
    track.clips.erase (std::remove_if (track.clips.begin(), track.clips.end(), [fromSec, toSec] (const ClipPtr& c)
    {
        return c->startSec >= fromSec - 0.0005 && c->endSec() <= toSec + 0.0005;
    }), track.clips.end());
}

void AudioEngine::shiftFrom (double fromSec, double delta)
{
    {
        const juce::ScopedLock sl (lock);
        for (auto& t : tracks)
            for (auto& c : t->clips)
                if (c->startSec >= fromSec - 0.0005)
                    c->startSec = juce::jmax (0.0, c->startSec + delta);
    }

    for (auto& sec : sections)
    {
        if (sec.startSec >= fromSec - 0.0005)
        {
            sec.startSec += delta;
            sec.endSec += delta;
        }
    }
}

void AudioEngine::duplicateSection (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) sections.size()))
        return;

    const auto sec = sections[(size_t) index];
    const double s0 = sec.startSec, s1 = sec.endSec, len = sec.length();

    beginClipEdit();

    // Cut clips at the section edges so the section's audio stands alone
    for (auto& t : tracks)
    {
        std::vector<AudioClip*> toSplit;
        for (auto& c : t->clips)
            if (c->startSec < s1 && c->endSec() > s1)
                toSplit.push_back (c.get());
        for (auto* c : toSplit)
            splitClip (*t, c, s1);

        toSplit.clear();
        for (auto& c : t->clips)
            if (c->startSec < s0 && c->endSec() > s0)
                toSplit.push_back (c.get());
        for (auto* c : toSplit)
            splitClip (*t, c, s0);
    }

    // Make room, then copy
    shiftFrom (s1, len);

    for (auto& t : tracks)
    {
        std::vector<ClipPtr> copies;
        for (auto& c : t->clips)
        {
            if (c->startSec >= s0 - 0.0005 && c->startSec < s1 - 0.0005)
            {
                auto copy = copyClip (*this, *c);
                copy->startSec += len;
                copies.push_back (copy);
            }
        }
        const juce::ScopedLock sl (lock);
        for (auto& c : copies)
            t->clips.push_back (c);
    }

    auto copySection = sec;
    copySection.startSec = s1;
    copySection.endSec = s1 + len;
    sections.insert (sections.begin() + index + 1, copySection);

    endClipEdit ("Duplicate section");
    markEdited();
    sendChangeMessage();
}

void AudioEngine::deleteSectionWithAudio (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) sections.size()))
        return;

    const auto sec = sections[(size_t) index];

    beginClipEdit();
    for (auto& t : tracks)
        clearRange (*t, sec.startSec, sec.endSec);

    sections.erase (sections.begin() + index);
    shiftFrom (sec.endSec, -sec.length());
    endClipEdit ("Delete section");
    markEdited();
    sendChangeMessage();
}

void AudioEngine::setClipMuted (AudioClip& clip, bool muted)
{
    beginClipEdit();
    {
        const juce::ScopedLock sl (lock);
        clip.muted = muted;
    }
    endClipEdit (muted ? "Mute clip" : "Unmute clip");
    markEdited();
    sendChangeMessage();
}

void AudioEngine::chooseTake (Track& track, int takeGroup, int takeNumber)
{
    beginClipEdit();
    {
        const juce::ScopedLock sl (lock);
        for (auto& c : track.clips)
            if (c->takeGroup == takeGroup)
                c->muted = c->takeNumber != takeNumber;
    }
    endClipEdit ("Choose take " + juce::String (takeNumber));
    markEdited();
    sendChangeMessage();
}

void AudioEngine::flattenTakes (Track& track, int takeGroup)
{
    beginClipEdit();
    {
        const juce::ScopedLock sl (lock);
        track.clips.erase (std::remove_if (track.clips.begin(), track.clips.end(),
                                           [takeGroup] (const ClipPtr& c) { return c->takeGroup == takeGroup && c->muted; }),
                           track.clips.end());
        for (auto& c : track.clips)
            if (c->takeGroup == takeGroup)
                c->takeGroup = c->takeNumber = 0;
    }
    endClipEdit ("Keep active take");
    markEdited();
    sendChangeMessage();
}

//==============================================================================
juce::StringArray AudioEngine::getActiveInputNames() const
{
    juce::StringArray result;
    if (auto* d = deviceManager.getCurrentAudioDevice())
    {
        const auto names = d->getInputChannelNames();
        const auto active = d->getActiveInputChannels();
        for (int i = 0; i < names.size(); ++i)
            if (active[i])
                result.add (names[i]);
    }
    return result;
}

juce::StringArray AudioEngine::getOutputPairNames() const
{
    juce::StringArray result;
    if (auto* d = deviceManager.getCurrentAudioDevice())
    {
        const auto names = d->getOutputChannelNames();
        const auto active = d->getActiveOutputChannels();
        juce::StringArray activeNames;
        for (int i = 0; i < names.size(); ++i)
            if (active[i])
                activeNames.add (names[i]);

        for (int i = 0; i < activeNames.size(); i += 2)
            result.add ("Out " + juce::String (i + 1) + (i + 1 < activeNames.size() ? "+" + juce::String (i + 2) : juce::String())
                        + "  (" + activeNames[i] + ")");
    }
    if (result.isEmpty())
        result.add ("Out 1+2");
    return result;
}

void AudioEngine::updateIOMeters()
{
    for (size_t i = 0; i < (size_t) maxChannels; ++i)
    {
        inputLevel[i]  = juce::jmax (inputPeak[i].exchange (0.0f),  inputLevel[i] * 0.8f);
        outputLevel[i] = juce::jmax (outputPeak[i].exchange (0.0f), outputLevel[i] * 0.8f);
    }
}

Track* AudioEngine::findTrackForClip (const AudioClip* clip) const
{
    for (auto& t : tracks)
        for (auto& c : t->clips)
            if (c.get() == clip)
                return t.get();
    return nullptr;
}

void AudioEngine::resampleClipsIfNeeded()
{
    const double rate = sampleRate.load();

    for (auto& t : tracks)
    {
        for (auto& clip : t->clips)
        {
            if (std::abs (clip->audioRate - rate) < 0.5)
                continue;

            double fileRate = 44100.0;
            juce::String error;
            if (auto decoded = decodeFile (formatManager, clip->file, fileRate, error))
            {
                auto converted = resample (*decoded, fileRate, rate);
                const juce::ScopedLock sl (lock);
                clip->audio = std::move (converted);
                clip->audioRate = rate;
            }
        }
    }
}

double AudioEngine::getProjectLengthSec() const
{
    double end = 0.0;
    for (auto& t : tracks)
        end = juce::jmax (end, t->getEndSec());
    return end;
}

//==============================================================================
bool AudioEngine::addPlugin (Track& track, const juce::PluginDescription& desc, juce::String& error,
                             const juce::MemoryBlock* state, bool bypassed)
{
    auto instance = pluginFormatManager.createPluginInstance (desc, sampleRate.load(), blockSize, error);

    if (instance == nullptr)
    {
        if (error.isEmpty())
            error = "Couldn't load " + desc.name;
        return false;
    }

    // Prefer a plain stereo in / stereo out layout
    auto layout = instance->getBusesLayout();
    if (! layout.inputBuses.isEmpty())  layout.inputBuses.getReference (0)  = juce::AudioChannelSet::stereo();
    if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    instance->setBusesLayout (layout);
    instance->enableAllBuses();

    if (state != nullptr && state->getSize() > 0)
        instance->setStateInformation (state->getData(), (int) state->getSize());

    instance->setPlayHead (this);
    instance->prepareToPlay (sampleRate.load(), blockSize);

    auto slot = std::make_unique<PluginSlot>();
    slot->instance = std::move (instance);
    slot->bypassed = bypassed;

    {
        const juce::ScopedLock sl (lock);
        track.plugins.push_back (std::move (slot));
        track.buffer.setSize (track.getRequiredChannels(), blockSize, false, true, true);
    }

    markEdited();
    sendChangeMessage();
    return true;
}

void AudioEngine::removePlugin (Track& track, int slotIndex)
{
    if (! juce::isPositiveAndBelow (slotIndex, (int) track.plugins.size()))
        return;

    if (onPluginAboutToBeDeleted)
        onPluginAboutToBeDeleted (track.plugins[(size_t) slotIndex]->instance.get());

    std::unique_ptr<PluginSlot> removed;
    {
        const juce::ScopedLock sl (lock);
        removed = std::move (track.plugins[(size_t) slotIndex]);
        track.plugins.erase (track.plugins.begin() + slotIndex);
    }

    removed.reset();
    markEdited();
    sendChangeMessage();
}

//==============================================================================
void AudioEngine::play()
{
    playing = true;
    sendChangeMessage();
}

void AudioEngine::stop()
{
    if (! playing && ! recording)
        position = 0;          // second press of Stop returns to the start

    playing = false;

    if (recording)
        finishRecording();

    if (position.load() < 0)
        position = 0;

    sendChangeMessage();
}

void AudioEngine::setPositionSec (double seconds)
{
    if (recording)
        return;

    position = (juce::int64) std::llround (juce::jmax (0.0, seconds) * sampleRate.load());
    sendChangeMessage();
}

juce::File AudioEngine::getRecordingFolder() const
{
    if (projectFolder != juce::File())
        return projectFolder.getChildFile ("Audio");

    return Rakam::getUserFolder().getChildFile ("Recordings");
}

juce::String AudioEngine::startRecording (int countInBars)
{
    if (recording)
        return {};

    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return "No audio device is open. Choose one in Options > Audio / ASIO Settings.";

    if (device->getActiveInputChannels().countNumberOfSetBits() == 0)
        return "No input channels are enabled. Turn on your inputs in Options > Audio / ASIO Settings.";

    const auto mode = getRecordMode();
    const double loopLen = loopEndSec.load() - loopStartSec.load();
    if ((mode == RecordMode::punch || mode == RecordMode::loopTakes) && loopLen < 0.5)
        return "This record mode uses the loop region. Drag in the thin strip under the bar numbers to mark where to record.";

    auto folder = getRecordingFolder();
    if (! folder.createDirectory())
        return "Can't create the recording folder: " + folder.getFullPathName();

    // Recording file format from Settings: 1 WAV 24, 2 WAV 32 float, 3 WAV 16, 4 FLAC 24
    const int recFormat = Settings::getInt (settings, Settings::recordFormat, 1);
    juce::WavAudioFormat wav;
    juce::FlacAudioFormat flac;
    juce::AudioFormat& format = recFormat == 4 ? static_cast<juce::AudioFormat&> (flac) : wav;
    const juce::String extension = recFormat == 4 ? ".flac" : ".wav";
    auto writerOptions = juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate.load())
                                                         .withBitsPerSample (recFormat == 3 ? 16 : 24);
    if (recFormat == 2)
        writerOptions = writerOptions.withBitsPerSample (32)
                                     .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    std::vector<std::pair<Track*, std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter>>> writers;

    for (auto& t : tracks)
    {
        if (! t->armed)
            continue;

        const int channels = t->stereoInput ? 2 : 1;
        auto file = folder.getNonexistentChildFile (juce::File::createLegalFileName (t->name) + "_" + stamp, extension);

        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        if (stream == nullptr)
            return "Can't write to " + file.getFullPathName();

        auto writer = format.createWriterFor (stream, writerOptions.withNumChannels (channels));
        if (writer == nullptr)
            return "Couldn't create a " + extension.substring (1).toUpperCase() + " file for recording at this sample rate.";

        t->recordingFile = file;
        t->recordingChannels = channels;
        writers.emplace_back (t.get(), std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writerThread, 65536));
    }

    if (writers.empty())
        return "Arm at least one track first (press its R button), then press Record.";

    {
        const juce::ScopedLock sl (lock);
        for (auto& [track, w] : writers)
            track->recorder = std::move (w);

        const double rate = sampleRate.load();
        const auto barSamples = (juce::int64) (4.0 * 60.0 / bpm.load() * rate);

        activeRecordMode = mode;
        recordEndSample = std::numeric_limits<juce::int64>::max();
        recordLoopLengthSec = recordPunchLengthSec = 0.0;
        countInClick = countInBars > 0;

        if (mode == RecordMode::punch || mode == RecordMode::loopTakes)
        {
            recordStartSample = (juce::int64) std::llround (loopStartSec.load() * rate);
            if (mode == RecordMode::punch)
            {
                recordEndSample = (juce::int64) std::llround (loopEndSec.load() * rate);
                recordPunchLengthSec = loopLen;
            }
            else
            {
                recordLoopLengthSec = loopLen;
                loopEnabled = true;
            }

            // Punch always gets at least one bar of pre-roll so you hear the song before it records
            const int preBars = mode == RecordMode::punch ? juce::jmax (1, countInBars) : countInBars;
            position = recordStartSample - preBars * barSamples;
        }
        else
        {
            recordStartSample = position.load();
            if (countInBars > 0)
                position = recordStartSample - countInBars * barSamples;
        }

        recording = true;
        playing = true;
    }

    markEdited();
    sendChangeMessage();
    return {};
}

juce::String AudioEngine::getRecordModeName (RecordMode m)
{
    switch (m)
    {
        case RecordMode::replace:   return "Replace";
        case RecordMode::punch:     return "Punch in/out (loop range)";
        case RecordMode::loopTakes: return "Loop takes (loop range)";
        case RecordMode::normal:    break;
    }
    return "Normal (layer on top)";
}

int AudioEngine::getCurrentTakeNumber() const
{
    if (! recording || activeRecordMode != RecordMode::loopTakes || recordLoopLengthSec <= 0.0)
        return 0;

    const double elapsed = (double) (recordedSamples.load()) / sampleRate.load();
    return (int) (elapsed / recordLoopLengthSec) + 1;
}

void AudioEngine::finishRecording()
{
    std::vector<std::pair<Track*, juce::File>> finished;
    std::vector<std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter>> writers;

    {
        const juce::ScopedLock sl (lock);
        recording = false;

        for (auto& t : tracks)
        {
            if (t->recorder != nullptr)
            {
                writers.push_back (std::move (t->recorder));
                finished.emplace_back (t.get(), t->recordingFile);
            }
        }
    }

    writers.clear(); // flushes and closes the files

    const double rate = sampleRate.load();
    const double startSec = (double) recordStartSample / rate;
    const double latencySec = (double) roundTripLatency / rate; // shifts the take so it lines up with what you heard

    beginClipEdit();
    for (auto& [track, file] : finished)
        placeRecording (*track, file, activeRecordMode, startSec, latencySec, recordLoopLengthSec, recordPunchLengthSec);
    endClipEdit (activeRecordMode == RecordMode::loopTakes ? "Record takes" : "Record");

    recordedSamples = 0;
    sendChangeMessage();
}

void AudioEngine::placeRecording (Track& track, const juce::File& file, RecordMode mode, double startSec, double latencySec,
                                  double loopLengthSec, double punchLengthSec)
{
    juce::String error;
    auto clip = loadClip (file, error);
    if (clip == nullptr)
    {
        file.deleteFile();   // nothing usable was recorded (e.g. stopped during the count-in)
        return;
    }

    const double usable = clip->sourceLengthSec - latencySec;
    if (usable < 0.02)
    {
        file.deleteFile();
        return;
    }

    beginClipEdit();

    if (mode == RecordMode::loopTakes && loopLengthSec > 0.0)
    {
        int passes = (int) (usable / loopLengthSec);
        const double remainder = usable - passes * loopLengthSec;
        if (remainder > juce::jmin (1.0, loopLengthSec * 0.5) || passes == 0)
            ++passes;   // keep a partial last pass if it's long enough to matter

        int group = 1;
        for (auto& t : tracks)
            for (auto& c : t->clips)
                group = juce::jmax (group, c->takeGroup + 1);
        for (int i = 0; i < passes; ++i)
        {
            auto take = i == 0 ? clip : copyClip (*this, *clip);   // shares the decoded audio
            if (take == nullptr)
                break;
            take->name       = track.name + " take " + juce::String (i + 1);
            take->startSec   = startSec;
            take->offsetSec  = latencySec + i * loopLengthSec;
            take->lengthSec  = juce::jmin (loopLengthSec, take->sourceLengthSec - take->offsetSec);
            take->takeGroup  = group;
            take->takeNumber = i + 1;
            take->muted      = i != passes - 1;   // the last take plays; the others wait to be chosen
            if (take->lengthSec > 0.01)
                addClip (track, take);
        }
    }
    else
    {
        clip->startSec  = startSec;
        clip->offsetSec = juce::jmin (latencySec, clip->sourceLengthSec);
        clip->lengthSec = juce::jmax (0.0, clip->sourceLengthSec - clip->offsetSec);

        if (mode == RecordMode::punch && punchLengthSec > 0.0)
        {
            clip->lengthSec = juce::jmin (clip->lengthSec, punchLengthSec);
            clip->fadeInSec = clip->fadeOutSec = 0.005;   // click-free punch edges
        }

        if (mode == RecordMode::replace || mode == RecordMode::punch)
            clearRange (track, clip->startSec, clip->endSec());

        addClip (track, clip);
    }

    endClipEdit ("Record");
}

//==============================================================================
void AudioEngine::prepareTrack (Track& t)
{
    t.buffer.setSize (t.getRequiredChannels(), blockSize, false, true, false);

    for (auto& p : t.plugins)
    {
        p->instance->setPlayHead (this);
        p->instance->prepareToPlay (sampleRate.load(), blockSize);
    }
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const juce::ScopedLock sl (lock);

    const double oldRate = sampleRate.load();
    const double newRate = device->getCurrentSampleRate();

    // Keep the playhead at the same time position when the sample rate changes
    position = (juce::int64) ((double) position.load() * newRate / oldRate);

    sampleRate = newRate;
    blockSize  = juce::jmax (16, device->getCurrentBufferSizeSamples());
    roundTripLatency = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();

    mixBuffer.setSize (2, blockSize);
    numActiveInputs  = device->getActiveInputChannels().countNumberOfSetBits();
    numActiveOutputs = device->getActiveOutputChannels().countNumberOfSetBits();
    hardwareBuffer.setSize (juce::jmax (2, numActiveOutputs), blockSize);
    previewBuffer.setSize (2, blockSize);
    previewTransport.prepareToPlay (blockSize, newRate);

    for (auto& t : tracks)
        prepareTrack (*t);
    prepareTrack (*master);

    triggerAsyncUpdate(); // re-decode clips at the new sample rate on the message thread
}

void AudioEngine::audioDeviceStopped()
{
    previewTransport.releaseResources();
    const juce::ScopedLock sl (lock);

    for (auto& t : tracks)
        for (auto& p : t->plugins)
            p->instance->releaseResources();

    for (auto& p : master->plugins)
        p->instance->releaseResources();
}

void AudioEngine::handleAsyncUpdate()
{
    resampleClipsIfNeeded();
    sendChangeMessage();
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* input, int numInputs,
                                                    float* const* outputs, int numOutputs,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    const juce::ScopedTryLock sl (lock);

    if (! sl.isLocked() || offline)
    {
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputs[ch], numSamples);
        return;
    }

    auto pos = position.load();

    // 1. Write armed inputs to disk
    if (recording)
    {
        for (auto& t : tracks)
        {
            if (t->recorder == nullptr || numInputs == 0)
                continue;

            const int a = juce::jlimit (0, numInputs - 1, t->inputIndex);
            const int b = juce::jlimit (0, numInputs - 1, t->inputIndex + 1);
            const float* chans[2] = { input[a], input[t->stereoInput ? b : a] };

            const auto offset = recordStartSample - pos;   // > 0 while counting in / pre-rolling
            const int skip = (int) juce::jlimit ((juce::int64) 0, (juce::int64) numSamples, offset);
            const int last = activeRecordMode == RecordMode::punch
                                 ? (int) juce::jlimit ((juce::int64) 0, (juce::int64) numSamples, recordEndSample - pos)
                                 : numSamples;

            if (chans[0] != nullptr && chans[1] != nullptr && last > skip)
            {
                const float* shifted[2] = { chans[0] + skip, chans[1] + skip };
                t->recorder->write (shifted, last - skip);
            }
        }

        const auto offset = recordStartSample - pos;
        if (offset < numSamples && pos < recordEndSample)
            recordedSamples += numSamples - (int) juce::jmax ((juce::int64) 0, offset);
    }

    // Input meters
    for (int ch = 0; ch < juce::jmin (numInputs, maxChannels); ++ch)
        if (input[ch] != nullptr)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (input[ch], numSamples);
            const float peak = juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd()));
            auto& slot = inputPeak[(size_t) ch];
            if (peak > slot.load())
                slot = peak;
        }

    // 2. Mix all tracks, wrapping around the loop region if cycling
    mixBuffer.setSize (2, numSamples, false, false, true);
    mixBuffer.clear();

    const double rate = sampleRate.load();
    const auto loopStart = (juce::int64) std::llround (loopStartSec.load() * rate);
    const auto loopEnd   = (juce::int64) std::llround (loopEndSec.load() * rate);
    const bool looping = playing && loopEnabled && loopEnd - loopStart > (juce::int64) (0.05 * rate)
                         && (! recording || activeRecordMode == RecordMode::loopTakes);

    hardwareBuffer.setSize (juce::jmax (2, numOutputs), numSamples, false, false, true);
    hardwareBuffer.clear();

    std::array<const float*, 64> inputs {};
    const int numIn = juce::jmin (numInputs, (int) inputs.size());
    int done = 0;

    while (done < numSamples)
    {
        int n = numSamples - done;

        if (looping)
        {
            if (pos >= loopEnd)
                pos = loopStart;
            n = (int) juce::jmin ((juce::int64) n, loopEnd - pos);
        }

        for (int k = 0; k < numIn; ++k)
            inputs[(size_t) k] = input[k] != nullptr ? input[k] + done : nullptr;

        juce::AudioBuffer<float> part (mixBuffer.getArrayOfWritePointers(), 2, done, n);
        juce::AudioBuffer<float> hwPart (hardwareBuffer.getArrayOfWritePointers(), hardwareBuffer.getNumChannels(), done, n);
        renderTracks (part, n, pos, true, -1, inputs.data(), numIn, &hwPart);

        if (playing && (metronomeOn || (recording && countInClick && pos < recordStartSample)))
            addMetronome (part, n, pos);

        if (playing)
            pos += n;
        done += n;
    }

    renderMaster (mixBuffer, numSamples, true);

    // Browser preview goes straight to the speakers
    if (previewTransport.isPlaying())
    {
        previewBuffer.setSize (2, numSamples, false, false, true);
        juce::AudioSourceChannelInfo previewInfo (&previewBuffer, 0, numSamples);
        previewTransport.getNextAudioBlock (previewInfo);
        mixBuffer.addFrom (0, 0, previewBuffer, 0, 0, numSamples, 0.8f);
        mixBuffer.addFrom (1, 0, previewBuffer, 1, 0, numSamples, 0.8f);
    }

    // 3. Master -> its output pair, plus any tracks sent straight to other outputs, plus the test tone
    {
        const int pair = juce::jmax (0, master->outputPair.load());
        const int l = juce::jmin (pair * 2, hardwareBuffer.getNumChannels() - 1);
        const int r = juce::jmin (pair * 2 + 1, hardwareBuffer.getNumChannels() - 1);
        hardwareBuffer.addFrom (l, 0, mixBuffer, 0, 0, numSamples);
        hardwareBuffer.addFrom (r, 0, mixBuffer, l == r ? 0 : 1, 0, numSamples);
    }

    if (const int tonePair = testTonePair.load(); tonePair >= 0 && tonePair * 2 < hardwareBuffer.getNumChannels())
    {
        const double inc = juce::MathConstants<double>::twoPi * 440.0 / rate;
        const int r = juce::jmin (tonePair * 2 + 1, hardwareBuffer.getNumChannels() - 1);
        for (int i = 0; i < numSamples; ++i)
        {
            const float v = 0.125f * (float) std::sin (testTonePhase);
            testTonePhase += inc;
            hardwareBuffer.addSample (tonePair * 2, i, v);
            if (r != tonePair * 2)
                hardwareBuffer.addSample (r, i, v);
        }
        testTonePhase = std::fmod (testTonePhase, juce::MathConstants<double>::twoPi);
    }

    for (int ch = 0; ch < numOutputs; ++ch)
    {
        if (outputs[ch] == nullptr)
            continue;

        juce::FloatVectorOperations::copy (outputs[ch], hardwareBuffer.getReadPointer (ch), numSamples);

        if (ch < maxChannels)
        {
            const float peak = hardwareBuffer.getMagnitude (ch, 0, numSamples);
            if (peak > outputPeak[(size_t) ch].load())
                outputPeak[(size_t) ch] = peak;
        }
    }

    if (playing)
        position = pos;
}

//==============================================================================
void AudioEngine::renderTracks (juce::AudioBuffer<float>& out, int numSamples, juce::int64 pos,
                                bool realtime, int onlyTrack, const float* const* input, int numInputs,
                                juce::AudioBuffer<float>* hardwareOut)
{
    renderPosition = pos;

    bool anySolo = false;
    for (auto& t : tracks)
        anySolo = anySolo || t->solo.load();

    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        if (onlyTrack >= 0 && i != onlyTrack)
            continue;

        auto& t = *tracks[(size_t) i];

        t.buffer.setSize (t.getRequiredChannels(), numSamples, false, false, true);
        t.buffer.clear();

        if (playing || ! realtime)
            mixClips (t, numSamples, pos);

        // Live input monitoring
        if (realtime && t.monitor && numInputs > 0 && input != nullptr)
        {
            const int a = juce::jlimit (0, numInputs - 1, t.inputIndex);
            const int b = juce::jlimit (0, numInputs - 1, t.inputIndex + 1);

            if (input[a] != nullptr)
                t.buffer.addFrom (0, 0, input[a], numSamples);

            const float* right = t.stereoInput ? input[b] : input[a];
            if (right != nullptr)
                t.buffer.addFrom (1, 0, right, numSamples);
        }

        processPlugins (t, numSamples);

        // Fader + balance pan
        const float g = t.gain.load();
        const float p = t.pan.load();
        t.buffer.applyGain (0, 0, numSamples, g * juce::jmin (1.0f, 1.0f - p));
        t.buffer.applyGain (1, 0, numSamples, g * juce::jmin (1.0f, 1.0f + p));

        if (realtime)
        {
            t.peakL = juce::jmax (t.peakL.load(), t.buffer.getMagnitude (0, 0, numSamples));
            t.peakR = juce::jmax (t.peakR.load(), t.buffer.getMagnitude (1, 0, numSamples));
        }

        const bool audible = onlyTrack >= 0 || (! t.mute && (! anySolo || t.solo));

        if (! audible)
            continue;

        // Tracks sent straight to a hardware output skip the master bus (and the full-mix export);
        // when exporting a single stem they are always included.
        const int pair = t.outputPair.load();
        if (pair >= 0 && onlyTrack < 0)
        {
            if (hardwareOut != nullptr && pair * 2 < hardwareOut->getNumChannels())
            {
                const int r = juce::jmin (pair * 2 + 1, hardwareOut->getNumChannels() - 1);
                hardwareOut->addFrom (pair * 2, 0, t.buffer, 0, 0, numSamples);
                if (r != pair * 2)
                    hardwareOut->addFrom (r, 0, t.buffer, 1, 0, numSamples);
            }
            continue;
        }

        out.addFrom (0, 0, t.buffer, 0, 0, numSamples);
        out.addFrom (1, 0, t.buffer, 1, 0, numSamples);
    }
}

void AudioEngine::renderMaster (juce::AudioBuffer<float>& mix, int numSamples, bool realtime)
{
    auto& m = *master;
    m.buffer.setSize (m.getRequiredChannels(), numSamples, false, false, true);
    m.buffer.clear();
    m.buffer.copyFrom (0, 0, mix, 0, 0, numSamples);
    m.buffer.copyFrom (1, 0, mix, 1, 0, numSamples);

    processPlugins (m, numSamples);

    const float g = m.gain.load();
    mix.copyFrom (0, 0, m.buffer.getReadPointer (0), numSamples, g);
    mix.copyFrom (1, 0, m.buffer.getReadPointer (1), numSamples, g);

    if (realtime)
    {
        m.peakL = juce::jmax (m.peakL.load(), mix.getMagnitude (0, 0, numSamples));
        m.peakR = juce::jmax (m.peakR.load(), mix.getMagnitude (1, 0, numSamples));
    }
}

void AudioEngine::mixClips (Track& t, int numSamples, juce::int64 pos)
{
    const double rate = sampleRate.load();
    const juce::int64 blockEnd = pos + numSamples;

    for (auto& clip : t.clips)
    {
        auto audio = clip->audio;
        if (audio == nullptr || clip->muted)
            continue;

        const auto clipStart  = (juce::int64) std::llround (clip->startSec * rate);
        const auto clipLength = (juce::int64) std::llround (clip->lengthSec * rate);
        const auto clipOffset = (juce::int64) std::llround (clip->offsetSec * rate);

        const auto from = juce::jmax (pos, clipStart);
        const auto to   = juce::jmin (blockEnd, clipStart + clipLength);
        if (to <= from)
            continue;

        const auto srcStart = clipOffset + (from - clipStart);
        if (srcStart < 0 || srcStart >= audio->getNumSamples())
            continue;

        const int num = (int) juce::jmin (to - from, (juce::int64) audio->getNumSamples() - srcStart);
        const int dst = (int) (from - pos);
        const float clipGain = clip->gain;

        const auto fadeIn  = (juce::int64) (clip->fadeInSec * rate);
        const auto fadeOut = (juce::int64) (clip->fadeOutSec * rate);
        const auto rel = from - clipStart;   // position inside the clip
        const bool inFade = (fadeIn > 0 && rel < fadeIn) || (fadeOut > 0 && rel + num > clipLength - fadeOut);

        if (! inFade)
        {
            for (int ch = 0; ch < 2; ++ch)
                t.buffer.addFrom (ch, dst, *audio, juce::jmin (ch, audio->getNumChannels() - 1), (int) srcStart, num, clipGain);
            continue;
        }

        const float* src[2] = { audio->getReadPointer (0), audio->getReadPointer (juce::jmin (1, audio->getNumChannels() - 1)) };
        float* out[2] = { t.buffer.getWritePointer (0), t.buffer.getWritePointer (1) };

        for (int i = 0; i < num; ++i)
        {
            const auto r = rel + i;
            float env = clipGain;
            if (fadeIn > 0 && r < fadeIn)
                env *= fadeCurve ((double) r / (double) fadeIn);
            if (fadeOut > 0 && r > clipLength - fadeOut)
                env *= fadeCurve ((double) (clipLength - r) / (double) fadeOut);

            out[0][dst + i] += src[0][srcStart + i] * env;
            out[1][dst + i] += src[1][srcStart + i] * env;
        }
    }
}

void AudioEngine::processPlugins (Track& t, int numSamples)
{
    for (auto& slot : t.plugins)
    {
        if (slot->bypassed || slot->instance == nullptr)
            continue;

        auto& plugin = *slot->instance;
        const int channels = juce::jmin (t.buffer.getNumChannels(),
                                         juce::jmax (plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels()));
        if (channels <= 0)
            continue;

        juce::AudioBuffer<float> view (t.buffer.getArrayOfWritePointers(), channels, numSamples);
        t.midi.clear();
        plugin.processBlock (view, t.midi);
    }
}

void AudioEngine::addMetronome (juce::AudioBuffer<float>& out, int numSamples, juce::int64 pos)
{
    const double rate = sampleRate.load();
    const double beatLength = rate * 60.0 / juce::jlimit (20.0, 400.0, bpm.load());
    const int clickLength = (int) (0.03 * rate);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto s = pos + i;
        const auto beat = (juce::int64) std::floor ((double) s / beatLength);

        if (s == (juce::int64) std::ceil ((double) beat * beatLength))
        {
            clickSamplesLeft = clickLength;
            clickPhase = 0.0;
            clickFreq = (beat % 4 == 0) ? 1600.0 : 1000.0;
        }

        if (clickSamplesLeft > 0)
        {
            const float env = (float) clickSamplesLeft / (float) clickLength;
            const float v = 0.35f * env * (float) std::sin (clickPhase);
            clickPhase += juce::MathConstants<double>::twoPi * clickFreq / rate;
            --clickSamplesLeft;
            out.addSample (0, i, v);
            out.addSample (1, i, v);
        }
    }
}

//==============================================================================
juce::Optional<juce::AudioPlayHead::PositionInfo> AudioEngine::getPosition() const
{
    PositionInfo pos;
    const auto samples = renderPosition.load();
    const double rate = sampleRate.load();
    const double seconds = (double) samples / rate;
    const double tempo = bpm.load();
    const double ppq = seconds * tempo / 60.0;

    pos.setTimeInSamples (samples);
    pos.setTimeInSeconds (seconds);
    pos.setBpm (tempo);
    pos.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
    pos.setPpqPosition (ppq);
    pos.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
    pos.setIsPlaying (playing.load() || offline.load());
    pos.setIsRecording (recording.load());
    return pos;
}

//==============================================================================
bool AudioEngine::renderPass (const RenderOptions& o, juce::AudioFormatWriter* writer, float gain, float& peak,
                              const std::function<bool (double)>& progress, double progressStart, double progressScale, bool& failed)
{
    // Caller holds the lock with 'offline' set
    const double rate = sampleRate.load();
    const auto start = (juce::int64) std::llround (o.startSec * rate);
    const auto end   = (juce::int64) std::llround (o.endSec * rate);
    const auto total = juce::jmax ((juce::int64) 1, end - start);

    auto forEachPlugin = [this] (auto fn)
    {
        for (auto& t : tracks)
            for (auto& p : t->plugins) fn (*p->instance);
        for (auto& p : master->plugins) fn (*p->instance);
    };

    forEachPlugin ([] (juce::AudioPluginInstance& p) { p.setNonRealtime (true); p.reset(); });

    juce::AudioBuffer<float> block (2, blockSize);
    bool keepGoing = true;
    peak = 0.0f;

    for (auto pos = start; pos < end && keepGoing; pos += blockSize)
    {
        const int n = (int) juce::jmin ((juce::int64) blockSize, end - pos);
        block.clear();

        renderTracks (block, n, pos, false, o.trackIndex, nullptr, 0);

        if (o.trackIndex < 0)
            renderMaster (block, n, false);

        if (! juce::approximatelyEqual (gain, 1.0f))
            block.applyGain (0, n, gain);

        peak = juce::jmax (peak, block.getMagnitude (0, n));

        if (writer != nullptr && ! writer->writeFromAudioSampleBuffer (block, 0, n))
        {
            failed = true;
            keepGoing = false;
        }

        if (progress && ! progress (progressStart + progressScale * (double) (pos - start) / (double) total))
            keepGoing = false;
    }

    forEachPlugin ([] (juce::AudioPluginInstance& p) { p.setNonRealtime (false); p.reset(); });
    return keepGoing && ! failed;
}

bool AudioEngine::renderToFile (const RenderOptions& o, const std::function<bool (double)>& progress, juce::String& error)
{
    std::unique_ptr<juce::AudioFormat> format;
    auto options = juce::AudioFormatWriterOptions{}
                       .withSampleRate (sampleRate.load())
                       .withNumChannels (2)
                       .withBitsPerSample (o.bitDepth);

    // Tags: WAV uses RIFF INFO keys, OGG / MP3 use id3-style keys
    std::unordered_map<juce::String, juce::String> tags;
    auto tag = [&tags] (const char* key, const juce::String& value) { if (value.isNotEmpty()) tags[key] = value; };

    if (o.format == "FLAC")
    {
        format = std::make_unique<juce::FlacAudioFormat>();
        options = options.withBitsPerSample (juce::jmin (24, o.bitDepth));
    }
    else if (o.format == "OGG" || o.format == "MP3")
    {
        if (o.format == "MP3")
        {
           #if JUCE_USE_LAME_AUDIO_FORMAT
            if (! o.lameExecutable.existsAsFile())
            {
                error = "MP3 export needs the free LAME encoder. Download lame.exe and choose it in Settings > Export.";
                return false;
            }
            format = std::make_unique<juce::LAMEEncoderAudioFormat> (o.lameExecutable);
           #else
            error = "MP3 export isn't available in this build.";
            return false;
           #endif
        }
        else
        {
            format = std::make_unique<juce::OggVorbisAudioFormat>();
        }

        const auto qualities = format->getQualityOptions();
        int quality = qualities.size() - 1;
        for (int i = 0; i < qualities.size(); ++i)
            if (qualities[i].contains ("320"))
                quality = i;
        options = options.withBitsPerSample (16).withQualityOptionIndex (juce::jmax (0, quality));

        tag ("id3title", info.title);   tag ("id3artist", info.artist); tag ("id3album", info.album);
        tag ("id3genre", info.genre);   tag ("id3comment", info.comment);
    }
    else
    {
        format = std::make_unique<juce::WavAudioFormat>();
        if (o.floatingPoint)
            options = options.withBitsPerSample (32)
                             .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

        tag (juce::WavAudioFormat::riffInfoTitle, info.title);
        tag (juce::WavAudioFormat::riffInfoArtist, info.artist);
        tag (juce::WavAudioFormat::riffInfoGenre, info.genre);
        tag (juce::WavAudioFormat::riffInfoComment, info.comment);
        tag (juce::WavAudioFormat::riffInfoSoftware, Rakam::appName);
    }

    if (o.writeTags && ! tags.empty())
        options = options.withMetadataValues (tags);

    o.file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (o.file.createOutputStream().release());

    if (stream == nullptr)
    {
        error = "Can't write to " + o.file.getFullPathName();
        return false;
    }

    auto writer = format->createWriterFor (stream, options);

    if (writer == nullptr)
    {
        error = "The " + o.format + " writer doesn't support these settings (sample rate "
                + juce::String (sampleRate.load()) + " Hz).";
        o.file.deleteFile();
        return false;
    }

    bool ok = true, failed = false;
    {
        const juce::ScopedLock sl (lock);
        offline = true;

        float gain = 1.0f, peak = 0.0f;

        if (o.normalize)
        {
            // Pass 1 measures the loudest peak, pass 2 writes with the gain that brings it to the target
            ok = renderPass (o, nullptr, 1.0f, peak, progress, 0.0, 0.5, failed);
            if (peak > 1.0e-6f)
                gain = juce::Decibels::decibelsToGain (o.normalizeDb) / peak;
        }

        if (ok)
            ok = renderPass (o, writer.get(), gain, peak, progress, o.normalize ? 0.5 : 0.0, o.normalize ? 0.5 : 1.0, failed);

        offline = false;
    }

    writer.reset();   // finishes the file (for MP3 this runs the encoder)

    if (! ok)
    {
        o.file.deleteFile();
        error = failed ? "Disk write failed (is the disk full?)." : "Export cancelled.";
        return false;
    }

    if (! o.file.existsAsFile() || o.file.getSize() == 0)
    {
        error = "The encoder didn't produce " + o.file.getFileName() + ".";
        return false;
    }

    return true;
}
