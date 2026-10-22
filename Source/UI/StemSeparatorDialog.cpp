#include "UI/StemSeparatorDialog.h"

using namespace Rakam::Colours;

StemSeparatorDialog::StemSeparatorDialog (juce::PropertiesFile& s, const juce::File& input,
                                          const juce::File& outFolder, StemsReady ready)
    : settings (s), inputFile (input), outputFolder (outFolder), onStemsReady (std::move (ready))
{
    filePath.setText (inputFile.existsAsFile() ? inputFile.getFullPathName() : juce::String ("(choose a song)"), juce::dontSendNotification);
    filePath.setColour (juce::Label::textColourId, textDim);
    filePath.setMinimumHorizontalScale (0.6f);

    const auto& models = StemSeparator::getModels();
    for (int i = 0; i < models.size(); ++i)
        modelBox.addItem (models[i].label, i + 1);
    modelBox.setSelectedId (settings.getIntValue ("stemModel", 1), juce::dontSendNotification);

   #if JUCE_WINDOWS
    const juce::String defaultPython = "py -3";
   #else
    const juce::String defaultPython = "python3";
   #endif
    pythonEditor.setText (settings.getValue ("pythonCommand", defaultPython), false);
    pythonEditor.setTooltip ("The command that starts Python on this PC, e.g. \"py -3\" or \"python\" or a full path to python.exe");

    hint.setText ("First time? Install Python 3.11 or 3.12 from python.org, then press \"Install Demucs\" (downloads ~2 GB once). "
                  "Separation runs on your PC - a 4-minute song takes about 1-5 minutes.",
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, textDim);
    hint.setJustificationType (juce::Justification::topLeft);
    hint.setFont (juce::FontOptions (12.5f));

    log.setMultiLine (true);
    log.setReadOnly (true);
    log.setScrollbarsShown (true);
    log.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    log.setColour (juce::TextEditor::backgroundColourId, background);

    importToggle.setToggleState (true, juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &fileLabel, &filePath, &modelLabel, &pythonLabel, &hint, &modelBox,
                                                             &pythonEditor, &log, &browseButton, &installButton, &separateButton,
                                                             &cancelButton, &closeButton, &importToggle })
        addAndMakeVisible (c);

    browseButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a song to separate", inputFile.getParentDirectory(),
                                                       "*.wav;*.mp3;*.flac;*.aiff;*.aif;*.ogg;*.m4a");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                  {
                                      inputFile = fc.getResult();
                                      filePath.setText (inputFile.getFullPathName(), juce::dontSendNotification);
                                  }
                              });
    };

    separateButton.setColour (juce::TextButton::buttonColourId, accent);
    separateButton.onClick = [this] { run (false); };
    installButton.onClick  = [this] { run (true); };
    cancelButton.onClick   = [this] { separator.cancel(); };
    closeButton.onClick    = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    };

    juce::Component::SafePointer<StemSeparatorDialog> safe (this);

    separator.onLog = [safe] (const juce::String& text)
    {
        if (safe != nullptr)
            safe->appendLog (text);
    };

    separator.onFinished = [safe] (bool ok, juce::Array<juce::File> stems, const juce::String& error)
    {
        if (safe == nullptr)
            return;

        safe->setBusy (false);

        if (! ok)
        {
            safe->appendLog ("\nERROR: " + error + "\n");
            return;
        }

        if (stems.isEmpty())
        {
            safe->appendLog ("\nDemucs is installed and ready. Now press \"Separate\".\n");
            return;
        }

        safe->appendLog ("\nDone! " + juce::String (stems.size()) + " stems saved in:\n"
                         + stems.getFirst().getParentDirectory().getFullPathName() + "\n");

        if (safe->importToggle.getToggleState() && safe->onStemsReady)
            safe->onStemsReady (stems, safe->inputFile);
    };

    setBusy (false);
    setSize (640, 480);
}

StemSeparatorDialog::~StemSeparatorDialog()
{
    separator.onLog = nullptr;
    separator.onFinished = nullptr;
    separator.cancel();
}

void StemSeparatorDialog::run (bool installOnly)
{
    if (! installOnly && ! inputFile.existsAsFile())
    {
        appendLog ("Choose a song first.\n");
        return;
    }

    settings.setValue ("stemModel", modelBox.getSelectedId());
    settings.setValue ("pythonCommand", pythonEditor.getText().trim());

    StemSeparator::Job job;
    job.input        = inputFile;
    job.outputFolder = outputFolder;
    job.python       = pythonEditor.getText().trim();
    job.modelIndex   = modelBox.getSelectedId() - 1;
    job.installOnly  = installOnly;

    log.clear();
    appendLog (installOnly ? "Installing Demucs and PyTorch (this can take several minutes)...\n"
                           : "Separating " + inputFile.getFileName() + " - the first run also downloads the AI model.\n");
    setBusy (true);
    separator.start (job);
}

void StemSeparatorDialog::appendLog (const juce::String& text)
{
    // Progress bars print '\r' to redraw a line; show them as new lines instead
    log.moveCaretToEnd();
    log.insertTextAtCaret (text.replace ("\r\n", "\n").replace ("\r", "\n"));

    if (log.getTotalNumChars() > 60000)
    {
        auto all = log.getText();
        log.setText (all.substring (all.length() - 40000), false);
        log.moveCaretToEnd();
    }
}

void StemSeparatorDialog::setBusy (bool busy)
{
    separateButton.setEnabled (! busy);
    installButton.setEnabled (! busy);
    browseButton.setEnabled (! busy);
    modelBox.setEnabled (! busy);
    pythonEditor.setEnabled (! busy);
    cancelButton.setEnabled (busy);
}

void StemSeparatorDialog::paint (juce::Graphics& g)
{
    g.fillAll (panel);
}

void StemSeparatorDialog::resized()
{
    auto r = getLocalBounds().reduced (16);

    auto row = [&r] (int h = 28) { auto x = r.removeFromTop (h); r.removeFromTop (8); return x; };

    {
        auto x = row();
        fileLabel.setBounds (x.removeFromLeft (64));
        browseButton.setBounds (x.removeFromRight (90));
        x.removeFromRight (6);
        filePath.setBounds (x);
    }
    {
        auto x = row();
        modelLabel.setBounds (x.removeFromLeft (64));
        modelBox.setBounds (x);
    }
    {
        auto x = row();
        pythonLabel.setBounds (x.removeFromLeft (64));
        installButton.setBounds (x.removeFromRight (130));
        x.removeFromRight (6);
        pythonEditor.setBounds (x);
    }

    hint.setBounds (row (36));

    auto buttons = r.removeFromBottom (32);
    closeButton.setBounds (buttons.removeFromRight (80));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (80));
    buttons.removeFromRight (8);
    separateButton.setBounds (buttons.removeFromRight (120));
    importToggle.setBounds (buttons);

    r.removeFromBottom (8);
    log.setBounds (r);
}
