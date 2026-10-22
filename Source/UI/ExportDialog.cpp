#include "UI/ExportDialog.h"
#include "UI/BackgroundTask.h"

using namespace Rakam::Colours;

ExportDialog::ExportDialog (AudioEngine& e, const juce::File& defaultFolder, const juce::String& defaultName)
    : engine (e), folder (defaultFolder)
{
    whatBox.addItem ("Full mix + all stems", 1);
    whatBox.addItem ("Full mix only", 2);
    whatBox.addItem ("Stems only (one file per track)", 3);
    whatBox.setSelectedId (engine.getSettings().getIntValue ("exportWhat", 1), juce::dontSendNotification);

    formatBox.addItem ("WAV 24-bit (recommended)", 1);
    formatBox.addItem ("WAV 16-bit (CD)", 2);
    formatBox.addItem ("WAV 32-bit float", 3);
    formatBox.addItem ("FLAC 24-bit (lossless, smaller)", 4);
    formatBox.addItem ("OGG Vorbis (small, for sharing)", 5);
    formatBox.addItem ("MP3 320 kbps (needs lame.exe, see Settings)", 6);
    formatBox.setSelectedId (engine.getSettings().getIntValue (Settings::exportFormat, 1), juce::dontSendNotification);

    rangeBox.addItem ("Whole song", 1);
    rangeBox.addItem ("Loop region only", 2);
    rangeBox.setSelectedId (1, juce::dontSendNotification);
    rangeBox.setItemEnabled (2, engine.loopEndSec.load() > engine.loopStartSec.load() + 0.05);

    normalizeToggle.setToggleState (engine.getSettings().getBoolValue (Settings::exportNormalize, false), juce::dontSendNotification);
    normalizeToggle.setTooltip ("Raises (or lowers) the mix so its loudest peak is -1 dB. Stems are left untouched so they still add up.");

    tailSlider.setSliderStyle (juce::Slider::IncDecButtons);
    tailSlider.setRange (0.0, 20.0, 0.5);
    tailSlider.setValue (2.0, juce::dontSendNotification);
    tailSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 50, 24);
    tailSlider.setTooltip ("Extra seconds after the last clip so reverb and delay tails aren't cut off");

    nameEditor.setText (defaultName, false);
    folderPath.setText (folder.getFullPathName(), juce::dontSendNotification);
    folderPath.setColour (juce::Label::textColourId, textDim);
    folderPath.setMinimumHorizontalScale (0.6f);

    skipEmpty.setToggleState (true, juce::dontSendNotification);
    skipMuted.setToggleState (false, juce::dontSendNotification);

    info.setColour (juce::Label::textColourId, textDim);
    info.setJustificationType (juce::Justification::topLeft);

    for (auto* c : std::initializer_list<juce::Component*> { &rangeLabel, &rangeBox, &normalizeToggle,
                                                             &whatLabel, &formatLabel, &tailLabel, &nameLabel, &folderLabel,
                                                             &folderPath, &info, &whatBox, &formatBox, &tailSlider, &nameEditor,
                                                             &browseButton, &exportButton, &cancelButton, &skipEmpty, &skipMuted })
        addAndMakeVisible (c);

    whatBox.onChange = [this] { updateInfo(); };
    formatBox.onChange = [this] { updateInfo(); };
    rangeBox.onChange = [this] { updateInfo(); };
    tailSlider.onValueChange = [this] { updateInfo(); };
    skipEmpty.onClick = [this] { updateInfo(); };
    skipMuted.onClick = [this] { updateInfo(); };

    browseButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose export folder", folder);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult() != juce::File())
                                  {
                                      folder = fc.getResult();
                                      folderPath.setText (folder.getFullPathName(), juce::dontSendNotification);
                                  }
                              });
    };

    exportButton.setColour (juce::TextButton::buttonColourId, accent);
    exportButton.onClick = [this] { startExport(); };
    cancelButton.onClick = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    };

    updateInfo();
    setSize (560, 430);
}

juce::String ExportDialog::getExtension() const
{
    switch (formatBox.getSelectedId())
    {
        case 4:  return ".flac";
        case 5:  return ".ogg";
        case 6:  return ".mp3";
        default: return ".wav";
    }
}

std::vector<AudioEngine::RenderOptions> ExportDialog::buildJobs() const
{
    std::vector<AudioEngine::RenderOptions> jobs;

    const bool loopOnly = rangeBox.getSelectedId() == 2;
    const double start = loopOnly ? engine.loopStartSec.load() : 0.0;
    const double end = loopOnly ? engine.loopEndSec.load() + tailSlider.getValue()
                                : engine.getProjectLengthSec() + tailSlider.getValue();
    const auto baseName = juce::File::createLegalFileName (nameEditor.getText().trim().isEmpty() ? "Mix" : nameEditor.getText().trim());
    const int what = whatBox.getSelectedId();

    AudioEngine::RenderOptions base;
    base.startSec = start;
    base.endSec = end;
    base.lameExecutable = juce::File (engine.getSettings().getValue (Settings::lamePath));

    switch (formatBox.getSelectedId())
    {
        case 2:  base.format = "WAV";  base.bitDepth = 16; break;
        case 3:  base.format = "WAV";  base.bitDepth = 32; base.floatingPoint = true; break;
        case 4:  base.format = "FLAC"; base.bitDepth = 24; break;
        case 5:  base.format = "OGG";  base.bitDepth = 16; break;
        case 6:  base.format = "MP3";  base.bitDepth = 16; break;
        default: base.format = "WAV";  base.bitDepth = 24; break;
    }

    if (what == 1 || what == 2)
    {
        auto job = base;
        job.trackIndex = -1;
        job.normalize = normalizeToggle.getToggleState();
        job.normalizeDb = -1.0f;
        job.file = folder.getChildFile (baseName + getExtension());
        jobs.push_back (job);
    }

    if (what == 1 || what == 3)
    {
        const auto stemFolder = folder.getChildFile (baseName + " - Stems");

        for (int i = 0; i < engine.getNumTracks(); ++i)
        {
            auto* t = engine.getTrack (i);
            if ((skipEmpty.getToggleState() && ! t->hasClips()) || (skipMuted.getToggleState() && t->mute))
                continue;

            auto job = base;
            job.trackIndex = i;
            job.file = stemFolder.getChildFile (juce::String (i + 1).paddedLeft ('0', 2) + " - "
                                                + juce::File::createLegalFileName (t->name) + getExtension());
            jobs.push_back (job);
        }
    }

    return jobs;
}

