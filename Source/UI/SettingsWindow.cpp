#include "UI/SettingsWindow.h"

using namespace Rakam::Colours;

namespace
{
    /** A simple page of labelled rows. */
    class Page : public juce::Component
    {
    public:
        void addHeading (const juce::String& text)
        {
            auto& r = rows.emplace_back();
            r.heading = std::make_unique<juce::Label> (juce::String(), text.toUpperCase());
            r.heading->setFont (juce::FontOptions (11.5f, juce::Font::bold));
            r.heading->setColour (juce::Label::textColourId, accent);
            addAndMakeVisible (*r.heading);
            r.height = 30;
        }

        juce::Component* addRow (const juce::String& label, std::unique_ptr<juce::Component> control,
                                 const juce::String& hint = {}, int height = 28)
        {
            auto& r = rows.emplace_back();
            r.label = std::make_unique<juce::Label> (juce::String(), label);
            r.label->setColour (juce::Label::textColourId, text);
            addAndMakeVisible (*r.label);

            r.control = std::move (control);
            addAndMakeVisible (*r.control);

            if (hint.isNotEmpty())
            {
                r.hint = std::make_unique<juce::Label> (juce::String(), hint);
                r.hint->setColour (juce::Label::textColourId, textDim);
                r.hint->setFont (juce::FontOptions (12.0f));
                r.hint->setMinimumHorizontalScale (0.8f);
                addAndMakeVisible (*r.hint);
            }

            r.height = height + (hint.isNotEmpty() ? 20 : 0);
            return r.control.get();
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (20, 16);
            for (auto& r : rows)
            {
                auto row = area.removeFromTop (r.height);
                area.removeFromTop (8);

                if (r.heading != nullptr)
                {
                    r.heading->setBounds (row.withTrimmedTop (8));
                    continue;
                }

                auto top = row.removeFromTop (row.getHeight() - (r.hint != nullptr ? 20 : 0));
                r.label->setBounds (top.removeFromLeft (190));
                r.control->setBounds (top.withWidth (juce::jmin (top.getWidth(), 360)));
                if (r.hint != nullptr)
                    r.hint->setBounds (row.withTrimmedLeft (190));
            }
        }

    private:
        struct Row
        {
            std::unique_ptr<juce::Label> heading, label, hint;
            std::unique_ptr<juce::Component> control;
            int height = 28;
        };
        std::vector<Row> rows;
    };

    std::unique_ptr<juce::ComboBox> makeChoice (juce::PropertiesFile& s, const char* key, int def,
                                                const juce::StringArray& names, const juce::Array<int>& values,
                                                std::function<void()> changed)
    {
        auto box = std::make_unique<juce::ComboBox>();
        for (int i = 0; i < names.size(); ++i)
            box->addItem (names[i], i + 1);

        const int current = s.getIntValue (key, def);
        box->setSelectedId (juce::jmax (1, values.indexOf (current) + 1), juce::dontSendNotification);
        box->onChange = [&s, key, values, b = box.get(), changed]
        {
            s.setValue (key, values[b->getSelectedId() - 1]);
            s.saveIfNeeded();
            if (changed) changed();
        };
        return box;
    }

    std::unique_ptr<juce::ToggleButton> makeToggle (juce::PropertiesFile& s, const char* key, bool def,
                                                    const juce::String& text, std::function<void()> changed)
    {
        auto b = std::make_unique<juce::ToggleButton> (text);
        b->setToggleState (s.getBoolValue (key, def), juce::dontSendNotification);
        b->onClick = [&s, key, t = b.get(), changed]
        {
            s.setValue (key, t->getToggleState());
            s.saveIfNeeded();
            if (changed) changed();
        };
        return b;
    }

    /** Path label + Browse button for choosing a folder or a file. */
    class PathChooser : public juce::Component
    {
    public:
        PathChooser (juce::PropertiesFile& s, const char* k, juce::File current, bool chooseFolder, juce::String filePattern = {})
            : settings (s), key (k), folder (chooseFolder), pattern (std::move (filePattern))
        {
            path.setText (current == juce::File() ? juce::String ("(not set)") : current.getFullPathName(), juce::dontSendNotification);
            path.setColour (juce::Label::textColourId, textDim);
            path.setColour (juce::Label::backgroundColourId, background);
            path.setMinimumHorizontalScale (0.5f);
            addAndMakeVisible (path);

            browse.onClick = [this, current]
            {
                chooser = std::make_unique<juce::FileChooser> (folder ? "Choose a folder" : "Choose a file", current, pattern);
                const int chooserFlags = juce::FileBrowserComponent::openMode
                                  | (folder ? juce::FileBrowserComponent::canSelectDirectories : juce::FileBrowserComponent::canSelectFiles);
                chooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult();
                    if (f == juce::File())
                        return;
                    settings.setValue (key, f.getFullPathName());
                    settings.saveIfNeeded();
                    path.setText (f.getFullPathName(), juce::dontSendNotification);
                });
            };
            addAndMakeVisible (browse);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            browse.setBounds (r.removeFromRight (84));
            r.removeFromRight (6);
            path.setBounds (r);
        }

    private:
        juce::PropertiesFile& settings;
        const char* key;
        bool folder;
        juce::String pattern;
        juce::Label path;
        juce::TextButton browse { "Browse..." };
        std::unique_ptr<juce::FileChooser> chooser;
    };
}

