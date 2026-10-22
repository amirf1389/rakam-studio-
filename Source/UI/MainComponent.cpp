#include "UI/MainComponent.h"
#include "UI/ExportDialog.h"
#include "UI/StemSeparatorDialog.h"
#include "UI/BackgroundTask.h"
#include "UI/SettingsWindow.h"
#include "Engine/ProjectIO.h"

using namespace Rakam::Colours;

//==============================================================================
/** A floating window showing a plugin's own editor. */
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioPluginInstance& p, std::function<void (PluginWindow*)> closeCallback)
        : juce::DocumentWindow (p.getName(), panel, juce::DocumentWindow::closeButton),
          plugin (p), onClose (std::move (closeCallback))
    {
        setUsingNativeTitleBar (true);

        juce::AudioProcessorEditor* editor = p.hasEditor() ? p.createEditorAndMakeActive() : nullptr;
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (p);

        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        setTopLeftPosition (120 + juce::Random::getSystemRandom().nextInt (200),
                            120 + juce::Random::getSystemRandom().nextInt (120));
        setVisible (true);
        setAlwaysOnTop (true);
    }

    ~PluginWindow() override
    {
        clearContentComponent();
    }

    void closeButtonPressed() override
    {
        if (onClose)
            onClose (this);
    }

    juce::AudioProcessor& plugin;

private:
    std::function<void (PluginWindow*)> onClose;
};

//==============================================================================
MainComponent::MainComponent (juce::PropertiesFile& s)
    : settings (s), engine (s), transport (engine), arrangement (engine), mixer (engine), recorderPanel (engine), ioPanel (engine), browser (engine), statusBar (engine)
{
    mixerVisible   = settings.getBoolValue ("showMixer", true);
    browserVisible = settings.getBoolValue ("showBrowser", true);

    addAndMakeVisible (menuBar);
    addAndMakeVisible (transport);
    addAndMakeVisible (arrangement);
    bottomPanel.addPage ("Mixer", mixer);
    bottomPanel.addPage ("Recorder", recorderPanel);
    bottomPanel.addPage ("I/O", ioPanel);
    bottomPanel.showPage (settings.getIntValue ("bottomPage", 0));
    bottomPanel.onPageChanged = [this] (int p) { settings.setValue ("bottomPage", p); };
    addChildComponent (bottomPanel);

    recorderPanel.onRecord = [this] { startRecording(); };
    recorderPanel.onOpenIO = [this] { showBottomPage (2); };
    ioPanel.onOpenAudioSettings = [this] { showSettings (1); };
    addChildComponent (browser);
    addAndMakeVisible (statusBar);
    bottomPanel.setVisible (mixerVisible);
    browser.setVisible (browserVisible);

    layout.setItemLayout (0, 150, -1.0, -0.62);  // arrangement
    layout.setItemLayout (1, 6, 6, 6);           // divider
    layout.setItemLayout (2, 260, 700, 370);     // mixer
    divider = std::make_unique<juce::StretchableLayoutResizerBar> (&layout, 1, false);
    addChildComponent (*divider);
    divider->setVisible (mixerVisible);

    transport.onRecord      = [this] { startRecording(); };
    transport.onExport      = [this] { showExportDialog(); };
    transport.onSeparate    = [this] { showStemSeparator ({}, nullptr, nullptr); };
    transport.onToggleMixer   = [this] { toggleMixer(); };
    transport.onToggleBrowser = [this] { toggleBrowser(); };
    transport.onToggleLoop    = [this] { toggleLoop(); };
    transport.onUndo          = [this] { undo(); };
    transport.onRedo          = [this] { redo(); };
    transport.setPanelStates (browserVisible, mixerVisible);
    transport.onAddTrack    = [this] { addTrack(); };
    transport.onSettings    = [this] { showSettings (1); };

    arrangement.onImportRequest = [this] (Track* t, double sec) { importAudioDialog (t, sec); };
    arrangement.onError = [this] (const juce::String& e) { showMessage ("Import", e); };
    arrangement.onSeparateClip = [this] (Track& t, AudioClip& c) { showStemSeparator (c.file, &t, &c); };

    mixer.onAddPlugin  = [this] (Track& t, juce::Component* anchor) { showAddPluginMenu (t, anchor); };
    mixer.onOpenPlugin = [this] (juce::AudioPluginInstance& p) { openPluginWindow (p); };

    browser.onImportFile = [this] (const juce::File& f)
    {
        auto* t = arrangement.getSelectedTrack();
        if (t == nullptr)
            t = engine.addTrack (f.getFileNameWithoutExtension());
        juce::String error;
        if (engine.importFile (f, *t, engine.getPositionSec(), error) == nullptr)
            showMessage ("Import", error);
    };
    browser.onAddEffect = [this] (const juce::PluginDescription& d)
    {
        auto* t = arrangement.getSelectedTrack();
        addEffect (t != nullptr ? *t : engine.getMaster(), d);
    };

    statusBar.onClick = [this] { showSettings (1); };

    engine.onPluginAboutToBeDeleted = [this] (juce::AudioProcessor* p) { closePluginWindow (p); };
    engine.addChangeListener (this);
    engine.getDeviceManager().addChangeListener (this);

    addKeyListener (this);
    setWantsKeyboardFocus (true);

    recent.setMaxNumberOfItems (12);
    recent.restoreFromString (settings.getValue (Settings::recentFiles));
    lastAutosave = juce::Time::getMillisecondCounter();
    applySettings();

    setSize (1400, 860);
    updateTitle();
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    // Normal shutdown: the crash-recovery copy is no longer needed
    settings.setValue (Settings::cleanExit, true);
    Settings::getAutosaveFolder().getChildFile ("autosave" + Rakam::projectExt).deleteFile();
    settings.saveIfNeeded();

    engine.onPluginAboutToBeDeleted = nullptr;
    engine.removeChangeListener (this);
    engine.getDeviceManager().removeChangeListener (this);
    removeKeyListener (this);
    pluginWindows.clear();
    pluginManagerWindow.reset();
    menuBar.setModel (nullptr);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    menuBar.setBounds (r.removeFromTop (24));
    transport.setBounds (r.removeFromTop (54));
    statusBar.setBounds (r.removeFromBottom (24));

    if (browserVisible)
        browser.setBounds (r.removeFromLeft (250));

    if (mixerVisible)
    {
        juce::Component* comps[] = { &arrangement, divider.get(), &bottomPanel };
        layout.layOutComponents (comps, 3, r.getX(), r.getY(), r.getWidth(), r.getHeight(), true, true);
    }
    else
    {
        arrangement.setBounds (r);
    }
}

