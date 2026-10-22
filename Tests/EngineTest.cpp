// Headless checks for the audio engine: import, mixing, stems, project save/load and packaging.
// Build with -DRAKAM_BUILD_TESTS=ON and run RakamEngineTest.

#include "Engine/AudioEngine.h"
#include "Engine/ProjectIO.h"

namespace
{
    int failures = 0;

    void check (bool condition, const juce::String& what)
    {
        std::cout << (condition ? "  PASS  " : "  FAIL  ") << what << std::endl;
        if (! condition)
            ++failures;
    }

    juce::File writeSine (const juce::File& file, double rate, int channels, double seconds, double freq, float amp)
    {
        juce::AudioBuffer<float> b (channels, (int) (rate * seconds));
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (ch, i, amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / rate));

        juce::WavAudioFormat wav;
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> out (file.createOutputStream().release());
        auto w = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (rate)
                                               .withNumChannels (channels).withBitsPerSample (24));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return file;
    }

    std::unique_ptr<juce::AudioBuffer<float>> readAll (AudioEngine& e, const juce::File& f, double* rate = nullptr)
    {
        std::unique_ptr<juce::AudioFormatReader> r (e.getFormatManager().createReaderFor (f));
        if (r == nullptr)
            return nullptr;
        auto b = std::make_unique<juce::AudioBuffer<float>> ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (b.get(), 0, (int) r->lengthInSamples, 0, true, true);
        if (rate != nullptr)
            *rate = r->sampleRate;
        return b;
    }

    float maxDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        float d = 0.0f;
        const int n = juce::jmin (a.getNumSamples(), b.getNumSamples());
        for (int ch = 0; ch < juce::jmin (a.getNumChannels(), b.getNumChannels()); ++ch)
            for (int i = 0; i < n; ++i)
                d = juce::jmax (d, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
        return d;
    }

    bool render (AudioEngine& e, const juce::File& f, int track, double end)
    {
        AudioEngine::RenderOptions o;
        o.file = f;
        o.trackIndex = track;
        o.endSec = end;
        o.format = "WAV";
        o.bitDepth = 32;
        o.floatingPoint = true;
        juce::String err;
        const bool ok = e.renderToFile (o, [] (double) { return true; }, err);
        if (! ok)
            std::cout << "    render error: " << err << std::endl;
        return ok;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("RakamEngineTest");
    dir.deleteRecursively();
    dir.createDirectory();

    juce::PropertiesFile::Options opts;
    opts.applicationName = "RakamEngineTest";
    juce::PropertiesFile props (dir.getChildFile ("test.settings"), opts);

    auto engine = std::make_unique<AudioEngine> (props);
    auto& e = *engine;
    const double rate = e.getSampleRate();
    std::cout << "Engine sample rate: " << rate << " Hz, block " << e.getBlockSize() << std::endl;

    auto fileA = writeSine (dir.getChildFile ("sineA.wav"), 48000.0, 1, 2.0, 440.0, 0.5f);   // needs resampling
    auto fileB = writeSine (dir.getChildFile ("sineB.wav"), rate, 2, 1.0, 1000.0, 0.25f);

    std::cout << "\n[Import]" << std::endl;
    auto* t1 = e.addTrack ("Vocals");
    auto* t2 = e.addTrack ("Synth");
    juce::String err;
    auto clipA = e.importFile (fileA, *t1, 0.0, err);
    auto clipB = e.importFile (fileB, *t2, 1.0, err);
    check (clipA != nullptr && clipB != nullptr, "two files imported");
    check (std::abs (clipA->lengthSec - 2.0) < 0.001, "48 kHz clip length is 2.0 s");
    check (std::abs (clipA->audio->getNumSamples() - (int) (2.0 * rate)) <= 2, "48 kHz clip resampled to engine rate");
    check (std::abs (e.getProjectLengthSec() - 2.0) < 0.001, "project length is 2.0 s");

    std::cout << "\n[Mix and stems]" << std::endl;
    t1->gain = 0.5f;
    t2->pan = -1.0f; // hard left

    auto mixFile = dir.getChildFile ("mix.wav");
    auto stem1 = dir.getChildFile ("stem1.wav");
    auto stem2 = dir.getChildFile ("stem2.wav");
    check (render (e, mixFile, -1, 2.5) && render (e, stem1, 0, 2.5) && render (e, stem2, 1, 2.5), "mix + 2 stems rendered");

    auto mix = readAll (e, mixFile);
    auto s1 = readAll (e, stem1);
    auto s2 = readAll (e, stem2);
    check (mix && s1 && s2, "rendered files are readable");
    check (mix->getNumSamples() == s1->getNumSamples() && s1->getNumSamples() == s2->getNumSamples()
             && std::abs (mix->getNumSamples() - (int) (2.5 * rate)) <= 1, "all stems have the same length as the mix");

    juce::AudioBuffer<float> sum (2, s1->getNumSamples());
    sum.clear();
    for (int ch = 0; ch < 2; ++ch)
    {
        sum.addFrom (ch, 0, *s1, ch, 0, s1->getNumSamples());
        sum.addFrom (ch, 0, *s2, ch, 0, s2->getNumSamples());
    }
    check (maxDiff (sum, *mix) < 1.0e-5f, "stem 1 + stem 2 == full mix (stems null against the mix)");
    check (std::abs (s1->getMagnitude (0, 0, (int) (0.5 * rate)) - 0.25f) < 0.01f, "track gain 0.5 applied (peak 0.25)");
    check (s2->getMagnitude (1, 0, s2->getNumSamples()) < 1.0e-6f, "hard-left pan silences right channel");
    check (s2->getMagnitude (0, 0, (int) (0.99 * rate)) < 1.0e-6f, "clip placed at 1.0 s is silent before 1.0 s");
    check (s2->getMagnitude (0, (int) (1.01 * rate), (int) (0.5 * rate)) > 0.2f, "clip placed at 1.0 s plays after 1.0 s");

    std::cout << "\n[Mute / solo / master]" << std::endl;
    t1->mute = true;
    auto mutedFile = dir.getChildFile ("muted.wav");
    render (e, mutedFile, -1, 2.5);
    check (maxDiff (*readAll (e, mutedFile), *s2) < 1.0e-5f, "muting track 1 leaves only track 2 in the mix");
    t1->mute = false;
    t1->solo = true;
    auto soloFile = dir.getChildFile ("solo.wav");
    render (e, soloFile, -1, 2.5);
    check (maxDiff (*readAll (e, soloFile), *s1) < 1.0e-5f, "solo track 1 leaves only track 1 in the mix");
    t1->solo = false;
    e.getMaster().gain = 0.5f;
    auto halfFile = dir.getChildFile ("half.wav");
    render (e, halfFile, -1, 2.5);
    auto half = readAll (e, halfFile);
    half->applyGain (2.0f);
    check (maxDiff (*half, *mix) < 1.0e-5f, "master fader scales the mix");
    e.getMaster().gain = 1.0f;

    std::cout << "\n[Split clip]" << std::endl;
    e.splitClip (*t1, clipA.get(), 0.73);
    check (t1->clips.size() == 2, "split produced two clips");
    auto splitFile = dir.getChildFile ("split.wav");
    render (e, splitFile, -1, 2.5);
    check (maxDiff (*readAll (e, splitFile), *mix) < 1.0e-5f, "split clips render identically");

    std::cout << "\n[Save / load]" << std::endl;
    auto projDir = dir.getChildFile ("Project");
    projDir.createDirectory();
    projDir.getChildFile ("Audio").createDirectory();
    fileB.copyFileTo (projDir.getChildFile ("Audio").getChildFile ("sineB.wav"));
    {
        const juce::ScopedLock sl (e.getLock());
        t2->clips[0]->file = projDir.getChildFile ("Audio").getChildFile ("sineB.wav");
    }
    auto projFile = projDir.getChildFile ("song.rakam");
    check (ProjectIO::save (e, projFile, err), "project saved");
    check (projFile.loadFileAsString().contains ("file=\"Audio/sineB.wav\""), "audio inside the project folder is stored as a relative path");

    juce::StringArray warnings;
    check (ProjectIO::load (e, projFile, err, warnings) && warnings.isEmpty(), "project loaded without warnings");
    check (e.getNumTracks() == 2 && e.getTrack (0)->clips.size() == 2, "tracks and clips restored");
    auto reloadedFile = dir.getChildFile ("reloaded.wav");
    render (e, reloadedFile, -1, 2.5);
    check (maxDiff (*readAll (e, reloadedFile), *mix) < 1.0e-5f, "reloaded project renders identically");

    std::cout << "\n[Collect & Zip]" << std::endl;
    auto zip = dir.getChildFile ("package.zip");
    check (ProjectIO::collectAndZip (e, zip, err), "package written");
    juce::ZipFile zf (zip);
    check (zf.getIndexOfFileName ("project.rakam") >= 0 && zf.getIndexOfFileName ("Audio/sineA.wav") >= 0
             && zf.getIndexOfFileName ("Audio/sineB.wav") >= 0 && zf.getIndexOfFileName ("PLUGINS_NEEDED.txt") >= 0,
           "package contains project, both audio files and plugin list");

    // Move the originals away so the package must be self-contained
    fileA.moveFileTo (dir.getChildFile ("moved_sineA.wav"));
    auto extracted = ProjectIO::extractPackage (zip, err);
    check (extracted.existsAsFile(), "package extracted");
    warnings.clear();
    check (ProjectIO::load (e, extracted, err, warnings) && warnings.isEmpty(), "package opened without missing files");
    auto pkgFile = dir.getChildFile ("package_render.wav");
    render (e, pkgFile, -1, 2.5);
    check (maxDiff (*readAll (e, pkgFile), *mix) < 1.0e-5f, "packaged project renders identically");
    extracted.getParentDirectory().deleteRecursively();

    std::cout << "\n[Formats]" << std::endl;
    for (auto fmt : { juce::String ("WAV"), juce::String ("FLAC"), juce::String ("OGG") })
    {
        AudioEngine::RenderOptions o;
        o.file = dir.getChildFile ("fmt." + fmt.toLowerCase());
        o.format = fmt;
        o.endSec = 2.5;
        o.bitDepth = fmt == "WAV" ? 16 : 24;
        const bool ok = e.renderToFile (o, [] (double) { return true; }, err);
        double r = 0.0;
        auto b = ok ? readAll (e, o.file, &r) : nullptr;
        check (b != nullptr && std::abs (b->getNumSamples() - (int) (2.5 * rate)) < 2048, fmt + " export readable");
    }

    std::cout << "\n[Cancel]" << std::endl;
    {
        AudioEngine::RenderOptions o;
        o.file = dir.getChildFile ("cancel.wav");
        o.endSec = 2.5;
        const bool ok = e.renderToFile (o, [] (double p) { return p < 0.3; }, err);
        check (! ok && ! o.file.existsAsFile(), "cancelled export removes the partial file");
    }

    //==============================================================================
    std::cout << "\n[v0.2: fades, clip gain, undo]" << std::endl;
    e.clearProject();
    auto fileC = writeSine (dir.getChildFile ("sineC.wav"), rate, 2, 2.0, 220.0, 0.5f);
    auto* tc = e.addTrack ("Fades");
    auto clipC = e.importFile (fileC, *tc, 0.0, err);
    check (clipC != nullptr, "clip imported into a fresh project");
    {
        e.beginClipEdit();
        const juce::ScopedLock sl (e.getLock());
        clipC->fadeInSec = 0.5;
        clipC->fadeOutSec = 0.5;
    }
    e.endClipEdit ("Fades");

    auto fadeFile = dir.getChildFile ("fades.wav");
    render (e, fadeFile, 0, 2.0);
    auto fades = readAll (e, fadeFile);
    check (fades->getMagnitude (0, 0, (int) (0.02 * rate)) < 0.05f, "fade-in starts near silence");
    check (std::abs (fades->getMagnitude (0, (int) (0.8 * rate), (int) (0.4 * rate)) - 0.5f) < 0.01f, "full level between the fades");
    check (fades->getMagnitude (0, (int) (1.98 * rate), (int) (0.02 * rate)) < 0.05f, "fade-out ends near silence");

    e.normalizeClip (*clipC);
    check (std::abs (clipC->gain * 0.5f - juce::Decibels::decibelsToGain (-0.3f)) < 0.01f, "normalize sets clip peak to -0.3 dB");

    e.beginClipEdit();
    clipC->startSec = 3.0;
    e.endClipEdit ("Move clip");
    check (e.undo() && std::abs (clipC->startSec) < 1.0e-9, "undo restores the clip position");
    check (e.undo() && std::abs (clipC->gain - 1.0f) < 1.0e-6f, "undo reverts normalize");
    check (e.redo() && e.redo() && std::abs (clipC->startSec - 3.0) < 1.0e-9, "redo re-applies both edits");
    e.splitClip (*tc, clipC.get(), 3.5);
    check (tc->clips.size() == 2, "split adds a clip");
    check (e.undo() && tc->clips.size() == 1 && std::abs (clipC->lengthSec - 2.0) < 1.0e-9, "undo split merges back");
    auto dup = e.duplicateClip (*tc, clipC.get());
    check (dup != nullptr && std::abs (dup->startSec - clipC->endSec()) < 1.0e-9 && tc->clips.size() == 2, "duplicate clip placed right after");
    auto* tcopy = e.duplicateTrack (0);
    check (tcopy != nullptr && e.getNumTracks() == 2 && e.getTrack (1) == tcopy && tcopy->clips.size() == 2, "duplicate track copies clips");
    e.moveTrack (1, 0);
    check (e.getTrack (0) == tcopy, "move track reorders");

    std::cout << "\n[v0.2: loop playback]" << std::endl;
    {
        e.loopStartSec = 0.5;
        e.loopEndSec = 1.0;
        e.loopEnabled = true;
        e.setPositionSec (0.9);
        e.play();
        const int n = (int) (0.2 * rate);
        juce::AudioBuffer<float> outBuf (2, n);
        juce::AudioIODeviceCallbackContext ctx {};
        e.audioDeviceIOCallbackWithContext (nullptr, 0, outBuf.getArrayOfWritePointers(), 2, n, ctx);
        check (std::abs (e.getPositionSec() - 0.6) < 0.001, "playhead wraps from loop end back to loop start");
        e.stop();
        e.loopEnabled = false;
    }

    std::cout << "\n[v0.2: built-in effects]" << std::endl;
    {
        auto descs = BuiltInPluginFormat::getAllDescriptions();
        check (descs.size() == 11, "11 built-in effects available");

        e.clearProject();
        auto* t = e.addTrack ("FX");
        e.importFile (fileC, *t, 0.0, err);
        for (auto& d : descs)
        {
            juce::String perr;
            const bool ok = e.addPlugin (*t, d, perr);
            if (! ok) std::cout << "    " << perr << std::endl;
            check (ok, "loads " + d.name);
        }

        auto fxFile = dir.getChildFile ("allfx.wav");
        render (e, fxFile, 0, 3.0);
        auto fx = readAll (e, fxFile);
        bool finite = true;
        for (int ch = 0; ch < fx->getNumChannels(); ++ch)
            for (int i = 0; i < fx->getNumSamples(); ++i)
                finite = finite && std::isfinite (fx->getSample (ch, i));
        check (finite && fx->getMagnitude (0, fx->getNumSamples()) > 0.01f && fx->getMagnitude (0, fx->getNumSamples()) <= 1.0f,
               "all 11 effects in series produce clean, limited audio");

        // Utility at -6 dB halves the signal
        e.clearProject();
        auto* u = e.addTrack ("Utility");
        e.importFile (fileC, *u, 0.0, err);
        e.addPlugin (*u, BuiltInPluginFormat::getAllDescriptions()[10], err);
        auto* utility = dynamic_cast<BuiltInPlugin*> (u->plugins[0]->instance.get());
        auto* gainParam = utility->state.getParameter ("gain");
        gainParam->setValueNotifyingHost (gainParam->convertTo0to1 (-6.0206f));
        auto utilFile = dir.getChildFile ("utility.wav");
        render (e, utilFile, 0, 2.0);
        check (std::abs (readAll (e, utilFile)->getMagnitude (0, (int) (0.5 * rate), (int) (0.5 * rate)) - 0.25f) < 0.005f,
               "Utility -6 dB halves the level");

        // Effect settings survive save / load
        utility->setCurrentProgram (1); // Mono check
        auto pf = dir.getChildFile ("Proj2").getChildFile ("fx.rakam");
        pf.getParentDirectory().createDirectory();
        ProjectIO::save (e, pf, err);
        juce::StringArray w2;
        ProjectIO::load (e, pf, err, w2);
        auto* reloaded = dynamic_cast<BuiltInPlugin*> (e.getTrack (0)->plugins[0]->instance.get());
        check (reloaded != nullptr && reloaded->state.getRawParameterValue ("mono")->load() > 0.5f
                 && reloaded->getCurrentProgram() == 1, "built-in effect preset and settings restored after reload");
    }

    //==============================================================================
    std::cout << "\n[v0.3: export options]" << std::endl;
    {
        e.clearProject();
        auto* t = e.addTrack ("Sine");
        e.importFile (fileC, *t, 0.0, err);   // 0.5 peak, 2 s
        e.info.title = "Test Song";
        e.info.artist = "Rakam";

        AudioEngine::RenderOptions o;
        o.file = dir.getChildFile ("norm.wav");
        o.endSec = 2.0;
        o.bitDepth = 32;
        o.floatingPoint = true;
        o.normalize = true;
        o.normalizeDb = -1.0f;
        check (e.renderToFile (o, [] (double) { return true; }, err), "normalized export rendered");
        auto norm = readAll (e, o.file);
        check (std::abs (juce::Decibels::gainToDecibels (norm->getMagnitude (0, norm->getNumSamples())) + 1.0f) < 0.05f,
               "normalize brings the peak to -1 dB");

        std::unique_ptr<juce::AudioFormatReader> reader (e.getFormatManager().createReaderFor (o.file));
        check (reader != nullptr && reader->metadataValues[juce::WavAudioFormat::riffInfoTitle] == "Test Song"
                 && reader->metadataValues[juce::WavAudioFormat::riffInfoArtist] == "Rakam", "WAV export carries title / artist tags");

        AudioEngine::RenderOptions r;
        r.file = dir.getChildFile ("range.wav");
        r.startSec = 0.5;
        r.endSec = 1.25;
        e.renderToFile (r, [] (double) { return true; }, err);
        check (std::abs (readAll (e, r.file)->getNumSamples() - (int) (0.75 * rate)) <= 1, "loop-range export has the range's length");

        AudioEngine::RenderOptions mp3;
        mp3.file = dir.getChildFile ("song.mp3");
        mp3.endSec = 1.0;
        mp3.format = "MP3";
        juce::String mp3Error;
        check (! e.renderToFile (mp3, [] (double) { return true; }, mp3Error) && mp3Error.contains ("LAME"),
               "MP3 without lame.exe gives a clear message");
    }

    std::cout << "\n[v0.3: file handling]" << std::endl;
    {
        // Copy external audio into the project + backups
        auto proj = dir.getChildFile ("P3").getChildFile ("song.rakam");
        proj.getParentDirectory().createDirectory();
        ProjectIO::SaveOptions so;
        so.copyExternalAudio = true;
        so.backupsToKeep = 2;
        for (int i = 0; i < 4; ++i)
        {
            check (ProjectIO::save (e, proj, err, so), "save #" + juce::String (i + 1));
            juce::Thread::sleep (1100);   // backups are timestamped to the second
        }
        check (e.getTrack (0)->clips[0]->file.isAChildOf (proj.getParentDirectory().getChildFile ("Audio")),
               "external audio copied into the project's Audio folder");
        check (proj.loadFileAsString().contains ("file=\"Audio/sineC.wav\""), "project points at the copied file");
        check (proj.getParentDirectory().getChildFile ("Backups").getNumberOfChildFiles (juce::File::findFiles) == 2,
               "backups capped at the chosen number");
        check (proj.loadFileAsString().contains ("title=\"Test Song\""), "project info saved");

        // Missing files and relink
        auto audioFolder = proj.getParentDirectory().getChildFile ("Audio");
        auto moved = dir.getChildFile ("MovedAudio");
        audioFolder.moveFileTo (moved);
        juce::StringArray w;
        ProjectIO::MissingList missing;
        ProjectIO::load (e, proj, err, w, &missing);
        check (missing.size() == 1 && e.getTrack (0)->clips.empty(), "missing audio is detected on open");
        check (e.info.title == "Test Song", "project info loaded");
        check (ProjectIO::relinkMissing (e, missing, dir) == 1 && missing.empty() && e.getTrack (0)->clips.size() == 1,
               "missing audio relinked by searching a folder");

        // Templates
        juce::String terr;
        check (ProjectIO::saveAsTemplate (e, "Unit Test Template", terr), "user template saved");
        auto tpl = Settings::getTemplatesFolder().getChildFile ("Unit Test Template.rakam");
        check (tpl.existsAsFile() && ! tpl.loadFileAsString().contains ("<CLIP"), "templates keep tracks but no audio");
        tpl.deleteFile();

        int expected[] = { 3, 3, 5, 8, 4 };
        int idx = 0;
        for (auto& name : ProjectIO::getStarterTemplateNames())
        {
            e.clearProject();
            ProjectIO::buildStarterTemplate (e, name);
            check (e.getNumTracks() == expected[idx++] && ! e.getMaster().plugins.empty(), "starter template: " + name);
        }
        e.clearProject();
        ProjectIO::buildStarterTemplate (e, "Vocal Recording");
        auto* lead = dynamic_cast<BuiltInPlugin*> (e.getTrack (1)->plugins[0]->instance.get());
        check (lead != nullptr && lead->getProgramName (lead->getCurrentProgram()) == "Vocal presence", "template effects use their presets");
    }

    //==============================================================================
    std::cout << "\n[v0.4: song structure]" << std::endl;
    {
        e.clearProject();
        auto* t = e.addTrack ("Song");
        auto song = writeSine (dir.getChildFile ("song8.wav"), rate, 2, 8.0, 220.0, 0.5f);
        e.importFile (song, *t, 0.0, err);
        e.clearUndoHistory();

        const int intro = e.addSection ("Intro", 0.0, 2.0);
        const int verse = e.addSection ("Verse", 2.0, 5.0);
        e.addSection ("Chorus", 5.0, 8.0);
        check (intro == 0 && verse == 1 && e.sections.size() == 3, "sections added in time order");
        check (e.sectionAt (3.0) == 1 && e.sectionAt (9.0) < 0, "section lookup by time");
        check (AudioEngine::sectionColour ("Chorus") != AudioEngine::sectionColour ("Verse 2"), "sections get colours by name");

        e.duplicateSection (1);   // Verse (3 s) copied after itself
        check (e.sections.size() == 4 && std::abs (e.sections[2].startSec - 5.0) < 1e-9 && std::abs (e.sections[2].endSec - 8.0) < 1e-9
                 && std::abs (e.sections[3].startSec - 8.0) < 1e-9 && e.sections[3].name == "Chorus",
               "duplicate section inserts a copy and pushes later sections");
        check (std::abs (e.getProjectLengthSec() - 11.0) < 1e-6, "song is 3 s longer after duplicating the verse");

        auto dupFile = dir.getChildFile ("dup.wav");
        render (e, dupFile, 0, 11.0);
        auto dupAudio = readAll (e, dupFile);
        auto orig = readAll (e, song);
        // The copy (5..8 s) must equal the original verse (2..5 s), and the chorus must now start at 8 s
        juce::AudioBuffer<float> a1 (2, (int) (3.0 * rate)), b1 (2, (int) (3.0 * rate)), c1 (2, (int) (3.0 * rate)), d1 (2, (int) (3.0 * rate));
        for (int ch = 0; ch < 2; ++ch)
        {
            a1.copyFrom (ch, 0, *dupAudio, ch, (int) (5.0 * rate), a1.getNumSamples());
            b1.copyFrom (ch, 0, *orig, ch, (int) (2.0 * rate), b1.getNumSamples());
            c1.copyFrom (ch, 0, *dupAudio, ch, (int) (8.0 * rate), c1.getNumSamples());
            d1.copyFrom (ch, 0, *orig, ch, (int) (5.0 * rate), d1.getNumSamples());
        }
        check (maxDiff (a1, b1) < 1.0e-4f, "duplicated audio matches the original section");
        check (maxDiff (c1, d1) < 1.0e-4f, "audio after the section moved later intact");

        check (e.undo() && e.sections.size() == 3 && std::abs (e.getProjectLengthSec() - 8.0) < 1e-6, "undo duplicate section");
        check (e.redo() && e.sections.size() == 4, "redo duplicate section");

        e.deleteSectionWithAudio (0);   // remove the 2 s intro
        check (e.sections.size() == 3 && std::abs (e.sections[0].startSec) < 1e-9 && std::abs (e.getProjectLengthSec() - 9.0) < 1e-6,
               "delete section removes its audio and closes the gap");

        auto proj = dir.getChildFile ("P4").getChildFile ("structure.rakam");
        proj.getParentDirectory().createDirectory();
        ProjectIO::save (e, proj, err);
        juce::StringArray w;
        ProjectIO::load (e, proj, err, w);
        check (e.sections.size() == 3 && e.sections[0].name == "Verse", "sections saved and loaded");
    }

    std::cout << "\n[v0.4: record modes and takes]" << std::endl;
    {
        e.clearProject();
        auto* t = e.addTrack ("Vox");
        auto base = writeSine (dir.getChildFile ("base.wav"), rate, 2, 6.0, 220.0, 0.5f);
        e.importFile (base, *t, 0.0, err);

        // Replace: a 2 s recording at 1 s cuts the old audio out from 1..3 s
        auto take = writeSine (dir.getChildFile ("take.wav"), rate, 2, 2.0, 880.0, 0.25f);
        e.placeRecording (*t, take, AudioEngine::RecordMode::replace, 1.0, 0.0, 0.0, 0.0);
        auto replaceFile = dir.getChildFile ("replace.wav");
        render (e, replaceFile, 0, 6.0);
        auto rep = readAll (e, replaceFile);
        check (std::abs (rep->getMagnitude (0, (int) (1.2 * rate), (int) (1.5 * rate)) - 0.25f) < 0.01f, "replace: only the new take plays inside the range");
        check (std::abs (rep->getMagnitude (0, (int) (3.2 * rate), (int) (1.5 * rate)) - 0.5f) < 0.01f, "replace: old audio continues after it");
        check (t->clips.size() == 3, "replace: old clip split around the new one");

        // Punch: 4 s file but punch range is 1.5 s, starting at 4 s
        auto punch = writeSine (dir.getChildFile ("punch.wav"), rate, 2, 4.0, 660.0, 0.25f);
        e.placeRecording (*t, punch, AudioEngine::RecordMode::punch, 4.0, 0.0, 0.0, 1.5);
        Track* tt = t;
        bool found = false;
        for (auto& c : tt->clips)
            if (c->file == punch)
                found = std::abs (c->lengthSec - 1.5) < 1e-6 && c->fadeInSec > 0.0;
        check (found, "punch: recording trimmed to the punch range with short fades");

        // Loop takes: a 2 s loop recorded 3.4 times with 10 ms latency -> 3 full takes + 1 partial (0.4 s < 1 s: dropped)
        e.clearProject();
        auto* lt = e.addTrack ("Loop");
        auto loopRec = writeSine (dir.getChildFile ("loop.wav"), rate, 1, 6.81, 330.0, 0.3f);
        e.placeRecording (*lt, loopRec, AudioEngine::RecordMode::loopTakes, 4.0, 0.01, 2.0, 0.0);
        int active = 0, muted = 0;
        for (auto& c : lt->clips)
            (c->muted ? muted : active)++;
        check (lt->clips.size() == 3 && active == 1 && muted == 2, "loop takes: 3 takes, only one active");
        check (! lt->clips.back()->muted && lt->clips.back()->takeNumber == 3, "loop takes: the last take is active");
        check (std::abs (lt->clips[1]->offsetSec - 2.01) < 1e-6 && std::abs (lt->clips[1]->startSec - 4.0) < 1e-6,
               "loop takes: each take reads its own pass, placed at the loop start");

        e.chooseTake (*lt, lt->clips[0]->takeGroup, 1);
        check (! lt->clips[0]->muted && lt->clips[2]->muted, "choose take switches the active take");
        check (e.undo() && lt->clips[0]->muted && ! lt->clips[2]->muted, "undo take choice");
        e.redo();

        auto takeFile = dir.getChildFile ("takes.wav");
        render (e, takeFile, 0, 6.0);
        check (std::abs (readAll (e, takeFile)->getMagnitude (0, (int) (4.2 * rate), (int) (1.5 * rate)) - 0.3f) < 0.01f,
               "only the chosen take is heard (no stacking)");

        auto proj = dir.getChildFile ("P5").getChildFile ("takes.rakam");
        proj.getParentDirectory().createDirectory();
        ProjectIO::save (e, proj, err);
        juce::StringArray w;
        ProjectIO::load (e, proj, err, w);
        auto* lt2 = e.getTrack (0);
        check (lt2->clips.size() == 3 && ! lt2->clips[0]->muted && lt2->clips[1]->muted && lt2->clips[0]->takeGroup > 0,
               "takes survive save / load");

        e.flattenTakes (*lt2, lt2->clips[0]->takeGroup);
        check (lt2->clips.size() == 1 && lt2->clips[0]->takeGroup == 0, "keep active take deletes the others");

        // A recording stopped during the count-in leaves nothing behind
        auto empty = writeSine (dir.getChildFile ("empty.wav"), rate, 1, 0.005, 100.0, 0.1f);
        const auto before = lt2->clips.size();
        e.placeRecording (*lt2, empty, AudioEngine::RecordMode::normal, 0.0, 0.01, 0.0, 0.0);
        check (lt2->clips.size() == before && ! empty.existsAsFile(), "empty recordings are discarded");
    }

    std::cout << "\n[v0.4: I/O routing]" << std::endl;
    {
        e.clearProject();
        auto* a = e.addTrack ("To master");
        auto* b = e.addTrack ("Headphones");
        e.importFile (fileC, *a, 0.0, err);
        e.importFile (fileC, *b, 0.0, err);
        b->outputPair = 1;   // Out 3+4

        auto mixFile2 = dir.getChildFile ("routing.wav");
        render (e, mixFile2, -1, 2.0);
        check (std::abs (readAll (e, mixFile2)->getMagnitude (0, (int) (0.5 * rate), (int) (0.5 * rate)) - 0.5f) < 0.01f,
               "full-mix export only contains tracks routed to the master");
        auto stemB = dir.getChildFile ("stemB.wav");
        render (e, stemB, 1, 2.0);
        check (readAll (e, stemB)->getMagnitude (0, (int) (0.5 * rate), (int) (0.5 * rate)) > 0.4f, "stem export of a direct-out track still works");

        const int n = 1024;
        juce::AudioBuffer<float> outs (4, n), ins (2, n);
        ins.clear();
        for (int i = 0; i < n; ++i)
            ins.setSample (0, i, 0.6f * (float) std::sin (i * 0.05));
        e.setPositionSec (0.5);
        e.play();
        juce::AudioIODeviceCallbackContext ctx {};
        e.audioDeviceIOCallbackWithContext (ins.getArrayOfReadPointers(), 2, outs.getArrayOfWritePointers(), 4, n, ctx);
        check (outs.getMagnitude (0, 0, n) > 0.4f && outs.getMagnitude (0, 0, n) < 0.6f, "master plays on Out 1+2");
        check (outs.getMagnitude (2, 0, n) > 0.4f && outs.getMagnitude (3, 0, n) > 0.4f, "direct-out track plays on Out 3+4");

        e.getMaster().outputPair = 1;
        e.audioDeviceIOCallbackWithContext (ins.getArrayOfReadPointers(), 2, outs.getArrayOfWritePointers(), 4, n, ctx);
        check (outs.getMagnitude (0, 0, n) < 1.0e-6f && outs.getMagnitude (2, 0, n) > 0.8f, "master output can move to Out 3+4");
        e.getMaster().outputPair = 0;
        e.stop();

        e.testTonePair = 0;
        e.audioDeviceIOCallbackWithContext (ins.getArrayOfReadPointers(), 2, outs.getArrayOfWritePointers(), 4, n, ctx);
        e.testTonePair = -1;
        check (std::abs (outs.getMagnitude (0, 0, n) - 0.125f) < 0.01f && outs.getMagnitude (2, 0, n) < 1.0e-6f, "test tone on the chosen output");

        e.updateIOMeters();
        check (std::abs (e.getInputLevel (0) - 0.6f) < 0.01f && e.getInputLevel (1) < 1.0e-6f, "input meters read each input channel");
        check (e.getOutputLevel (0) > 0.1f, "output meters read each output channel");

        auto proj = dir.getChildFile ("P6").getChildFile ("io.rakam");
        proj.getParentDirectory().createDirectory();
        ProjectIO::save (e, proj, err);
        juce::StringArray w;
        ProjectIO::load (e, proj, err, w);
        check (e.getTrack (1)->outputPair.load() == 1 && e.getTrack (0)->outputPair.load() == -1 && e.getMaster().outputPair.load() == 0,
               "output routing saved and loaded");
    }

    engine.reset();
    dir.deleteRecursively();

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " TEST(S) FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
