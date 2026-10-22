#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#ifndef JUCE_APPLICATION_VERSION_STRING
 #define JUCE_APPLICATION_VERSION_STRING "dev"
#endif

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace Rakam
{
    inline const juce::String appName     { "Rakam Studio" };
    inline const juce::String projectExt  { ".rakam" };

    // Shared colours used throughout the UI
    namespace Colours
    {
        inline const juce::Colour background  { 0xff15171c };
        inline const juce::Colour panel       { 0xff1e222a };
        inline const juce::Colour panelLight  { 0xff2a2f39 };
        inline const juce::Colour grid        { 0xff2c313b };
        inline const juce::Colour gridStrong  { 0xff3c4350 };
        inline const juce::Colour text        { 0xffe6e8ec };
        inline const juce::Colour textDim     { 0xff8b93a3 };
        inline const juce::Colour accent      { 0xffff6a3d };
        inline const juce::Colour record      { 0xffe5484d };
        inline const juce::Colour mute        { 0xfff5a524 };
        inline const juce::Colour solo        { 0xff3ec27b };
        inline const juce::Colour monitor     { 0xff4c9aff };
    }

    inline juce::File getUserFolder()
    {
        auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (appName);
        f.createDirectory();
        return f;
    }
}