void MainComponent::timerCallback()
{
    for (int i = 0; i < engine.getNumTracks(); ++i)
        engine.getTrack (i)->updateMeters();
    engine.getMaster().updateMeters();

    arrangement.updateMeters();
    engine.updateIOMeters();
    if (mixerVisible && bottomPanel.getCurrentPage() == 0)
        mixer.updateMeters();

    const int minutes = Settings::getInt (settings, Settings::autosaveMinutes, 2);
    if (minutes > 0 && juce::Time::getMillisecondCounter() - lastAutosave > (juce::uint32) minutes * 60000u)
        doAutosave();
}

void MainComponent::doAutosave()
{
    lastAutosave = juce::Time::getMillisecondCounter();

    if (! engine.hasUnsavedChanges() || engine.isRecording() || engine.getNumTracks() == 0)
        return;

    ProjectIO::SaveOptions o;
    o.markAsSaved = false;
    juce::String error;
    if (ProjectIO::save (engine, Settings::getAutosaveFolder().getChildFile ("autosave" + Rakam::projectExt), error, o))
    {
        settings.setValue ("autosaveSource", currentFile.getFullPathName());
        statusBar.setMessage ("Autosaved recovery copy");
    }
}

void MainComponent::applySettings()
{
    const double scale = Settings::getInt (settings, Settings::uiScale, 100) / 100.0;
    if (std::abs (juce::Desktop::getInstance().getGlobalScaleFactor() - (float) scale) > 0.001f)
        juce::Desktop::getInstance().setGlobalScaleFactor ((float) scale);

    arrangement.setTrackHeight (Settings::getInt (settings, Settings::trackHeight, 86));
}

void MainComponent::addToRecent (const juce::File& f)
{
    if (f.isAChildOf (Settings::getAutosaveFolder()) || f.isAChildOf (Settings::getTemplatesFolder()))
        return;

    recent.addFile (f);
    settings.setValue (Settings::recentFiles, recent.toString());
    settings.setValue (Settings::lastProject, f.getFullPathName());
    settings.saveIfNeeded();
}

void MainComponent::handleStartup (const juce::File& fileFromCommandLine)
{
    const bool crashed = ! settings.getBoolValue (Settings::cleanExit, true);
    settings.setValue (Settings::cleanExit, false);
    settings.saveIfNeeded();

    const auto autosave = Settings::getAutosaveFolder().getChildFile ("autosave" + Rakam::projectExt);

    if (crashed && autosave.existsAsFile())
    {
        juce::Component::SafePointer<MainComponent> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle ("Recover your work?")
                                          .withMessage ("Rakam Studio didn't close properly last time.\n\n"
                                                        "An autosaved copy from " + autosave.getLastModificationTime().formatted ("%H:%M on %d %b")
                                                        + " is available. Restore it?")
                                          .withButton ("Restore")
                                          .withButton ("Discard"),
                                      [safe, autosave, fileFromCommandLine] (int result)
                                      {
                                          if (safe == nullptr)
                                              return;

                                          if (result == 1)
                                          {
                                              safe->openFile (autosave);
                                              const juce::File source (safe->settings.getValue ("autosaveSource"));
                                              safe->currentFile = source.existsAsFile() ? source : juce::File();
                                              if (safe->currentFile != juce::File())
                                                  safe->engine.setProjectFolder (safe->currentFile.getParentDirectory());
                                              safe->engine.markEdited (true);
                                              safe->updateTitle();
                                              safe->statusBar.setMessage ("Recovered - save to keep it");
                                          }
                                          else
                                          {
                                              autosave.deleteFile();
                                              safe->handleStartup (fileFromCommandLine);
                                          }
                                      });
        return;
    }

    if (fileFromCommandLine.existsAsFile())
    {
        openFile (fileFromCommandLine);
        return;
    }

    switch (Settings::getInt (settings, Settings::startupAction, 3))
    {
        case 2:
        {
            const juce::File last (settings.getValue (Settings::lastProject));
            if (last.existsAsFile())
                openFile (last);
            break;
        }
        case 3:
            showStartScreen();
            break;
        default:
            break;
    }
}

void MainComponent::showStartScreen()
{
    juce::Array<juce::File> files;
    for (int i = 0; i < recent.getNumFiles(); ++i)
        if (recent.getFile (i).existsAsFile())
            files.add (recent.getFile (i));

    auto* screen = new StartScreen (files);
    juce::Component::SafePointer<MainComponent> safe (this);
    screen->onNew        = [safe] (const StartScreen::Choice& c) { if (safe != nullptr) safe->newFromTemplate (c); };
    screen->onOpenRecent = [safe] (const juce::File& f) { if (safe != nullptr) safe->confirmDiscard ([safe, f] { if (safe != nullptr) safe->openFile (f); }); };
    screen->onOpenOther  = [safe] { if (safe != nullptr) safe->openProjectDialog(); };

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (screen);
    o.dialogTitle = "Welcome";
    o.dialogBackgroundColour = panel;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.componentToCentreAround = this;
    o.launchAsync();
}

