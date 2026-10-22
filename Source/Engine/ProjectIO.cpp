#include "Engine/ProjectIO.h"

namespace
{
    using PathMapper = std::function<juce::String (const juce::File&)>;

    void writeTrack (juce::XmlElement& x, Track& t, const PathMapper& pathFor, bool includeClips)
    {
        x.setAttribute ("name",   t.name);
        x.setAttribute ("colour", t.colour.toString());
        x.setAttribute ("gain",   (double) t.gain.load());
        x.setAttribute ("pan",    (double) t.pan.load());
        x.setAttribute ("mute",   t.mute.load());
        x.setAttribute ("solo",   t.solo.load());
        x.setAttribute ("armed",  t.armed.load());
        x.setAttribute ("input",  t.inputIndex);
        x.setAttribute ("stereoInput", t.stereoInput);
        x.setAttribute ("output", t.outputPair.load());

        if (includeClips)
        {
            for (auto& clip : t.clips)
            {
                auto* c = x.createNewChildElement ("CLIP");
                c->setAttribute ("name",    clip->name);
                c->setAttribute ("file",    pathFor (clip->file));
                c->setAttribute ("start",   clip->startSec);
                c->setAttribute ("offset",  clip->offsetSec);
                c->setAttribute ("length",  clip->lengthSec);
                c->setAttribute ("fadeIn",  clip->fadeInSec);
                c->setAttribute ("fadeOut", clip->fadeOutSec);
                c->setAttribute ("gain",    (double) clip->gain);
                if (clip->muted)          c->setAttribute ("muted", true);
                if (clip->takeGroup > 0)  { c->setAttribute ("takeGroup", clip->takeGroup); c->setAttribute ("take", clip->takeNumber); }
            }
        }

        for (auto& slot : t.plugins)
        {
            auto* p = x.createNewChildElement ("INSERT");
            p->setAttribute ("bypassed", slot->bypassed.load());

            if (auto desc = slot->instance->getPluginDescription().createXml())
                p->addChildElement (desc.release());

            juce::MemoryBlock state;
            slot->instance->getStateInformation (state);
            p->createNewChildElement ("STATE")->addTextElement (state.toBase64Encoding());
        }
    }

    std::unique_ptr<juce::XmlElement> buildXml (AudioEngine& engine, const PathMapper& pathFor, bool includeClips = true)
    {
        auto root = std::make_unique<juce::XmlElement> ("RAKAM_PROJECT");
        root->setAttribute ("version", 2);
        root->setAttribute ("bpm", engine.bpm.load());
        root->setAttribute ("sampleRate", engine.getSampleRate());
        root->setAttribute ("loop", engine.loopEnabled.load());
        root->setAttribute ("loopStart", engine.loopStartSec.load());
        root->setAttribute ("loopEnd", engine.loopEndSec.load());
        root->setAttribute ("savedWith", Rakam::appName + " " + JUCE_APPLICATION_VERSION_STRING);

        auto* info = root->createNewChildElement ("INFO");
        info->setAttribute ("title",   engine.info.title);
        info->setAttribute ("artist",  engine.info.artist);
        info->setAttribute ("album",   engine.info.album);
        info->setAttribute ("genre",   engine.info.genre);
        info->setAttribute ("comment", engine.info.comment);

        auto* structure = root->createNewChildElement ("STRUCTURE");
        for (auto& sec : engine.sections)
        {
            auto* x = structure->createNewChildElement ("SECTION");
            x->setAttribute ("name", sec.name);
            x->setAttribute ("start", sec.startSec);
            x->setAttribute ("end", sec.endSec);
            x->setAttribute ("colour", sec.colour.toString());
        }

        for (int i = 0; i < engine.getNumTracks(); ++i)
            writeTrack (*root->createNewChildElement ("TRACK"), *engine.getTrack (i), pathFor, includeClips);

        writeTrack (*root->createNewChildElement ("MASTER"), engine.getMaster(), pathFor, includeClips);
        return root;
    }

    juce::File resolvePath (const juce::File& projectDir, juce::String path)
    {
        path = path.replaceCharacter ('\\', '/');

        if (juce::File::isAbsolutePath (path))
            return juce::File (path);

        auto f = projectDir;
        for (auto& part : juce::StringArray::fromTokens (path, "/", ""))
            if (part.isNotEmpty())
                f = f.getChildFile (part);
        return f;
    }

