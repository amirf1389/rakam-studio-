#pragma once

#include "Engine/AudioEngine.h"
#include "UI/TransportBar.h"
#include "UI/ArrangementView.h"
#include "UI/MixerView.h"
#include "UI/BrowserPanel.h"
#include "UI/StatusBar.h"
#include "UI/RecorderPanel.h"
#include "UI/IOPanel.h"
#include "UI/BottomPanel.h"
#include "UI/ProjectDialogs.h"
#include "Engine/ProjectIO.h"

class PluginWindow;

/** The whole DAW window: menu bar, transport, timeline and mixer. */
class MainComponent : public juce::Component,
                      public juce::MenuBarModel,
                      public juce::DragAndDropContainer,
                      public juce::KeyListener,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit MainComponent (juce::PropertiesFile& settings);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Opens a project or package passed on the command line / double-clicked in Explorer. */
    void openFile (const juce::File& file);

    /** Called once the window is visible: crash recovery, command-line file or the start screen. */
    void handleStartup (const juce::File& fileFromCommandLine);

    /** Asks to save unsaved changes, then calls proceed() unless the user cancels. */
    void confirmDiscard (std::function<void()> proceed);

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int, int) override {}

    // KeyListener
    bool keyPressed (const juce::KeyPress& key, juce::Component* origin) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    // Actions
    void newProject();
    void newFromTemplate (const StartScreen::Choice& choice);
    void showStartScreen();
    void saveCopy();
    void saveTemplateDialog();
    void revertToSaved();
    void showProjectInfo();
    void findMissingFiles();
    void showSettings (int tab = 0);
    void applySettings();
    void doAutosave();
    void addToRecent (const juce::File& f);
    void openProjectDialog();
    void saveProject (std::function<void (bool)> after = nullptr);
    void saveProjectAs (std::function<void (bool)> after = nullptr);
    void collectAndZip();
    void importAudioDialog (Track* track, double atSec);
    void showExportDialog();
    void showStemSeparator (const juce::File& input, Track* sourceTrack, const AudioClip* sourceClip);
    void showAudioSettings();
    void showPluginManager();
    void showAddPluginMenu (Track& track, juce::Component* anchor);
    void addEffect (Track& track, const juce::PluginDescription& desc);
    void toggleLoop();
    void toggleBrowser();
    void toggleMixer();
    void showBottomPage (int page);   // 0 mixer, 1 recorder, 2 I/O
    void undo();
    void redo();
    void openPluginWindow (juce::AudioPluginInstance& plugin);
    void closePluginWindow (juce::AudioProcessor* plugin);
    void startRecording();
    void addTrack();
    void showMessage (const juce::String& title, const juce::String& text, bool warning = true);
    void updateTitle();
    juce::File getDefaultProjectFolder() const;

    juce::PropertiesFile& settings;
    AudioEngine engine;

    juce::MenuBarComponent menuBar { this };
    TransportBar transport;
    ArrangementView arrangement;
    MixerView mixer;
    RecorderPanel recorderPanel;
    IOPanel ioPanel;
    BottomPanel bottomPanel;
    BrowserPanel browser;
    StatusBar statusBar;
    juce::StretchableLayoutManager layout;
    std::unique_ptr<juce::StretchableLayoutResizerBar> divider;
    juce::TooltipWindow tooltips { this, 600 };

    juce::File currentFile;
    juce::RecentlyOpenedFilesList recent;
    ProjectIO::MissingList missingClips;
    juce::uint32 lastAutosave = 0;
    bool mixerVisible = true, browserVisible = true;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::DocumentWindow> pluginManagerWindow;
    juce::OwnedArray<PluginWindow> pluginWindows;   // declared last: closed before the engine is destroyed

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