void MainComponent::newFromTemplate (const StartScreen::Choice& choice)
{
    confirmDiscard ([this, choice]
    {
        pluginWindows.clear();

        if (choice.file.existsAsFile())
        {
            juce::String error;
            juce::StringArray warnings;
            ProjectIO::load (engine, choice.file, error, warnings);
            engine.setProjectFolder ({});
            engine.markEdited (false);
            if (! warnings.isEmpty())
                showMessage ("Template", warnings.joinIntoString ("\n"));
        }
        else
        {
            engine.clearProject();
            if (choice.name.isNotEmpty())
                ProjectIO::buildStarterTemplate (engine, choice.name);
        }

        currentFile = juce::File();
        missingClips.clear();
        arrangement.select (engine.getNumTracks() > 0 ? engine.getTrack (0) : nullptr, nullptr);
        updateTitle();
        if (choice.name.isNotEmpty())
            statusBar.setMessage ("New project from template: " + choice.name);
    });
}

void MainComponent::saveCopy()
{
    auto start = getDefaultProjectFolder().getChildFile ((currentFile.existsAsFile() ? currentFile.getFileNameWithoutExtension()
                                                                                      : juce::String ("My Song")) + " copy" + Rakam::projectExt);
    chooser = std::make_unique<juce::FileChooser> ("Save a copy", start, "*" + Rakam::projectExt);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult() == juce::File())
                                  return;
                              ProjectIO::SaveOptions o;
                              o.markAsSaved = false;
                              juce::String error;
                              const auto f = fc.getResult().withFileExtension (Rakam::projectExt);
                              if (ProjectIO::save (engine, f, error, o))
                                  statusBar.setMessage ("Saved a copy: " + f.getFileName());
                              else
                                  showMessage ("Save a copy", error);
                          });
}

void MainComponent::saveTemplateDialog()
{
    auto* aw = new juce::AlertWindow ("Save as template",
                                      "Saves the tracks, effects and mixer settings (no audio) so you can start new songs from it.",
                                      juce::MessageBoxIconType::NoIcon, this);
    aw->addTextEditor ("name", currentFile.existsAsFile() ? currentFile.getFileNameWithoutExtension() : juce::String ("My Template"), "Name");
    aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw] (int result)
    {
        if (safe == nullptr || result != 1)
            return;
        const auto name = aw->getTextEditorContents ("name").trim();
        if (name.isEmpty())
            return;
        juce::String error;
        if (ProjectIO::saveAsTemplate (safe->engine, name, error))
            safe->statusBar.setMessage ("Template saved: " + name);
        else
            safe->showMessage ("Save as template", error);
    }), true);
}

void MainComponent::revertToSaved()
{
    if (! currentFile.existsAsFile())
        return;

    juce::Component::SafePointer<MainComponent> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon, "Revert to saved",
                                                                                "Throw away all changes since the last save?", "Revert", "Cancel"),
                                  [safe] (int result)
                                  {
                                      if (safe != nullptr && result == 1)
                                          safe->openFile (safe->currentFile);
                                  });
}

void MainComponent::showProjectInfo()
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new ProjectInfoDialog (engine));
    o.dialogTitle = "Project Info";
    o.dialogBackgroundColour = panel;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.componentToCentreAround = this;
    o.launchAsync();
}

void MainComponent::findMissingFiles()
{
    if (missingClips.empty())
    {
        showMessage ("Find missing files", "Nothing is missing.", false);
        return;
    }

    chooser = std::make_unique<juce::FileChooser> ("Choose a folder to search for " + juce::String ((int) missingClips.size()) + " missing file(s)",
                                                   Rakam::getUserFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (! fc.getResult().isDirectory())
                                  return;

                              const int before = (int) missingClips.size();
                              juce::MouseCursor::showWaitCursor();
                              const int found = ProjectIO::relinkMissing (engine, missingClips, fc.getResult());
                              juce::MouseCursor::hideWaitCursor();

                              showMessage ("Find missing files", "Found " + juce::String (found) + " of " + juce::String (before) + " missing file(s)."
                                           + (missingClips.empty() ? juce::String ("\nSave the project to keep the new locations.")
                                                                   : juce::String ("\nTry another folder for the rest.")), false);
                          });
}

void MainComponent::showSettings (int tab)
{
    auto* panelComp = new SettingsPanel (engine, tab);
    juce::Component::SafePointer<MainComponent> safe (this);
    panelComp->onChanged = [safe] { if (safe != nullptr) safe->applySettings(); };
    panelComp->onOpenPluginManager = [safe] { if (safe != nullptr) safe->showPluginManager(); };
    panelComp->onClearRecent = [safe]
    {
        if (safe == nullptr)
            return;
        safe->recent.clear();
        safe->settings.setValue (Settings::recentFiles, juce::String());
        safe->statusBar.setMessage ("Recent projects list cleared");
    };

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (panelComp);
    o.dialogTitle = "Settings";
    o.dialogBackgroundColour = panel;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    o.launchAsync();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &engine.getDeviceManager())
    {
        arrangement.refreshInputs();
        engine.saveSettings();
    }

    updateTitle();
}

void MainComponent::updateTitle()
{
    const auto name = currentFile.existsAsFile() ? currentFile.getFileNameWithoutExtension() : juce::String ("Untitled");
    auto title = name + (engine.hasUnsavedChanges() ? " *" : "") + " - " + Rakam::appName;

    if (auto* device = engine.getDeviceManager().getCurrentAudioDevice())
        title << "   [" << device->getTypeName() << ": " << device->getName() << ", "
              << juce::String (device->getCurrentSampleRate(), 0) << " Hz, "
              << device->getCurrentBufferSizeSamples() << " samples]";
    else
        title << "   [no audio device]";

    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        if (window->getName() != title)
            window->setName (title);
}

juce::File MainComponent::getDefaultProjectFolder() const
{
    auto f = Settings::getFolder (settings, Settings::projectsFolder, "Projects");
    return f;
}

void MainComponent::showMessage (const juce::String& title, const juce::String& text, bool warning)
{
    juce::AlertWindow::showMessageBoxAsync (warning ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::InfoIcon,
                                            title, text);
}

