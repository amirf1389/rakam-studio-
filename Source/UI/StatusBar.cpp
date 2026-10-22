#include "UI/StatusBar.h"

using namespace Rakam::Colours;

StatusBar::StatusBar (AudioEngine& e) : engine (e)
{
    setTooltip ("Click to open Audio / ASIO settings");
    startTimerHz (4);
    timerCallback();
}

void StatusBar::setMessage (const juce::String& text)
{
    message = text;
    messageTime = juce::Time::getMillisecondCounter();
    repaint();
}

void StatusBar::timerCallback()
{
    if (auto* d = engine.getDeviceManager().getCurrentAudioDevice())
    {
        const double rate = d->getCurrentSampleRate();
        const int buffer = d->getCurrentBufferSizeSamples();
        const double latencyMs = 1000.0 * (d->getInputLatencyInSamples() + d->getOutputLatencyInSamples()) / juce::jmax (1.0, rate);
        deviceText = d->getTypeName() + ": " + d->getName() + "    " + juce::String (rate / 1000.0, 1) + " kHz    "
                   + juce::String (buffer) + " samples    " + juce::String (latencyMs, 1) + " ms round trip";
    }
    else
    {
        deviceText = "No audio device - click here to choose one";
    }

    cpu = cpu * 0.7 + engine.getCpuUsage() * 0.3;
    cpuText = "CPU " + juce::String (juce::roundToInt (cpu * 100.0)) + "%";

    const double len = engine.getProjectLengthSec();
    lengthText = "Length " + juce::String ((int) (len / 60.0)) + ":" + juce::String ((int) std::fmod (len, 60.0)).paddedLeft ('0', 2)
               + "    " + juce::String (engine.getNumTracks()) + " tracks"
               + (engine.hasUnsavedChanges() ? "    Unsaved changes" : "    Saved");

    if (message.isNotEmpty() && juce::Time::getMillisecondCounter() - messageTime > 4000)
        message.clear();

    repaint();
}

void StatusBar::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    g.setColour (grid);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    auto r = getLocalBounds().reduced (10, 0);
    g.setFont (juce::FontOptions (12.0f));

    // CPU with a tiny bar
    auto cpuArea = r.removeFromRight (110);
    auto bar = cpuArea.removeFromRight (40).withSizeKeepingCentre (40, 6).toFloat();
    g.setColour (background);
    g.fillRoundedRectangle (bar, 3.0f);
    g.setColour (cpu > 0.8 ? record : cpu > 0.5 ? mute : solo);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, cpu)), 3.0f);
    g.setColour (textDim);
    g.drawText (cpuText, cpuArea.withTrimmedRight (6), juce::Justification::centredRight);

    g.drawText (lengthText, r.removeFromRight (260), juce::Justification::centredRight);

    if (message.isNotEmpty())
    {
        g.setColour (accent);
        g.drawText (message, r, juce::Justification::centredLeft, true);
    }
    else
    {
        g.drawText (deviceText, r, juce::Justification::centredLeft, true);
    }
}
