#pragma once

#include "Common.h"

/**
    Runs a job on a background thread with a progress window, then calls a
    completion callback on the message thread and deletes itself.

    Usage:
        BackgroundTask::launch ("Exporting...", [] (BackgroundTask& task) -> juce::String { ...; return error; },
                                [] (const juce::String& error, bool cancelled) { ... });
*/
class BackgroundTask : public juce::ThreadWithProgressWindow
{
public:
    using Work = std::function<juce::String (BackgroundTask&)>;
    using Done = std::function<void (const juce::String& error, bool cancelled)>;

    static void launch (const juce::String& title, Work work, Done done)
    {
        auto* task = new BackgroundTask (title, std::move (work), std::move (done));
        task->launchThread();
    }

    // Accessible to the work function
    using juce::ThreadWithProgressWindow::setProgress;
    using juce::ThreadWithProgressWindow::setStatusMessage;
    bool shouldStop() const { return threadShouldExit(); }

private:
    BackgroundTask (const juce::String& title, Work w, Done d)
        : juce::ThreadWithProgressWindow (title, true, true), work (std::move (w)), done (std::move (d)) {}

    void run() override
    {
        error = work (*this);
    }

    void threadComplete (bool userPressedCancel) override
    {
        if (done)
            done (error, userPressedCancel);
        delete this;
    }

    Work work;
    Done done;
    juce::String error;
};