//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "Track", "Structure", "View", "Options", "Help" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    auto item = [] (const juce::String& text, const juce::String& shortcut, std::function<void()> action, bool enabled = true)
    {
        juce::PopupMenu::Item i (text);
        i.shortcutKeyDescription = shortcut;
        i.action = std::move (action);
        i.isEnabled = enabled;
        return i;
    };

    juce::PopupMenu m;

    if (index == 0)
    {
        m.addItem (item ("New Project", "Ctrl+N", [this] { newProject(); }));

        juce::PopupMenu templatesMenu;
        for (auto& name : ProjectIO::getStarterTemplateNames())
            templatesMenu.addItem (name, [this, name] { newFromTemplate ({ name, {} }); });
        const auto userTemplates = ProjectIO::getUserTemplates();
        if (! userTemplates.isEmpty())
        {
            templatesMenu.addSeparator();
            for (auto& f : userTemplates)
                templatesMenu.addItem (f.getFileNameWithoutExtension(), [this, f] { newFromTemplate ({ f.getFileNameWithoutExtension(), f }); });
        }
        templatesMenu.addSeparator();
        templatesMenu.addItem ("Open templates folder", [] { Settings::getTemplatesFolder().startAsProcess(); });
        m.addSubMenu ("New from Template", templatesMenu);
        m.addItem (item ("Start Screen...", {}, [this] { showStartScreen(); }));
        m.addSeparator();

        m.addItem (item ("Open Project or Package (.rakam / .zip)...", "Ctrl+O", [this] { openProjectDialog(); }));
        juce::PopupMenu recentMenu;
        for (int i = 0; i < recent.getNumFiles(); ++i)
        {
            auto f = recent.getFile (i);
            recentMenu.addItem (juce::PopupMenu::Item (f.getFileNameWithoutExtension() + "   (" + f.getParentDirectory().getFileName() + ")")
                                    .setEnabled (f.existsAsFile())
                                    .setAction ([this, f] { confirmDiscard ([this, f] { openFile (f); }); }));
        }
        if (recent.getNumFiles() > 0)
        {
            recentMenu.addSeparator();
            recentMenu.addItem ("Clear list", [this] { recent.clear(); settings.setValue (Settings::recentFiles, juce::String()); });
        }
        m.addSubMenu ("Open Recent", recentMenu, recent.getNumFiles() > 0);
        m.addSeparator();

        m.addItem (item ("Save", "Ctrl+S", [this] { saveProject(); }));
        m.addItem (item ("Save As...", "Ctrl+Shift+S", [this] { saveProjectAs(); }));
        m.addItem (item ("Save a Copy...", {}, [this] { saveCopy(); }));
        m.addItem (item ("Save as Template...", {}, [this] { saveTemplateDialog(); }));
        m.addItem (item ("Revert to Saved", {}, [this] { revertToSaved(); }, currentFile.existsAsFile() && engine.hasUnsavedChanges()));
        m.addSeparator();
        m.addItem (item ("Project Info (title, artist)...", {}, [this] { showProjectInfo(); }));
        m.addItem (item ("Find Missing Files..." + (missingClips.empty() ? juce::String() : "  (" + juce::String ((int) missingClips.size()) + ")"),
                         {}, [this] { findMissingFiles(); }, ! missingClips.empty()));
        m.addItem (item ("Show Project Folder", {}, [this] { currentFile.revealToUser(); }, currentFile.existsAsFile()));
        m.addSeparator();
        m.addItem (item ("Import Audio...", "Ctrl+I", [this] { importAudioDialog (arrangement.getSelectedTrack(), engine.getPositionSec()); }));
        m.addSeparator();
        m.addSectionHeader ("Bring out your mix");
        m.addItem (item ("Export Mix / Stems...", "Ctrl+E", [this] { showExportDialog(); }));
        m.addItem (item ("Collect && Zip Project (share / move to another PC)...", {}, [this] { collectAndZip(); }));
        m.addItem (item ("Separate Song into Stems (AI)...", {}, [this] { showStemSeparator ({}, nullptr, nullptr); }));
        m.addSeparator();
        m.addItem (item ("Settings...", "Ctrl+,", [this] { showSettings(); }));
        m.addItem (item ("Quit", "Alt+F4", [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }));
    }
    else if (index == 1)
    {
        auto& um = engine.getUndoManager();
        m.addItem (item ("Undo " + um.getUndoDescription(), "Ctrl+Z", [this] { undo(); }, um.canUndo()));
        m.addItem (item ("Redo " + um.getRedoDescription(), "Ctrl+Y", [this] { redo(); }, um.canRedo()));
        m.addSeparator();
        const bool hasClip = arrangement.getSelectedClip() != nullptr;
        m.addItem (item ("Split Clip at Playhead", "S", [this] { arrangement.splitSelectedClipAtPlayhead(); }, hasClip));
        m.addItem (item ("Duplicate Clip", "Ctrl+D", [this] { arrangement.duplicateSelectedClip(); }, hasClip));
        m.addItem (item ("Normalize Clip", {}, [this] { if (auto* c = arrangement.getSelectedClip()) engine.normalizeClip (*c); }, hasClip));
        m.addItem (item ("Delete Clip", "Del", [this] { arrangement.deleteSelectedClip(); }, hasClip));
    }
    else if (index == 2)
    {
        m.addItem (item ("Add Audio Track", "Ctrl+T", [this] { addTrack(); }));
        m.addItem (item ("Delete Selected Track", {}, [this]
        {
            if (auto* t = arrangement.getSelectedTrack())
                engine.removeTrack (engine.indexOf (t));
        }, arrangement.getSelectedTrack() != nullptr));
        m.addItem (item ("Duplicate Selected Track", {}, [this]
        {
            if (auto* t = arrangement.getSelectedTrack())
                arrangement.select (engine.duplicateTrack (engine.indexOf (t)), nullptr);
        }, arrangement.getSelectedTrack() != nullptr));
    }
    else if (index == 3)
    {
        const int current = engine.sectionAt (engine.getPositionSec());
        m.addItem (item ("Add Section at Playhead", "Ctrl+M", [this] { arrangement.addSectionAt (engine.getPositionSec()); }));
        m.addItem (item ("Previous Section", "[", [this] { arrangement.jumpSection (-1); }, ! engine.sections.empty()));
        m.addItem (item ("Next Section", "]", [this] { arrangement.jumpSection (1); }, ! engine.sections.empty()));
        m.addSeparator();

        const juce::String name = current >= 0 ? engine.sections[(size_t) current].name : juce::String ("section");
        m.addSectionHeader (current >= 0 ? "At the playhead: " + name : "Move the playhead into a section to edit it");
        m.addItem (item ("Rename " + name + "...", {}, [this, current] { arrangement.renameSection (current); }, current >= 0));
        m.addItem (item ("Loop " + name, {}, [this, current]
        {
            const auto sec = engine.sections[(size_t) current];
            engine.loopStartSec = sec.startSec;
            engine.loopEndSec = sec.endSec;
            engine.loopEnabled = true;
            arrangement.repaint();
        }, current >= 0));
        m.addItem (item ("Duplicate " + name, {}, [this, current] { engine.duplicateSection (current); }, current >= 0));
        m.addItem (item ("Delete " + name + " and its audio", {}, [this, current] { engine.deleteSectionWithAudio (current); }, current >= 0));
        m.addItem (item ("Remove " + name + " marker", {}, [this, current] { engine.removeSection (current); }, current >= 0));
        m.addSeparator();

        juce::PopupMenu quick;
        const juce::StringArray layouts { "Pop: Intro - Verse - Chorus - Verse - Chorus - Bridge - Chorus - Outro",
                                          "Short: Intro - Verse - Chorus - Outro",
                                          "EDM: Intro - Build - Drop - Break - Build - Drop - Outro" };
        for (int i = 0; i < layouts.size(); ++i)
            quick.addItem (layouts[i], [this, i]
            {
                const juce::StringArray names = i == 0 ? juce::StringArray { "Intro", "Verse 1", "Chorus", "Verse 2", "Chorus", "Bridge", "Chorus", "Outro" }
                                              : i == 1 ? juce::StringArray { "Intro", "Verse", "Chorus", "Outro" }
                                                       : juce::StringArray { "Intro", "Build", "Drop", "Break", "Build", "Drop", "Outro" };
                const juce::Array<int> bars = i == 0 ? juce::Array<int> { 4, 8, 8, 8, 8, 4, 8, 4 }
                                            : i == 1 ? juce::Array<int> { 4, 8, 8, 4 }
                                                     : juce::Array<int> { 8, 8, 16, 8, 8, 16, 8 };
                const double bar = 4.0 * 60.0 / engine.bpm.load();
                engine.beginClipEdit();
                engine.sections.clear();
                double t = 0.0;
                for (int k = 0; k < names.size(); ++k)
                {
                    engine.addSection (names[k], t, t + bars[k] * bar);
                    t += bars[k] * bar;
                }
                engine.endClipEdit ("Song layout");
                statusBar.setMessage ("Added a song layout - drag the section edges to fit your song");
            });
        m.addSubMenu ("Insert song layout (replaces sections)", quick);
        m.addItem (item ("Clear all sections", {}, [this]
        {
            engine.beginClipEdit();
            engine.sections.clear();
            engine.endClipEdit ("Clear sections");
            engine.sendChangeMessage();
        }, ! engine.sections.empty()));
    }
    else if (index == 4)
    {
        m.addItem (item ("Browser", "B", [this] { toggleBrowser(); }).setTicked (browserVisible));
        m.addItem (item ("Bottom panel", "M", [this] { toggleMixer(); }).setTicked (mixerVisible));
        m.addItem (item ("Mixer", {}, [this] { showBottomPage (0); }).setTicked (mixerVisible && bottomPanel.getCurrentPage() == 0));
        m.addItem (item ("Recorder", {}, [this] { showBottomPage (1); }).setTicked (mixerVisible && bottomPanel.getCurrentPage() == 1));
        m.addItem (item ("Inputs / Outputs (I/O)", {}, [this] { showBottomPage (2); }).setTicked (mixerVisible && bottomPanel.getCurrentPage() == 2));
        m.addSeparator();
        m.addItem (item ("Loop", "L", [this] { toggleLoop(); }).setTicked (engine.loopEnabled.load()));
        m.addItem (item ("Snap to Grid", {}, [this] { arrangement.snapToGrid = ! arrangement.snapToGrid; }).setTicked (arrangement.snapToGrid));
        m.addItem (item ("Metronome", {}, [this] { engine.metronomeOn = ! engine.metronomeOn.load(); }).setTicked (engine.metronomeOn.load()));
    }
    else if (index == 5)
    {
        m.addItem (item ("Settings...", "Ctrl+,", [this] { showSettings(); }));
        m.addItem (item ("Audio / ASIO Settings...", {}, [this] { showSettings (1); }));
        m.addItem (item ("Recording Settings...", {}, [this] { showSettings (2); }));
        m.addItem (item ("Plugin Manager (scan VST3)...", {}, [this] { showPluginManager(); }));
    }
    else if (index == 6)
    {
        m.addItem ("Keyboard Shortcuts", [this]
        {
            showMessage ("Keyboard shortcuts",
                         "Space  Play / Pause        R  Record        Home  Go to start\n"
                         "L  Loop on/off        B  Browser        M  Bottom panel (Mixer / Recorder / I/O)\n"
                         "[  ]  Previous / next section        Ctrl+M  Add section at playhead\n"
                         "S  Split clip at playhead        Ctrl+D  Duplicate clip        Delete  Delete clip\n"
                         "Ctrl+Z  Undo        Ctrl+Y  Redo\n"
                         "Ctrl+T  Add track        Ctrl+I  Import        Ctrl+E  Export mix / stems\n"
                         "Ctrl+S  Save        Ctrl+O  Open        Ctrl+N  New\n\n"
                         "Clips: drag to move, drag edges to trim, drag the white dots to fade, right-click for gain & more.\n"
                         "Loop: drag in the thin strip under the ruler; double-click it to switch loop on/off.\n"
                         "Alt while dragging = no snap.  Ctrl+wheel = zoom.  Shift+wheel = scroll.", false);
        });
        m.addItem ("About", [this]
        {
            showMessage (Rakam::appName, Rakam::appName + " " + JUCE_APPLICATION_VERSION_STRING
                                         + "\nMultitrack recording, VST3 mixing, mix & stem export, AI stem separation.\n"
                                           "Built with JUCE (AGPLv3). AI separation by Demucs (MIT).", false);
        });
    }

    return m;
}

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    const auto mods = key.getModifiers();
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());

    if (mods.isCommandDown())
    {
        if (key.getKeyCode() == 'S' && mods.isShiftDown()) { saveProjectAs(); return true; }
        switch (juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode()))
        {
            case 'Z': if (mods.isShiftDown()) redo(); else undo(); return true;
            case 'Y': redo(); return true;
            case 'D': arrangement.duplicateSelectedClip(); return true;
            case 'M': arrangement.addSectionAt (engine.getPositionSec()); return true;
            case 'S': saveProject(); return true;
            case ',': showSettings(); return true;
            case 'O': openProjectDialog(); return true;
            case 'N': newProject(); return true;
            case 'E': showExportDialog(); return true;
            case 'T': addTrack(); return true;
            case 'I': importAudioDialog (arrangement.getSelectedTrack(), engine.getPositionSec()); return true;
            default: break;
        }
        return false;
    }

    if (key == juce::KeyPress::spaceKey)                          { engine.togglePlay(); return true; }
    if (key == juce::KeyPress::homeKey)                           { engine.setPositionSec (0.0); return true; }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { arrangement.deleteSelectedClip(); return true; }
    if (c == 'r')                                                  { engine.isRecording() ? engine.stop() : startRecording(); return true; }
    if (c == 's')                                                  { arrangement.splitSelectedClipAtPlayhead(); return true; }
    if (c == 'l')                                                  { toggleLoop(); return true; }
    if (c == 'b')                                                  { toggleBrowser(); return true; }
    if (c == 'm')                                                  { toggleMixer(); return true; }
    if (c == '[')                                                  { arrangement.jumpSection (-1); return true; }
    if (c == ']')                                                  { arrangement.jumpSection (1); return true; }

    return false;
}