    ProjectIO::MissingClip readClipInfo (const juce::XmlElement& c)
    {
        ProjectIO::MissingClip m;
        m.name    = c.getStringAttribute ("name");
        m.start   = c.getDoubleAttribute ("start");
        m.offset  = c.getDoubleAttribute ("offset");
        m.length  = c.getDoubleAttribute ("length", -1.0);
        m.fadeIn  = c.getDoubleAttribute ("fadeIn");
        m.fadeOut = c.getDoubleAttribute ("fadeOut");
        m.gain    = (float) c.getDoubleAttribute ("gain", 1.0);
        m.muted     = c.getBoolAttribute ("muted");
        m.takeGroup = c.getIntAttribute ("takeGroup");
        m.take      = c.getIntAttribute ("take");
        return m;
    }

    bool placeClip (AudioEngine& engine, Track& t, const juce::File& file, const ProjectIO::MissingClip& m, juce::String& error)
    {
        auto clip = engine.loadClip (file, error);
        if (clip == nullptr)
            return false;

        if (m.name.isNotEmpty())
            clip->name = m.name;
        clip->startSec   = juce::jmax (0.0, m.start);
        clip->offsetSec  = juce::jlimit (0.0, clip->sourceLengthSec, m.offset);
        clip->lengthSec  = juce::jlimit (0.0, clip->sourceLengthSec - clip->offsetSec, m.length < 0.0 ? clip->sourceLengthSec : m.length);
        clip->fadeInSec  = juce::jlimit (0.0, clip->lengthSec, m.fadeIn);
        clip->fadeOutSec = juce::jlimit (0.0, clip->lengthSec, m.fadeOut);
        clip->gain       = m.gain;
        clip->muted      = m.muted;
        clip->takeGroup  = m.takeGroup;
        clip->takeNumber = m.take;
        engine.addClip (t, clip);
        return true;
    }

    void readTrack (AudioEngine& engine, Track& t, const juce::XmlElement& x, const juce::File& projectDir,
                    juce::StringArray& warnings, ProjectIO::MissingList* missing)
    {
        if (! t.isMaster)
        {
            t.name        = x.getStringAttribute ("name", t.name);
            t.colour      = juce::Colour::fromString (x.getStringAttribute ("colour", t.colour.toString()));
            t.pan         = (float) x.getDoubleAttribute ("pan", 0.0);
            t.mute        = x.getBoolAttribute ("mute");
            t.solo        = x.getBoolAttribute ("solo");
            t.armed       = x.getBoolAttribute ("armed");
            t.inputIndex  = x.getIntAttribute ("input", 0);
            t.stereoInput = x.getBoolAttribute ("stereoInput");
        }

        t.outputPair = x.getIntAttribute ("output", t.isMaster ? 0 : -1);

        t.gain = (float) x.getDoubleAttribute ("gain", 1.0);

        for (auto* c : x.getChildWithTagNameIterator ("CLIP"))
        {
            const auto path = c->getStringAttribute ("file");
            auto file = resolvePath (projectDir, path);
            auto m = readClipInfo (*c);

            if (! file.existsAsFile())
            {
                warnings.add ("Missing audio file: " + file.getFullPathName());
                if (missing != nullptr)
                {
                    m.track = &t;
                    m.fileName = file.getFileName();
                    m.originalPath = file.getFullPathName();
                    missing->push_back (m);
                }
                continue;
            }

            juce::String error;
            if (! placeClip (engine, t, file, m, error))
                warnings.add (error);
        }

        for (auto* p : x.getChildWithTagNameIterator ("INSERT"))
        {
            auto* descXml = p->getChildByName ("PLUGIN");
            juce::PluginDescription desc;

            if (descXml == nullptr || ! desc.loadFromXml (*descXml))
                continue;

            juce::MemoryBlock state;
            if (auto* s = p->getChildByName ("STATE"))
                state.fromBase64Encoding (s->getAllSubText().trim());

            juce::String error;
            if (! engine.addPlugin (t, desc, error, &state, p->getBoolAttribute ("bypassed")))
                warnings.add ("Plugin not available: " + desc.name + " (" + desc.manufacturerName + ")");
        }
    }

    void makeBackup (const juce::File& projectFile, int keep)
    {
        if (keep <= 0 || ! projectFile.existsAsFile())
            return;

        auto folder = projectFile.getParentDirectory().getChildFile ("Backups");
        folder.createDirectory();

        const auto stem = projectFile.getFileNameWithoutExtension();
        projectFile.copyFileTo (folder.getChildFile (stem + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S")
                                                     + Rakam::projectExt));

        auto backups = folder.findChildFiles (juce::File::findFiles, false, stem + " *" + Rakam::projectExt);
        std::sort (backups.begin(), backups.end(),
                   [] (const juce::File& a, const juce::File& b) { return a.getLastModificationTime() > b.getLastModificationTime(); });

        for (int i = keep; i < backups.size(); ++i)
            backups[i].deleteFile();
    }

