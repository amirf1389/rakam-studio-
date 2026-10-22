#pragma once

#include "Common.h"
#include <juce_dsp/juce_dsp.h>

/**
    Rakam Studio's own effects. They work like VST3 plugins (inserts on any
    track or the master, saved with the project, presets, tempo sync) but are
    built into the app, so they're always available — no installing or scanning.
*/
class BuiltInPlugin : public juce::AudioPluginInstance
{
public:
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    struct Preset
    {
        juce::String name;
        std::vector<std::pair<juce::String, float>> values;   // parameter id -> real value
    };

    BuiltInPlugin (const juce::String& name, const juce::String& category, Layout layout);

    //==============================================================================
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) final { process (buffer); }

    /** Each effect processes stereo (or mono) audio in place. */
    virtual void process (juce::AudioBuffer<float>&) = 0;

    const juce::String getName() const override { return pluginName; }
    void fillInPluginDescription (juce::PluginDescription&) const override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    void releaseResources() override {}
    double getTailLengthSeconds() const override { return tailSeconds; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    int getNumPrograms() override                         { return juce::jmax (1, (int) presets.size()); }
    int getCurrentProgram() override                      { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int size) override;

    /** Resets every parameter to its default value. */
    void resetToDefaults();

    //==============================================================================
    /** Optional live display drawn above the knobs (EQ curve, gain-reduction meter...). */
    virtual int  getDisplayHeight() const { return 0; }
    virtual void paintDisplay (juce::Graphics&, juce::Rectangle<float>) {}

    juce::AudioProcessorValueTreeState state;

    static juce::String identifierPrefix() { return "rakam:"; }

protected:
    float param (const char* id) const { return state.getRawParameterValue (id)->load(); }
    double getHostBpm() const;

    std::vector<Preset> presets;
    double tailSeconds = 0.0;

private:
    juce::String pluginName, pluginCategory;
    int currentPreset = 0;
};

//==============================================================================
/** Lets the plugin manager create built-in effects like any other plugin format. */
class BuiltInPluginFormat : public juce::AudioPluginFormat
{
public:
    static constexpr const char* formatName = "Rakam";

    static juce::StringArray getEffectNames();
    static std::unique_ptr<BuiltInPlugin> create (const juce::String& nameOrIdentifier);
    static juce::Array<juce::PluginDescription> getAllDescriptions();

    juce::String getName() const override { return formatName; }
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String&) override;
    bool fileMightContainThisPluginType (const juce::String& f) override { return f.startsWith (BuiltInPlugin::identifierPrefix()); }
    juce::String getNameOfPluginFromIdentifier (const juce::String& f) override { return f.fromFirstOccurrenceOf (BuiltInPlugin::identifierPrefix(), false, false); }
    bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
    bool doesPluginStillExist (const juce::PluginDescription& d) override { return create (d.fileOrIdentifier) != nullptr; }
    bool canScanForPlugins() const override { return false; }
    bool isTrivialToScan() const override { return true; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }

private:
    void createPluginInstance (const juce::PluginDescription&, double, int, PluginCreationCallback) override;
};