//==============================================================================
void MainComponent::addTrack()
{
    auto* t = engine.addTrack();
    arrangement.select (t, nullptr);
}

void MainComponent::startRecording()
{
    const auto error = engine.startRecording (Settings::getInt (settings, Settings::countInBars, 0));
    if (error.isNotEmpty())
        showMessage ("Record", error);
}

void MainComponent::confirmDiscard (std::function<void()> proceed)
{
    if (! engine.hasUnsavedChanges())
    {
        proceed();
        return;
    }

    juce::Component::SafePointer<MainComponent> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsYesNoCancel (juce::MessageBoxIconType::QuestionIcon,
                                                                                   "Unsaved changes",
                                                                                   "Save changes to this project first?",
                                                                                   "Save", "Don't Save", "Cancel"),
                                  [safe, proceed] (int result)
                                  {
                                      if (safe == nullptr)
                                          return;
                                      if (result == 1)
                                          safe->saveProject ([proceed] (bool saved) { if (saved) proceed(); });
                                      else if (result == 2)
                                          proceed();
                                  });
}

void MainComponent::newProject()
{
    confirmDiscard ([this]
    {
        engine.clearProject();
        currentFile = juce::File();
        updateTitle();
    });
}

void MainComponent::openFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    juce::File projectFile = file;

    if (file.hasFileExtension ("zip"))
    {
        juce::String error;
        projectFile = ProjectIO::extractPackage (file, error);
        if (projectFile == juce::File())
        {
            showMessage ("Open package", error);
            return;
        }
    }

    juce::MouseCursor::showWaitCursor();
    pluginWindows.clear();
    juce::String error;
    juce::StringArray warnings;
    missingClips.clear();
    const bool ok = ProjectIO::load (engine, projectFile, error, warnings, &missingClips);
    juce::MouseCursor::hideWaitCursor();

    if (! ok)
    {
        showMessage ("Open project", error);
        return;
    }

    currentFile = projectFile;
    addToRecent (projectFile);
    arrangement.select (nullptr, nullptr);
    updateTitle();

    if (! missingClips.empty())
    {
        juce::Component::SafePointer<MainComponent> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("Missing audio files")
                                          .withMessage (juce::String ((int) missingClips.size()) + " audio file(s) couldn't be found, e.g.\n"
                                                        + missingClips.front().originalPath
                                                        + "\n\nIf you moved them, choose the folder they are in now.")
                                          .withButton ("Find files...")
                                          .withButton ("Later"),
                                      [safe] (int r) { if (safe != nullptr && r == 1) safe->findMissingFiles(); });
    }
    else if (! warnings.isEmpty())
    {
        showMessage ("Opened with warnings", warnings.joinIntoString ("\n"));
    }
}