    /** Copies audio that lives outside the project folder into <project>/Audio and points the clips at the copies. */
    void copyExternalAudio (AudioEngine& engine, const juce::File& dir)
    {
        auto audioDir = dir.getChildFile ("Audio");
        std::map<juce::String, juce::File> copied;

        for (int i = 0; i < engine.getNumTracks(); ++i)
        {
            for (auto& clip : engine.getTrack (i)->clips)
            {
                if (clip->file.isAChildOf (dir) || ! clip->file.existsAsFile())
                    continue;

                const auto key = clip->file.getFullPathName();
                if (copied.find (key) == copied.end())
                {
                    audioDir.createDirectory();
                    auto dest = audioDir.getChildFile (clip->file.getFileName());

                    if (dest.existsAsFile() && dest.getSize() != clip->file.getSize())
                        dest = audioDir.getNonexistentChildFile (clip->file.getFileNameWithoutExtension(), clip->file.getFileExtension(), false);

                    if (! dest.existsAsFile() && ! clip->file.copyFileTo (dest))
                        continue;

                    copied[key] = dest;
                }

                const juce::ScopedLock sl (engine.getLock());
                clip->file = copied[key];
                if (clip->thumbnail != nullptr)
                    clip->thumbnail->setSource (new juce::FileInputSource (clip->file));
            }
        }
    }

    BuiltInPlugin* addFx (AudioEngine& engine, Track& t, const juce::String& effect, const juce::String& preset = {})
    {
        juce::String error;
        for (auto& d : BuiltInPluginFormat::getAllDescriptions())
        {
            if (d.name != effect || ! engine.addPlugin (t, d, error))
                continue;

            auto* p = dynamic_cast<BuiltInPlugin*> (t.plugins.back()->instance.get());
            if (p != nullptr && preset.isNotEmpty())
                for (int i = 0; i < p->getNumPrograms(); ++i)
                    if (p->getProgramName (i) == preset)
                        p->setCurrentProgram (i);
            return p;
        }
        return nullptr;
    }
}

//==============================================================================
bool ProjectIO::save (AudioEngine& engine, const juce::File& projectFile, juce::String& error, const SaveOptions& options)
{
    const auto dir = projectFile.getParentDirectory();

    if (! dir.createDirectory())
    {
        error = "Couldn't create " + dir.getFullPathName();
        return false;
    }

    if (options.copyExternalAudio && options.includeClips)
        copyExternalAudio (engine, dir);

    makeBackup (projectFile, options.backupsToKeep);

    auto xml = buildXml (engine, [&dir] (const juce::File& f)
    {
        return f.isAChildOf (dir) ? f.getRelativePathFrom (dir).replaceCharacter ('\\', '/')
                                  : f.getFullPathName();
    }, options.includeClips);

    // Write to a temporary file first so a crash mid-save never destroys the previous version
    juce::TemporaryFile temp (projectFile);
    if (! xml->writeTo (temp.getFile()) || ! temp.overwriteTargetFileWithTemporary())
    {
        error = "Couldn't write " + projectFile.getFullPathName();
        return false;
    }

    if (options.markAsSaved)
    {
        engine.setProjectFolder (dir);
        engine.markEdited (false);
    }
    return true;
}

bool ProjectIO::load (AudioEngine& engine, const juce::File& projectFile, juce::String& error, juce::StringArray& warnings,
                      MissingList* missing)
{
    auto xml = juce::XmlDocument::parse (projectFile);

    if (xml == nullptr || ! xml->hasTagName ("RAKAM_PROJECT"))
    {
        error = projectFile.getFileName() + " is not a Rakam Studio project.";
        return false;
    }

    engine.clearProject();
    const auto dir = projectFile.getParentDirectory();
    engine.setProjectFolder (dir);
    engine.bpm = xml->getDoubleAttribute ("bpm", 120.0);
    engine.loopEnabled  = xml->getBoolAttribute ("loop");
    engine.loopStartSec = xml->getDoubleAttribute ("loopStart", 0.0);
    engine.loopEndSec   = xml->getDoubleAttribute ("loopEnd", 8.0);

    if (auto* info = xml->getChildByName ("INFO"))
    {
        engine.info.title   = info->getStringAttribute ("title");
        engine.info.artist  = info->getStringAttribute ("artist");
        engine.info.album   = info->getStringAttribute ("album");
        engine.info.genre   = info->getStringAttribute ("genre");
        engine.info.comment = info->getStringAttribute ("comment");
    }

    if (auto* structure = xml->getChildByName ("STRUCTURE"))
        for (auto* x : structure->getChildWithTagNameIterator ("SECTION"))
            engine.sections.push_back ({ x->getStringAttribute ("name"), x->getDoubleAttribute ("start"), x->getDoubleAttribute ("end"),
                                         juce::Colour::fromString (x->getStringAttribute ("colour", "ff4c9aff")) });

    for (auto* tx : xml->getChildWithTagNameIterator ("TRACK"))
        if (auto* t = engine.addTrack (tx->getStringAttribute ("name")))
            readTrack (engine, *t, *tx, dir, warnings, missing);

    if (auto* mx = xml->getChildByName ("MASTER"))
        readTrack (engine, engine.getMaster(), *mx, dir, warnings, missing);

    engine.clearUndoHistory();
    engine.markEdited (false);
    engine.sendChangeMessage();
    return true;
}

