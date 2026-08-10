cmake_minimum_required(VERSION 3.22)

project(UndergroundVox VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Descarga JUCE automaticamente (primera vez tarda unos minutos)
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(UndergroundVox
    COMPANY_NAME "Underground"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE        # instala el VST3 automaticamente al compilar
    PLUGIN_MANUFACTURER_CODE Ugnd
    PLUGIN_CODE Uvox
    FORMATS VST3 Standalone             # Standalone sirve para probar sin abrir el DAW
    PRODUCT_NAME "Auto-Mixer")

target_sources(UndergroundVox
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

target_compile_definitions(UndergroundVox
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(UndergroundVox
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