void MainComponent::openProjectDialog()
{
    confirmDiscard ([this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Open project or package", getDefaultProjectFolder(), "*" + Rakam::projectExt + ";*.zip");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { openFile (fc.getResult()); });
    });
}

void MainComponent::saveProject (std::function<void (bool)> after)
{
    if (! currentFile.existsAsFile())
    {
        saveProjectAs (std::move (after));
        return;
    }

    ProjectIO::SaveOptions o;
    o.copyExternalAudio = Settings::getBool (settings, Settings::copyAudioOnSave, true);
    o.backupsToKeep = Settings::getInt (settings, Settings::backupCount, 5);

    juce::String error;
    const bool ok = ProjectIO::save (engine, currentFile, error, o);
    if (! ok)
        showMessage ("Save", error);
    else
    {
        addToRecent (currentFile);
        statusBar.setMessage ("Saved " + currentFile.getFileName());
    }

    updateTitle();
    if (after)
        after (ok);
}

void MainComponent::saveProjectAs (std::function<void (bool)> after)
{
    auto start = currentFile.existsAsFile() ? currentFile : getDefaultProjectFolder().getChildFile ("My Song" + Rakam::projectExt);
    chooser = std::make_unique<juce::FileChooser> ("Save project", start, "*" + Rakam::projectExt);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, after] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file == juce::File())
                              {
                                  if (after) after (false);
                                  return;
                              }

                              file = file.withFileExtension (Rakam::projectExt);

                              // Saving straight into the Projects folder? Give the song its own folder (keeps Audio/ and Backups/ together)
                              if (file.getParentDirectory() == getDefaultProjectFolder())
                              {
                                  auto folder = file.getParentDirectory().getChildFile (file.getFileNameWithoutExtension());
                                  folder.createDirectory();
                                  file = folder.getChildFile (file.getFileName());
                              }

                              currentFile = file;
                              saveProject (after);
                          });
}