int ProjectIO::relinkMissing (AudioEngine& engine, MissingList& missing, const juce::File& searchFolder)
{
    std::map<juce::String, juce::File> found;
    for (const auto& entry : juce::RangedDirectoryIterator (searchFolder, true, "*", juce::File::findFiles))
        found.emplace (entry.getFile().getFileName().toLowerCase(), entry.getFile());

    int relinked = 0;
    for (auto it = missing.begin(); it != missing.end();)
    {
        auto match = found.find (it->fileName.toLowerCase());
        juce::String error;

        if (match != found.end() && engine.indexOf (it->track) >= 0
              && placeClip (engine, *it->track, match->second, *it, error))
        {
            ++relinked;
            it = missing.erase (it);
        }
        else
        {
            ++it;
        }
    }

    if (relinked > 0)
        engine.markEdited();
    return relinked;
}

//==============================================================================
juce::StringArray ProjectIO::getStarterTemplateNames()
{
    return { "Vocal Recording", "Podcast", "Band", "Mixing 8 Stems", "Beat Making" };
}

void ProjectIO::buildStarterTemplate (AudioEngine& engine, const juce::String& name)
{
    auto track = [&engine] (const juce::String& n) { return engine.addTrack (n); };

    if (name == "Vocal Recording")
    {
        track ("Beat");
        auto* lead = track ("Lead Vocal");
        addFx (engine, *lead, "EQ", "Vocal presence");
        addFx (engine, *lead, "Compressor", "Vocal smooth");
        addFx (engine, *lead, "Reverb", "Vocal plate");
        lead->armed = true;
        auto* backing = track ("Backing Vocals");
        addFx (engine, *backing, "EQ", "Vocal presence");
        addFx (engine, *backing, "Compressor", "Vocal smooth");
        backing->gain = 0.7f;
        addFx (engine, engine.getMaster(), "Limiter", "Safe master");
    }
    else if (name == "Podcast")
    {
        for (auto n : { "Host", "Guest" })
        {
            auto* t = track (n);
            addFx (engine, *t, "Gate", "Remove hiss");
            addFx (engine, *t, "EQ", "Vocal presence");
            addFx (engine, *t, "Compressor", "Vocal smooth");
        }
        track ("Music & Jingles")->gain = 0.5f;
        addFx (engine, engine.getMaster(), "Limiter", "Streaming -1");
        engine.getTrack (0)->armed = true;
    }
    else if (name == "Band")
    {
        addFx (engine, *track ("Drums"), "Compressor", "Drum bus glue");
        auto* bass = track ("Bass");
        addFx (engine, *bass, "EQ", "Bass tighten");
        addFx (engine, *bass, "Compressor", "Bass even");
        addFx (engine, *track ("Guitar"), "EQ", "Mud cut");
        track ("Keys");
        auto* vox = track ("Vocals");
        addFx (engine, *vox, "EQ", "Vocal presence");
        addFx (engine, *vox, "Compressor", "Vocal smooth");
        addFx (engine, *vox, "Reverb", "Room");
        addFx (engine, engine.getMaster(), "Limiter", "Safe master");
    }
    else if (name == "Mixing 8 Stems")
    {
        for (auto n : { "Kick", "Snare", "Hats & Perc", "Bass", "Chords", "Lead", "Vocals", "FX" })
            addFx (engine, *track (n), "EQ");
        addFx (engine, engine.getMaster(), "EQ");
        addFx (engine, engine.getMaster(), "Compressor", "Drum bus glue");
        addFx (engine, engine.getMaster(), "Limiter", "Loud master");
    }
    else if (name == "Beat Making")
    {
        addFx (engine, *track ("Drums"), "Compressor", "Punchy kick");
        addFx (engine, *track ("808 / Bass"), "Saturator", "Bass grit");
        addFx (engine, *track ("Melody"), "Delay", "1/8 dotted");
        track ("Samples");
        addFx (engine, engine.getMaster(), "Limiter", "Loud master");
        engine.metronomeOn = true;
    }

    engine.clearUndoHistory();
    engine.markEdited (false);
    engine.sendChangeMessage();
}

