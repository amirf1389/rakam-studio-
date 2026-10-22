cmake_minimum_required(VERSION 3.22)

project(RakamStudio VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# JUCE (audio framework). Downloaded automatically the first time you configure.
# JUCE 9 bundles the ASIO headers, so no separate Steinberg SDK download is needed.
# To use a JUCE folder you already have:  cmake -DJUCE_LOCAL_PATH=C:/path/to/JUCE ...
# ------------------------------------------------------------------------------
set(JUCE_LOCAL_PATH "" CACHE PATH "Optional path to a local JUCE checkout")

if(JUCE_LOCAL_PATH)
    add_subdirectory(${JUCE_LOCAL_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_gui_app(RakamStudio
    PRODUCT_NAME "Rakam Studio"
    COMPANY_NAME "Rakam"
    VERSION 0.4.0)

target_sources(RakamStudio PRIVATE
    Source/Main.cpp
    Source/Engine/AudioEngine.cpp
    Source/Engine/ProjectIO.cpp
    Source/Engine/StemSeparator.cpp
    Source/Plugins/BuiltInPlugins.cpp
    Source/Plugins/PluginEditor.cpp
    Source/UI/TransportBar.cpp
    Source/UI/BrowserPanel.cpp
    Source/UI/StatusBar.cpp
    Source/UI/SettingsWindow.cpp
    Source/UI/ProjectDialogs.cpp
    Source/UI/RecorderPanel.cpp
    Source/UI/IOPanel.cpp
    Source/UI/ArrangementView.cpp
    Source/UI/MixerView.cpp
    Source/UI/ExportDialog.cpp
    Source/UI/StemSeparatorDialog.cpp
    Source/UI/MainComponent.cpp)

target_include_directories(RakamStudio PRIVATE Source)

if(WIN32)
    set(RAKAM_ASIO 1)
else()
    set(RAKAM_ASIO 0)
endif()

target_compile_definitions(RakamStudio PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:RakamStudio,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:RakamStudio,JUCE_VERSION>"
    JUCE_ASIO=${RAKAM_ASIO}
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_AU=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_LAME_AUDIO_FORMAT=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(RakamStudio
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# Optional engine self-test (mixing, stems, save/load, packaging)
#   cmake -DRAKAM_BUILD_TESTS=ON ...  then run RakamEngineTest
# ------------------------------------------------------------------------------
option(RAKAM_BUILD_TESTS "Build the headless engine test" OFF)

if(RAKAM_BUILD_TESTS)
    juce_add_console_app(RakamEngineTest PRODUCT_NAME "RakamEngineTest")
    target_sources(RakamEngineTest PRIVATE
        Tests/EngineTest.cpp
        Source/Engine/AudioEngine.cpp
        Source/Engine/ProjectIO.cpp
        Source/Plugins/BuiltInPlugins.cpp
        Source/Plugins/PluginEditor.cpp)
    target_include_directories(RakamEngineTest PRIVATE Source)
    target_compile_definitions(RakamEngineTest PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_LAME_AUDIO_FORMAT=1
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_ASIO=${RAKAM_ASIO})
    target_link_libraries(RakamEngineTest PRIVATE
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp juce::juce_gui_extra
        juce::juce_recommended_config_flags)
endif()