void MainComponent::collectAndZip()
{
    if (engine.getNumTracks() == 0)
    {
        showMessage ("Collect & Zip", "The project is empty.");
        return;
    }

    const auto name = currentFile.existsAsFile() ? currentFile.getFileNameWithoutExtension() : juce::String ("My Song");
    chooser = std::make_unique<juce::FileChooser> ("Save project package", getDefaultProjectFolder().getChildFile (name + ".zip"), "*.zip");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult() == juce::File())
                                  return;

                              engine.stop();
                              auto zip = fc.getResult().withFileExtension ("zip");

                              juce::MouseCursor::showWaitCursor();
                              juce::String error;
                              const bool ok = ProjectIO::collectAndZip (engine, zip, error);
                              juce::MouseCursor::hideWaitCursor();

                              if (! ok)
                              {
                                  showMessage ("Collect & Zip", error);
                                  return;
                              }

                              juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                                                .withIconType (juce::MessageBoxIconType::InfoIcon)
                                                                .withTitle ("Package ready")
                                                                .withMessage ("Saved " + zip.getFileName() + " with all audio files.\n"
                                                                              "Open it on any PC with File > Open.")
                                                                .withButton ("Show file")
                                                                .withButton ("OK"),
                                                            [zip] (int r) { if (r == 1) zip.revealToUser(); });
                          });
}

void MainComponent::importAudioDialog (Track* track, double atSec)
{
    chooser = std::make_unique<juce::FileChooser> ("Import audio", Rakam::getUserFolder(),
                                                   engine.getFormatManager().getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, track, atSec] (const juce::FileChooser& fc)
                          {
                              auto* target = engine.indexOf (track) >= 0 ? track : nullptr;
                              juce::StringArray errors;
                              juce::MouseCursor::showWaitCursor();

                              for (auto& file : fc.getResults())
                              {
                                  auto* t = target != nullptr ? target : engine.addTrack (file.getFileNameWithoutExtension());
                                  target = nullptr;
                                  juce::String error;
                                  if (engine.importFile (file, *t, atSec, error) == nullptr)
                                      errors.add (error);
                              }

                              juce::MouseCursor::hideWaitCursor();
                              if (! errors.isEmpty())
                                  showMessage ("Import", errors.joinIntoString ("\n"));
                          });
}

//==============================================================================
void MainComponent::showExportDialog()
{
    if (engine.getProjectLengthSec() <= 0.0)
    {
        showMessage ("Export", "There's nothing to export yet. Record or import some audio first.");
        return;
    }

    auto savedFolder = juce::File (settings.getValue ("exportFolder"));
    auto folder = savedFolder.isDirectory() ? savedFolder : Rakam::getUserFolder().getChildFile ("Exports");
    folder.createDirectory();

    const auto name = currentFile.existsAsFile() ? currentFile.getFileNameWithoutExtension() : juce::String ("My Song");

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new ExportDialog (engine, folder, name));
    o.dialogTitle = "Export Mix / Stems";
    o.dialogBackgroundColour = panel;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.componentToCentreAround = this;
    o.launchAsync();
}

void MainComponent::showStemSeparator (const juce::File& input, Track* sourceTrack, const AudioClip* sourceClip)
{
    const auto outFolder = (engine.getRecordingFolder().getParentDirectory()).getChildFile ("Stems");

    // Where to place the stems: aligned under the source clip, or at the start of the song
    double start = 0.0, offset = 0.0, length = -1.0;
    if (sourceClip != nullptr)
    {
        start  = sourceClip->startSec;
        offset = sourceClip->offsetSec;
        length = sourceClip->lengthSec;
    }

    juce::Component::SafePointer<MainComponent> safe (this);

    auto onReady = [safe, sourceTrack, start, offset, length] (const juce::Array<juce::File>& stems, const juce::File& source)
    {
        if (safe == nullptr)
            return;

        auto& eng = safe->engine;
        juce::StringArray errors;

        for (auto& stem : stems)
        {
            auto* t = eng.addTrack (source.getFileNameWithoutExtension() + " - " + stem.getFileNameWithoutExtension());
            juce::String error;

            if (auto clip = eng.loadClip (stem, error))
            {
                clip->startSec  = start;
                clip->offsetSec = juce::jlimit (0.0, clip->sourceLengthSec, offset);
                clip->lengthSec = length > 0.0 ? juce::jmin (length, clip->sourceLengthSec - clip->offsetSec)
                                               : clip->sourceLengthSec - clip->offsetSec;
                eng.addClip (*t, clip);
            }
            else
            {
                errors.add (error);
            }
        }

        // Mute the original so you hear the stems
        if (sourceTrack != nullptr && eng.indexOf (sourceTrack) >= 0)
        {
            sourceTrack->mute = true;
            eng.sendChangeMessage();
        }

        if (! errors.isEmpty())
            safe->showMessage ("Stems", errors.joinIntoString ("\n"));
    };

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new StemSeparatorDialog (settings, input, outFolder, onReady));
    o.dialogTitle = "Separate Stems (AI)";
    o.dialogBackgroundColour = panel;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    o.launchAsync();
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager(),
                                                             0, 64,   // inputs
                                                             2, 64,   // outputs
                                                             false, false, true, false);
    selector->setSize (560, 520);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio / ASIO Settings";
    o.dialogBackgroundColour = panel;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    o.launchAsync();
}

