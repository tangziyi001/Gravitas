cmake_minimum_required(VERSION 3.22)
project(Gravitas VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Point to your JUCE installation
add_subdirectory($ENV{HOME}/JUCE JUCE)

juce_add_plugin(Gravitas
    VERSION                     0.1.0
    COMPANY_NAME                "ZetaSonic"
    BUNDLE_ID                   "com.ZetaSonic.Gravitas"
    PLUGIN_MANUFACTURER_CODE    Grvt
    PLUGIN_CODE                 Grv1
    FORMATS                     VST3 AU
    PRODUCT_NAME                "Gravitas"
    PLUGIN_NAME                 "Gravitas"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             "Fx"
    AU_MAIN_TYPE                "kAudioUnitType_Effect"
)

juce_add_binary_data(GravitasAssets SOURCES
    Assets/Textures/mercury.jpg
    Assets/Textures/venus.jpg
    Assets/Textures/earth.jpg
    Assets/Textures/mars.jpg
    Assets/Textures/jupiter.jpg
    Assets/Textures/saturn.jpg
    Assets/Textures/uranus.jpg
    Assets/Textures/neptune.jpg
)

juce_generate_juce_header(Gravitas)

target_sources(Gravitas PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Physics/BallPhysics.cpp
    Source/DSP/CircularBuffer.cpp
    Source/DSP/StutterEngine.cpp
)

target_include_directories(Gravitas PRIVATE Source)

target_compile_definitions(Gravitas PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Gravitas PRIVATE
    GravitasAssets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_graphics
    juce::juce_gui_basics
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)