void ExportDialog::updateInfo()
{
    const auto jobs = buildJobs();
    const double len = jobs.empty() ? 0.0 : jobs.front().endSec - jobs.front().startSec;

    juce::String text;
    text << juce::String (jobs.size()) << " file(s), " << juce::String (len, 1) << " s each, "
         << juce::String (engine.getSampleRate(), 0) << " Hz stereo.\n"
         << "Stems include each track's effects and fader, all start at 0:00 and have the same length, "
         << "so they line up in any other DAW.";
    info.setText (text, juce::dontSendNotification);
    exportButton.setEnabled (! jobs.empty() && engine.getProjectLengthSec() > 0.0);
}

void ExportDialog::startExport()
{
    auto jobs = buildJobs();
    if (jobs.empty())
        return;

    engine.getSettings().setValue ("exportWhat", whatBox.getSelectedId());
    engine.getSettings().setValue (Settings::exportFormat, formatBox.getSelectedId());
    engine.getSettings().setValue (Settings::exportNormalize, normalizeToggle.getToggleState());

    if (formatBox.getSelectedId() == 6 && ! juce::File (engine.getSettings().getValue (Settings::lamePath)).existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MP3 export",
                                                "MP3 needs the free LAME encoder.\n\n1. Download it (Settings > Export has a link).\n"
                                                "2. Choose lame.exe in Settings > Export.\n\nOr pick WAV / FLAC / OGG for now.");
        return;
    }
    engine.getSettings().setValue ("exportFolder", folder.getFullPathName());

    for (auto& j : jobs)
        j.file.getParentDirectory().createDirectory();

    engine.stop();
    auto* eng = &engine;
    auto outFolder = folder;

    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState (0);   // deletes this dialog; nothing below may touch members

    BackgroundTask::launch ("Exporting", [eng, jobs] (BackgroundTask& task) -> juce::String
    {
        const auto count = (double) jobs.size();

        for (size_t i = 0; i < jobs.size(); ++i)
        {
            task.setStatusMessage ("Rendering " + jobs[i].file.getFileName() + "  (" + juce::String ((int) i + 1)
                                   + " of " + juce::String ((int) jobs.size()) + ")");
            juce::String error;
            const bool ok = eng->renderToFile (jobs[i], [&task, i, count] (double p)
            {
                task.setProgress (((double) i + p) / count);
                return ! task.shouldStop();
            }, error);

            if (! ok)
                return error;
        }

        return {};
    },
    [outFolder, n = (int) jobs.size()] (const juce::String& error, bool cancelled)
    {
        if (cancelled || error.isNotEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export",
                                                    cancelled ? juce::String ("Export cancelled.") : error);
            return;
        }

        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::InfoIcon)
                                          .withTitle ("Export finished")
                                          .withMessage (juce::String (n) + " file(s) written to:\n" + outFolder.getFullPathName())
                                          .withButton ("Open folder")
                                          .withButton ("OK"),
                                      [outFolder] (int result) { if (result == 1) outFolder.startAsProcess(); });
    });
}

void ExportDialog::paint (juce::Graphics& g)
{
    g.fillAll (panel);
}

void ExportDialog::resized()
{
    auto r = getLocalBounds().reduced (18);

    auto row = [&r] (int h = 28) { auto x = r.removeFromTop (h); r.removeFromTop (8); return x; };
    auto labelled = [&] (juce::Label& l, juce::Component& c, int h = 28)
    {
        auto x = row (h);
        l.setBounds (x.removeFromLeft (70));
        c.setBounds (x);
        return x;
    };

    labelled (whatLabel, whatBox);
    labelled (formatLabel, formatBox);
    labelled (rangeLabel, rangeBox);

    {
        auto x = row();
        tailLabel.setBounds (x.removeFromLeft (70));
        tailSlider.setBounds (x.removeFromLeft (130));
        x.removeFromLeft (16);
        skipEmpty.setBounds (x);
    }
    skipMuted.setBounds (row (22).withTrimmedLeft (216));
    normalizeToggle.setBounds (row (22).withTrimmedLeft (70));

    labelled (nameLabel, nameEditor);

    {
        auto x = row();
        folderLabel.setBounds (x.removeFromLeft (70));
        browseButton.setBounds (x.removeFromRight (90));
        x.removeFromRight (6);
        folderPath.setBounds (x);
    }

    auto buttons = r.removeFromBottom (32);
    exportButton.setBounds (buttons.removeFromRight (120));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (90));

    info.setBounds (r);
}