void MainComponent::showPluginManager()
{
    if (pluginManagerWindow != nullptr)
    {
        pluginManagerWindow->toFront (true);
        return;
    }

    struct ManagerWindow : public juce::DocumentWindow
    {
        ManagerWindow (MainComponent& o) : juce::DocumentWindow ("Plugin Manager", panel, closeButton), owner (o) {}
        void closeButtonPressed() override
        {
            owner.engine.saveSettings();
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (&owner)]
            {
                if (safe != nullptr)
                    safe->pluginManagerWindow.reset();
            });
        }
        MainComponent& owner;
    };

    auto deadMansPedal = settings.getFile().getSiblingFile ("PluginScanCrashes.txt");
    auto* list = new juce::PluginListComponent (engine.getPluginFormatManager(), engine.getKnownPlugins(),
                                                deadMansPedal, &settings, false);
    list->setSize (720, 520);

    auto window = std::make_unique<ManagerWindow> (*this);
    window->setUsingNativeTitleBar (true);
    window->setContentOwned (list, true);
    window->setResizable (true, false);
    window->centreWithSize (720, 520);
    window->setVisible (true);
    pluginManagerWindow = std::move (window);

    if (engine.getKnownPlugins().getNumTypes() == 0)
        showMessage ("Plugin Manager", "Press \"Options\" > \"Scan for new or updated VST3 plug-ins\" at the bottom of this window.\n"
                                       "VST3 plugins are usually in C:\\Program Files\\Common Files\\VST3.", false);
}

void MainComponent::showAddPluginMenu (Track& track, juce::Component* anchor)
{
    juce::Array<juce::PluginDescription> effects;
    for (auto& d : engine.getKnownPlugins().getTypes())
        if (! d.isInstrument)
            effects.add (d);

    auto* t = &track;
    juce::PopupMenu menu;
    menu.addSectionHeader ("Rakam built-in");

    for (auto& d : BuiltInPluginFormat::getAllDescriptions())
        menu.addItem (d.name, [this, t, d]
        {
            if (t->isMaster || engine.indexOf (t) >= 0)
                addEffect (*t, d);
        });

    menu.addSeparator();
    juce::PopupMenu vst;
    if (effects.isEmpty())
        vst.addItem ("None yet - open Plugin Manager to scan", [this] { showPluginManager(); });
    else
        juce::KnownPluginList::addToMenu (vst, effects, juce::KnownPluginList::sortByManufacturer);
    menu.addSubMenu ("VST3 plugins", vst);
    menu.addItem ("Plugin Manager...", [this] { showPluginManager(); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor),
                        [this, t, effects] (int result)
                        {
                            const int index = juce::KnownPluginList::getIndexChosenByMenu (effects, result);
                            if (index < 0)
                                return;

                            if (t->isMaster || engine.indexOf (t) >= 0)
                                addEffect (*t, effects.getReference (index));
                        });
}

void MainComponent::addEffect (Track& track, const juce::PluginDescription& desc)
{
    juce::MouseCursor::showWaitCursor();
    juce::String error;
    const bool ok = engine.addPlugin (track, desc, error);
    juce::MouseCursor::hideWaitCursor();

    if (! ok)
    {
        showMessage ("Plugin", error);
        return;
    }

    statusBar.setMessage ("Added " + desc.name + " to " + track.name);
    openPluginWindow (*track.plugins.back()->instance);
}

void MainComponent::toggleLoop()
{
    if (engine.loopEndSec.load() - engine.loopStartSec.load() < 0.1)
    {
        // No usable range yet: loop 4 bars from the playhead's bar
        const double bar = 4.0 * 60.0 / engine.bpm.load();
        const double start = std::floor (engine.getPositionSec() / bar) * bar;
        engine.loopStartSec = start;
        engine.loopEndSec = start + 4.0 * bar;
    }

    engine.loopEnabled = ! engine.loopEnabled.load();
    arrangement.repaint();
    for (auto* c : arrangement.getChildren())
        c->repaint();
}

void MainComponent::toggleBrowser()
{
    browserVisible = ! browserVisible;
    browser.setVisible (browserVisible);
    settings.setValue ("showBrowser", browserVisible);
    transport.setPanelStates (browserVisible, mixerVisible);
    resized();
}

void MainComponent::showBottomPage (int page)
{
    if (! mixerVisible)
        toggleMixer();
    bottomPanel.showPage (page);
}

void MainComponent::toggleMixer()
{
    mixerVisible = ! mixerVisible;
    bottomPanel.setVisible (mixerVisible);
    divider->setVisible (mixerVisible);
    settings.setValue ("showMixer", mixerVisible);
    transport.setPanelStates (browserVisible, mixerVisible);
    resized();
}

void MainComponent::undo()
{
    const auto name = engine.getUndoManager().getUndoDescription();
    if (engine.undo())
        statusBar.setMessage ("Undo: " + name);
}

void MainComponent::redo()
{
    const auto name = engine.getUndoManager().getRedoDescription();
    if (engine.redo())
        statusBar.setMessage ("Redo: " + name);
}

void MainComponent::openPluginWindow (juce::AudioPluginInstance& plugin)
{
    for (auto* w : pluginWindows)
    {
        if (&w->plugin == &plugin)
        {
            w->toFront (true);
            return;
        }
    }

    pluginWindows.add (new PluginWindow (plugin, [this] (PluginWindow* w)
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), w]
        {
            if (safe != nullptr)
                safe->pluginWindows.removeObject (w);
        });
    }));
}

void MainComponent::closePluginWindow (juce::AudioProcessor* plugin)
{
    for (int i = pluginWindows.size(); --i >= 0;)
        if (&pluginWindows[i]->plugin == plugin)
            pluginWindows.remove (i);
}
