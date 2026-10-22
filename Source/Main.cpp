#include "UI/MainComponent.h"
#include "UI/LookAndFeel.h"

class RakamStudioApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = Rakam::appName;
        options.filenameSuffix      = ".settings";
        options.folderName          = Rakam::appName;
        options.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (options);

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        mainWindow = std::make_unique<MainWindow> (*properties.getUserSettings());

        const auto arg = commandLine.unquoted().trim();
        juce::MessageManager::callAsync ([this, arg]
        {
            if (mainWindow != nullptr)
                mainWindow->content->handleStartup (arg.isNotEmpty() ? juce::File (arg) : juce::File());
        });
    }

    void shutdown() override
    {
        mainWindow.reset();
        properties.saveIfNeeded();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (mainWindow == nullptr)
        {
            quit();
            return;
        }

        mainWindow->content->confirmDiscard ([] { juce::JUCEApplication::quit(); });
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        const auto arg = commandLine.unquoted().trim();
        if (mainWindow != nullptr && arg.isNotEmpty())
            mainWindow->content->confirmDiscard ([this, arg] { mainWindow->content->openFile (juce::File (arg)); });
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (juce::PropertiesFile& settings)
            : juce::DocumentWindow (Rakam::appName, Rakam::Colours::background, juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            content = new MainComponent (settings);
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (900, 600, 10000, 10000);

            if (! restoreWindowStateFromString (settings.getValue ("windowState")))
                centreWithSize (getWidth(), getHeight());

            setVisible (true);
            content->grabKeyboardFocus();
            settingsFile = &settings;
        }

        ~MainWindow() override
        {
            settingsFile->setValue ("windowState", getWindowStateAsString());
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        MainComponent* content = nullptr;

    private:
        juce::PropertiesFile* settingsFile = nullptr;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    juce::ApplicationProperties properties;
    RakamLookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (RakamStudioApplication)
