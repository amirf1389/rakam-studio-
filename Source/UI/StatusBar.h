#pragma once

#include "Engine/AudioEngine.h"

/** Thin bar at the bottom: audio device, sample rate, latency, CPU, song length and save state. */
class StatusBar : public juce::Component,
                  public juce::SettableTooltipClient,
                  private juce::Timer
{
public:
    explicit StatusBar (AudioEngine&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }

    std::function<void()> onClick;
    void setMessage (const juce::String& text);

private:
    void timerCallback() override;

    AudioEngine& engine;
    juce::String deviceText, cpuText, lengthText, message;
    juce::uint32 messageTime = 0;
    double cpu = 0.0;
};
