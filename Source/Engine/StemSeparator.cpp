#include "Engine/StemSeparator.h"

const juce::Array<StemSeparator::Model>& StemSeparator::getModels()
{
    static const juce::Array<Model> models {
        Model { "htdemucs",    "4 stems: vocals, drums, bass, other",     false },
        Model { "htdemucs_ft", "4 stems, high quality (slower)",          false },
        Model { "htdemucs_6s", "6 stems: + guitar, piano",                false },
        Model { "htdemucs",    "2 stems: vocals + instrumental (karaoke)", true }
    };
    return models;
}

StemSeparator::StemSeparator() : juce::Thread ("Stem separation") {}

StemSeparator::~StemSeparator()
{
    cancel();
    stopThread (5000);
}

void StemSeparator::start (const Job& j)
{
    if (isThreadRunning())
        return;

    job = j;
    startThread();
}

void StemSeparator::cancel()
{
    signalThreadShouldExit();
    const juce::ScopedLock sl (processLock);
    if (process != nullptr)
        process->kill();
}

void StemSeparator::log (const juce::String& text)
{
    juce::MessageManager::callAsync ([cb = onLog, text] { if (cb) cb (text); });
}

void StemSeparator::finish (bool ok, juce::Array<juce::File> stems, const juce::String& error)
{
    juce::MessageManager::callAsync ([cb = onFinished, ok, stems, error] { if (cb) cb (ok, stems, error); });
}

void StemSeparator::run()
{
    juce::StringArray args;
    args.addTokens (job.python, true);   // allows "py -3.11" as well as "python"
    args.trim();
    args.removeEmptyStrings();

    if (args.isEmpty())
        args.add ("python");

    const auto& models = getModels();
    const auto model = models[juce::jlimit (0, models.size() - 1, job.modelIndex)];

    if (job.installOnly)
    {
        // Pinned torch/torchaudio: newer torchaudio releases changed how audio files are saved, which breaks Demucs.
        args.addArray ({ "-m", "pip", "install", "--upgrade", "demucs", "soundfile",
                         "torch==2.8.*", "torchaudio==2.8.*" });
    }
    else
    {
        job.outputFolder.createDirectory();
        args.addArray ({ "-u", "-m", "demucs", "-n", model.id, "-o", job.outputFolder.getFullPathName() });
        if (model.twoStems)
            args.addArray ({ "--two-stems", "vocals" });
        args.add (job.input.getFullPathName());
    }

    log ("> " + args.joinIntoString (" ") + "\n");

    {
        const juce::ScopedLock sl (processLock);
        process = std::make_unique<juce::ChildProcess>();

        if (! process->start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        {
            process.reset();
            finish (false, {}, "Couldn't start \"" + args[0] + "\". Install Python from python.org "
                               "(tick \"Add python.exe to PATH\"), then try again.");
            return;
        }
    }

    char buffer[1024];
    bool gotOutput = false;
    while (! threadShouldExit())
    {
        const int n = process->readProcessOutput (buffer, (int) sizeof (buffer) - 1);

        if (n > 0)
        {
            buffer[n] = 0;
            gotOutput = true;
            log (juce::String::fromUTF8 (buffer, n));
        }
        else if (! process->isRunning())
        {
            break;
        }
        else
        {
            wait (50);
        }
    }

    const auto exitCode = process->getExitCode();
    {
        const juce::ScopedLock sl (processLock);
        process.reset();
    }

    if (threadShouldExit())
    {
        finish (false, {}, "Cancelled.");
        return;
    }

    if (exitCode != 0 && ! gotOutput)
    {
        finish (false, {}, "\"" + args[0] + "\" didn't run. Check the Python command: install Python from python.org "
                           "(tick \"Add python.exe to PATH\") and use \"py -3\" or \"python\".");
        return;
    }

    if (exitCode != 0)
    {
        finish (false, {}, job.installOnly
                             ? "pip failed (exit code " + juce::String (exitCode) + "). See the log above."
                             : "Demucs failed (exit code " + juce::String (exitCode) + "). If it says \"No module named demucs\", "
                               "press \"Install Demucs\" first.");
        return;
    }

    if (job.installOnly)
    {
        finish (true, {}, {});
        return;
    }

    // Demucs writes to <output>/<model>/<song name>/<stem>.wav
    auto stemFolder = job.outputFolder.getChildFile (model.id).getChildFile (job.input.getFileNameWithoutExtension());
    auto stems = stemFolder.findChildFiles (juce::File::findFiles, false, "*.wav");

    if (stems.isEmpty())
    {
        finish (false, {}, "Demucs finished but no stems were found in " + stemFolder.getFullPathName());
        return;
    }

    stems.sort();
    finish (true, stems, {});
}
