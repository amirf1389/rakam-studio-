#pragma once

#include "Engine/AudioEngine.h"

/**
    Saving, loading and packaging projects.

    A project is an XML file (.rakam). "Collect & Zip" bundles the project plus
    every audio file it uses into a single .zip you can send to someone else or
    move to another PC. Templates are projects saved without audio.
*/
namespace ProjectIO
{
    struct SaveOptions
    {
        bool markAsSaved       = true;   // false for autosave / "save a copy"
        bool includeClips      = true;   // false for templates
        bool copyExternalAudio = false;  // copy audio from elsewhere into <project>/Audio
        int  backupsToKeep     = 0;      // keep this many older versions in <project>/Backups
    };

    bool save (AudioEngine& engine, const juce::File& projectFile, juce::String& error, const SaveOptions& options = {});

    /** A clip whose audio file couldn't be found when the project was opened. */
    struct MissingClip
    {
        Track* track = nullptr;
        juce::String name, fileName, originalPath;
        double start = 0, offset = 0, length = 0, fadeIn = 0, fadeOut = 0;
        float gain = 1.0f;
        bool muted = false;
        int takeGroup = 0, take = 0;
    };
    using MissingList = std::vector<MissingClip>;

    /** Loads a project. Missing files / plugins are reported in warnings; missing clips are listed so they can be relinked. */
    bool load (AudioEngine& engine, const juce::File& projectFile, juce::String& error, juce::StringArray& warnings,
               MissingList* missing = nullptr);

    /** Searches a folder (and its subfolders) for missing files by name and puts the clips back. Returns how many were found. */
    int relinkMissing (AudioEngine& engine, MissingList& missing, const juce::File& searchFolder);

    /** Writes a self-contained zip: project.rakam + Audio/ + PLUGINS_NEEDED.txt */
    bool collectAndZip (AudioEngine& engine, const juce::File& zipFile, juce::String& error,
                        double* progress = nullptr);

    /** Extracts a package zip into Documents/Rakam Studio/Imported and returns the project file inside. */
    juce::File extractPackage (const juce::File& zipFile, juce::String& error);

    // Templates
    juce::StringArray getStarterTemplateNames();
    void buildStarterTemplate (AudioEngine& engine, const juce::String& name);   // into an empty project
    juce::Array<juce::File> getUserTemplates();
    bool saveAsTemplate (AudioEngine& engine, const juce::String& name, juce::String& error);
}
