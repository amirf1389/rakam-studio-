#pragma once

#include "Common.h"

/**
    All user preferences live in one PropertiesFile
    (%APPDATA%\Rakam Studio\Rakam Studio.settings on Windows).
    This header names every key and its default so they are used consistently.
*/
namespace Settings
{
    // General
    inline const char* uiScale          = "uiScale";          // percent: 90 / 100 / 110 / 125 / 150
    inline const char* trackHeight      = "trackHeight";      // pixels
    inline const char* startupAction    = "startupAction";    // 1 = empty project, 2 = reopen last project, 3 = template picker
    inline const char* confirmDelete    = "confirmDelete";
    inline const char* defaultBpm       = "defaultBpm";

    // Recording
    inline const char* recordFormat     = "recordFormat";     // 1 WAV 24, 2 WAV 32 float, 3 WAV 16, 4 FLAC 24
    inline const char* countInBars      = "countInBars";      // 0, 1, 2
    inline const char* autoMonitor      = "autoMonitor";      // arm also turns on input monitoring
    inline const char* recordMode       = "recordMode";       // AudioEngine::RecordMode

    // Files
    inline const char* projectsFolder   = "projectsFolder";
    inline const char* exportsFolder    = "exportFolder";
    inline const char* autosaveMinutes  = "autosaveMinutes";  // 0 = off
    inline const char* backupCount      = "backupCount";      // 0 = off
    inline const char* copyAudioOnSave  = "copyAudioOnSave";
    inline const char* recentFiles      = "recentFiles";
    inline const char* lastProject      = "lastProject";
    inline const char* cleanExit        = "cleanExit";

    // Export
    inline const char* exportFormat     = "exportFormat";
    inline const char* exportNormalize  = "exportNormalize";
    inline const char* lamePath         = "lamePath";         // lame.exe for MP3 export

    inline int    getInt (juce::PropertiesFile& s, const char* key, int def)          { return s.getIntValue (key, def); }
    inline bool   getBool (juce::PropertiesFile& s, const char* key, bool def)        { return s.getBoolValue (key, def); }
    inline double getDouble (juce::PropertiesFile& s, const char* key, double def)    { return s.getDoubleValue (key, def); }

    inline juce::File getFolder (juce::PropertiesFile& s, const char* key, const juce::String& defaultSubfolder)
    {
        juce::File f (s.getValue (key));
        if (! f.isDirectory())
            f = Rakam::getUserFolder().getChildFile (defaultSubfolder);
        f.createDirectory();
        return f;
    }

    inline juce::File getAutosaveFolder()
    {
        auto f = Rakam::getUserFolder().getChildFile ("Autosave");
        f.createDirectory();
        return f;
    }

    inline juce::File getTemplatesFolder()
    {
        auto f = Rakam::getUserFolder().getChildFile ("Templates");
        f.createDirectory();
        return f;
    }
}