//==============================================================================
SettingsPanel::SettingsPanel (AudioEngine& engine, int initialTab)
{
    auto& s = engine.getSettings();
    auto changed = [this] { if (onChanged) onChanged(); };

    // ---- General
    auto* general = new Page();
    general->addHeading ("Interface");
    general->addRow ("Interface size", makeChoice (s, Settings::uiScale, 100, { "90%", "100%", "110%", "125%", "150%" },
                                                   { 90, 100, 110, 125, 150 }, changed),
                     "Makes everything bigger or smaller (good for laptops and 4K screens)");
    general->addRow ("Track height", makeChoice (s, Settings::trackHeight, 86, { "Compact", "Normal", "Large" }, { 64, 86, 120 }, changed));
    general->addRow ("", makeToggle (s, Settings::confirmDelete, true, "Ask before deleting a track", changed));
    general->addHeading ("New projects");
    general->addRow ("When the app starts", makeChoice (s, Settings::startupAction, 3,
                                                        { "Empty project", "Reopen last project", "Show start screen" }, { 1, 2, 3 }, changed));
    general->addRow ("Default tempo", makeChoice (s, Settings::defaultBpm, 120,
                                                  { "70 BPM", "85 BPM", "90 BPM", "100 BPM", "110 BPM", "120 BPM", "128 BPM", "140 BPM", "160 BPM" },
                                                  { 70, 85, 90, 100, 110, 120, 128, 140, 160 }, changed));

    // ---- Audio
    auto* audio = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager(), 0, 64, 2, 64, false, false, true, false);

    // ---- Recording
    auto* recording = new Page();
    recording->addHeading ("Recording");
    recording->addRow ("File format", makeChoice (s, Settings::recordFormat, 1,
                                                  { "WAV 24-bit (recommended)", "WAV 32-bit float (never clips)", "WAV 16-bit", "FLAC 24-bit (half the size)" },
                                                  { 1, 2, 3, 4 }, changed));
    recording->addRow ("Record mode", makeChoice (s, Settings::recordMode, 1,
                                                  { "Normal (layer on top)", "Replace", "Punch in/out (loop range)", "Loop takes (loop range)" },
                                                  { 1, 2, 3, 4 }, changed),
                       "Also in the Recorder tab at the bottom of the main window");
    recording->addRow ("Count-in", makeChoice (s, Settings::countInBars, 0, { "Off", "1 bar", "2 bars" }, { 0, 1, 2 }, changed),
                       "Plays the metronome before recording starts so you can come in on time");
    recording->addRow ("", makeToggle (s, Settings::autoMonitor, false, "Turn on input monitoring when a track is armed", changed),
                       "Use headphones to avoid feedback");
    recording->addRow ("Recordings go to", std::make_unique<juce::Label> (juce::String(), "The project's Audio folder"),
                       "Before the first save: Documents\\Rakam Studio\\Recordings");

    // ---- Files
    auto* files = new Page();
    files->addHeading ("Folders");
    files->addRow ("Projects folder", std::make_unique<PathChooser> (s, Settings::projectsFolder,
                                                                     Settings::getFolder (s, Settings::projectsFolder, "Projects"), true));
    files->addRow ("Exports folder", std::make_unique<PathChooser> (s, Settings::exportsFolder,
                                                                    Settings::getFolder (s, Settings::exportsFolder, "Exports"), true));
    files->addHeading ("Safety");
    files->addRow ("Autosave", makeChoice (s, Settings::autosaveMinutes, 2, { "Off", "Every minute", "Every 2 minutes", "Every 5 minutes", "Every 10 minutes" },
                                           { 0, 1, 2, 5, 10 }, changed),
                   "Keeps a recovery copy - if the app or PC crashes, you'll be offered to restore it");
    files->addRow ("Backups", makeChoice (s, Settings::backupCount, 5, { "Off", "Keep 3", "Keep 5", "Keep 10", "Keep 20" }, { 0, 3, 5, 10, 20 }, changed),
                   "Each save keeps the previous version in the project's Backups folder");
    files->addRow ("", makeToggle (s, Settings::copyAudioOnSave, true, "Copy imported audio into the project folder when saving", changed),
                   "Keeps projects complete even if you move or delete the original files");

    auto clearRecent = std::make_unique<juce::TextButton> ("Clear recent projects list");
    clearRecent->onClick = [this] { if (onClearRecent) onClearRecent(); };
    files->addRow ("", std::move (clearRecent));

    auto openFolder = std::make_unique<juce::TextButton> ("Open settings folder");
    openFolder->onClick = [&s] { s.getFile().getParentDirectory().startAsProcess(); };
    files->addRow ("", std::move (openFolder));

    // ---- Export
    auto* exportPage = new Page();
    exportPage->addHeading ("Export defaults");
    exportPage->addRow ("Format", makeChoice (s, Settings::exportFormat, 1,
                                              { "WAV 24-bit", "WAV 16-bit", "WAV 32-bit float", "FLAC 24-bit", "OGG Vorbis", "MP3 320 kbps" },
                                              { 1, 2, 3, 4, 5, 6 }, changed));
    exportPage->addRow ("", makeToggle (s, Settings::exportNormalize, false, "Normalize the full mix to -1 dB peak", changed));
    exportPage->addHeading ("MP3 encoder");
    exportPage->addRow ("lame.exe", std::make_unique<PathChooser> (s, Settings::lamePath, juce::File (s.getValue (Settings::lamePath)), false, "*.exe;lame*"),
                        "MP3 export uses the free LAME encoder - download it once, then choose lame.exe here");
    auto link = std::make_unique<juce::HyperlinkButton> ("Download LAME for Windows (rarewares.org)",
                                                         juce::URL ("https://www.rarewares.org/mp3-lame-bundle.php"));
    link->setJustificationType (juce::Justification::centredLeft);
    link->setColour (juce::HyperlinkButton::textColourId, monitor);
    link->setFont (juce::FontOptions (14.0f), false, juce::Justification::centredLeft);
    exportPage->addRow ("", std::move (link));

    // ---- Plugins
    auto* plugins = new Page();
    plugins->addHeading ("Effects");
    plugins->addRow ("Built-in", std::make_unique<juce::Label> (juce::String(), juce::String (BuiltInPluginFormat::getEffectNames().size())
                                                                + " effects, always available"));
    plugins->addRow ("VST3", std::make_unique<juce::Label> (juce::String(), juce::String (engine.getKnownPlugins().getNumTypes()) + " plugins scanned"));
    auto manager = std::make_unique<juce::TextButton> ("Open Plugin Manager (scan VST3)");
    manager->onClick = [this] { if (onOpenPluginManager) onOpenPluginManager(); };
    plugins->addRow ("", std::move (manager), "VST3 plugins are usually in C:\\Program Files\\Common Files\\VST3");

    addPage ("General",   general);
    addPage ("Audio",     audio);
    addPage ("Recording", recording);
    addPage ("Files",     files);
    addPage ("Export",    exportPage);
    addPage ("Plugins",   plugins);
    showPage (juce::jlimit (0, pages.size() - 1, initialTab));

    setSize (760, 560);
}

