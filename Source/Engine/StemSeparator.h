#pragma once

#include "Common.h"

/**
    AI stem separation using Demucs (Meta's open-source model), run as an
    external Python process so the DAW itself stays small.

    It splits a finished song into vocals / drums / bass / other (or 6 stems,
    or vocals + instrumental). The results are WAV files that the DAW imports
    as new tracks.

    One-time setup on the user's PC:  Python 3.10–3.12, then the "Install Demucs"
    button (runs pip for you).
*/
class StemSeparator : private juce::Thread
{
public:
    struct Model
    {
        juce::String id, label;
        bool twoStems = false;
    };

    static const juce::Array<Model>& getModels();

    struct Job
    {
        juce::File input;
        juce::File outputFolder;
        juce::String python = "python";
        int modelIndex = 0;
        bool installOnly = false;   // just runs "pip install demucs"
    };

    StemSeparator();
    ~StemSeparator() override;

    void start (const Job& job);
    void cancel();
    bool isBusy() const { return isThreadRunning(); }

    /** Both callbacks are delivered on the message thread. */
    std::function<void (const juce::String& text)> onLog;
    std::function<void (bool success, juce::Array<juce::File> stems, const juce::String& error)> onFinished;

private:
    void run() override;
    void log (const juce::String& text);
    void finish (bool ok, juce::Array<juce::File> stems, const juce::String& error);

    Job job;
    juce::CriticalSection processLock;
    std::unique_ptr<juce::ChildProcess> process;
};
