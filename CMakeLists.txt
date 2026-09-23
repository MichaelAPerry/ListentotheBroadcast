cmake_minimum_required(VERSION 3.22)
project(ListenToTheBroadcast VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()
if(MSVC)
    # One self-contained DLL: no Visual C++ runtime install needed on the user's machine.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(LTB_BUILD_PLUGIN "Build the VST3 plugin (downloads JUCE)" ON)
option(LTB_BUILD_TESTS "Build the tests" ON)

# ---------------------------------------------------------------- core (no JUCE)
add_library(ltb_core STATIC
    source/core/AmbientSynth.cpp
    source/core/MusicEngine.cpp
    source/core/NetListener.cpp)
target_include_directories(ltb_core PUBLIC source/core)
set_target_properties(ltb_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
target_link_libraries(ltb_core PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(ltb_core PUBLIC ws2_32)
endif()

if(LTB_BUILD_TESTS)
    enable_testing()
    add_executable(ltb_core_tests tests/core_tests.cpp)
    target_link_libraries(ltb_core_tests PRIVATE ltb_core)
    add_test(NAME core COMMAND ltb_core_tests)
endif()

# ---------------------------------------------------------------- plugin
if(LTB_BUILD_PLUGIN)
    include(FetchContent)
    # Set FETCHCONTENT_SOURCE_DIR_JUCE to use a local checkout instead of downloading.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(ListenToTheBroadcast
        PRODUCT_NAME "Listen to the Broadcast"
        COMPANY_NAME "Listen to the Broadcast"
        BUNDLE_ID "org.listentothebroadcast.plugin"
        PLUGIN_MANUFACTURER_CODE Ltbc
        PLUGIN_CODE Ltb1
        FORMATS VST3
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Instrument Generator
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(ListenToTheBroadcast PRIVATE
        source/plugin/PluginProcessor.cpp
        source/plugin/PluginEditor.cpp
        source/plugin/Parameters.cpp)
    target_include_directories(ListenToTheBroadcast PRIVATE source/plugin)

    target_compile_definitions(ListenToTheBroadcast PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(ListenToTheBroadcast
        PRIVATE
            ltb_core
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    if(LTB_BUILD_TESTS)
        # Drives the real processor offline: fake traffic in, audio + MIDI out.
        juce_add_console_app(ltb_plugin_tests PRODUCT_NAME "ltb_plugin_tests")
        target_sources(ltb_plugin_tests PRIVATE
            tests/plugin_tests.cpp
            source/plugin/PluginProcessor.cpp
            source/plugin/PluginEditor.cpp
            source/plugin/Parameters.cpp)
        target_include_directories(ltb_plugin_tests PRIVATE source/plugin)
        target_compile_definitions(ltb_plugin_tests PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Listen to the Broadcast")
        target_link_libraries(ltb_plugin_tests PRIVATE
            ltb_core juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
        add_test(NAME plugin COMMAND ltb_plugin_tests)
    endif()
endif()