void SettingsPanel::addPage (const juce::String& name, juce::Component* page)
{
    pages.add (page);
    addChildComponent (page);

    auto* b = navButtons.add (new juce::TextButton (name));
    const int index = navButtons.size() - 1;
    b->setClickingTogglesState (true);
    b->setRadioGroupId (4242);
    b->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    b->setColour (juce::TextButton::buttonOnColourId, panelLight);
    b->setColour (juce::TextButton::textColourOnId, accent);
    b->setColour (juce::TextButton::textColourOffId, textDim);
    b->onClick = [this, index] { showPage (index); };
    addAndMakeVisible (b);
}

void SettingsPanel::showPage (int index)
{
    currentPage = index;
    for (int i = 0; i < pages.size(); ++i)
    {
        pages[i]->setVisible (i == index);
        navButtons[i]->setToggleState (i == index, juce::dontSendNotification);
    }
    resized();
}

void SettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    g.setColour (background);
    g.fillRect (getLocalBounds().removeFromLeft (150));
}

void SettingsPanel::resized()
{
    auto r = getLocalBounds();
    auto nav = r.removeFromLeft (150).reduced (10, 14);
    for (auto* b : navButtons)
    {
        b->setBounds (nav.removeFromTop (34));
        nav.removeFromTop (4);
    }

    for (auto* p : pages)
        p->setBounds (r.reduced (4));
}