juce::Array<juce::File> ProjectIO::getUserTemplates()
{
    auto files = Settings::getTemplatesFolder().findChildFiles (juce::File::findFiles, false, "*" + Rakam::projectExt);
    files.sort();
    return files;
}

bool ProjectIO::saveAsTemplate (AudioEngine& engine, const juce::String& name, juce::String& error)
{
    SaveOptions o;
    o.markAsSaved = false;
    o.includeClips = false;
    return save (engine, Settings::getTemplatesFolder().getChildFile (juce::File::createLegalFileName (name) + Rakam::projectExt), error, o);
}

bool ProjectIO::collectAndZip (AudioEngine& engine, const juce::File& zipFile, juce::String& error, double* progress)
{
    juce::ZipFile::Builder builder;
    std::map<juce::String, juce::String> mapped;
    juce::StringArray usedNames;

    auto xml = buildXml (engine, [&] (const juce::File& f) -> juce::String
    {
        const auto key = f.getFullPathName();
        if (auto it = mapped.find (key); it != mapped.end())
            return it->second;

        auto name = f.getFileName();
        for (int n = 2; usedNames.contains (name, true); ++n)
            name = f.getFileNameWithoutExtension() + "_" + juce::String (n) + f.getFileExtension();

        usedNames.add (name);
        const auto zipPath = "Audio/" + name;
        mapped[key] = zipPath;

        if (f.existsAsFile())
            builder.addFile (f, 0, zipPath);   // audio barely compresses, so store it as-is (fast)

        return zipPath;
    });

    const auto xmlText = xml->toString();
    builder.addEntry (std::make_unique<juce::MemoryInputStream> (juce::MemoryBlock (xmlText.toRawUTF8(), xmlText.getNumBytesAsUTF8()), true),
                      9, "project" + Rakam::projectExt, juce::Time::getCurrentTime());

    // A note listing the plugins the receiver needs installed
    juce::String pluginNote = "Plugins used in this project (install these to hear the mix exactly):\r\n\r\n";
    juce::StringArray seen;
    auto listPlugins = [&] (Track& t)
    {
        for (auto& p : t.plugins)
        {
            auto d = p->instance->getPluginDescription();
            auto line = d.name + " by " + d.manufacturerName + " (" + d.pluginFormatName + ")";
            if (! seen.contains (line))
            {
                seen.add (line);
                pluginNote << " - " << line << " - on " << t.name << "\r\n";
            }
        }
    };
    for (int i = 0; i < engine.getNumTracks(); ++i)
        listPlugins (*engine.getTrack (i));
    listPlugins (engine.getMaster());

    if (seen.isEmpty())
        pluginNote << " (none)\r\n";

    builder.addEntry (std::make_unique<juce::MemoryInputStream> (juce::MemoryBlock (pluginNote.toRawUTF8(), pluginNote.getNumBytesAsUTF8()), true),
                      9, "PLUGINS_NEEDED.txt", juce::Time::getCurrentTime());

    zipFile.deleteFile();
    juce::FileOutputStream out (zipFile);

    if (out.failedToOpen() || ! builder.writeToStream (out, progress))
    {
        error = "Couldn't write " + zipFile.getFullPathName();
        return false;
    }

    return true;
}

juce::File ProjectIO::extractPackage (const juce::File& zipFile, juce::String& error)
{
    juce::ZipFile zip (zipFile);

    if (zip.getNumEntries() == 0)
    {
        error = zipFile.getFileName() + " is not a valid package.";
        return {};
    }

    auto dest = Rakam::getUserFolder().getChildFile ("Imported")
                    .getNonexistentChildFile (zipFile.getFileNameWithoutExtension(), {}, false);

    if (! dest.createDirectory())
    {
        error = "Couldn't create " + dest.getFullPathName();
        return {};
    }

    auto result = zip.uncompressTo (dest, true);
    if (result.failed())
    {
        error = result.getErrorMessage();
        return {};
    }

    auto projects = dest.findChildFiles (juce::File::findFiles, true, "*" + Rakam::projectExt);
    if (projects.isEmpty())
    {
        error = "No project file found inside " + zipFile.getFileName();
        return {};
    }

    return projects.getFirst();
}
