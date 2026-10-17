cmake_minimum_required(VERSION 3.22)
project(ClassicBusCompressor VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
option(COPY_PLUGIN_AFTER_BUILD "Copy AU and VST3 bundles after building the plug-in target" OFF)

add_library(classic_bus_dsp STATIC Source/dsp/BusCompressor.cpp)
target_include_directories(classic_bus_dsp PUBLIC Source)
if(MSVC)
    target_compile_options(classic_bus_dsp PRIVATE /W4)
else()
    target_compile_options(classic_bus_dsp PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(classic_bus_test tests/BusCompressorTest.cpp)
target_link_libraries(classic_bus_test PRIVATE classic_bus_dsp)
add_test(NAME classic_bus_test COMMAND classic_bus_test)

set(JUCE_SOURCE_DIR "" CACHE PATH "Path to a JUCE 8 source checkout")
if(JUCE_SOURCE_DIR AND EXISTS "${JUCE_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_SOURCE_DIR}" juce-build)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
    add_subdirectory(third_party/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(ClassicBusCompressor
    PRODUCT_NAME "Classic Bus Compressor"
    COMPANY_NAME "codycodie"
    BUNDLE_ID "com.c0dypeng.classicbuscompressor"
    PLUGIN_MANUFACTURER_CODE Cdpg
    PLUGIN_CODE CbCp
    FORMATS AU VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD ${COPY_PLUGIN_AFTER_BUILD})

juce_add_binary_data(ClassicBusCompressorAssets SOURCES
    Resources/fonts/Inter-SemiBold.ttf
    Resources/fonts/JetBrainsMono-Medium.ttf)

target_sources(ClassicBusCompressor PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(ClassicBusCompressor PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ClassicBusCompressor PRIVATE classic_bus_dsp ClassicBusCompressorAssets juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

juce_add_console_app(classic_bus_snapshot)
target_sources(classic_bus_snapshot PRIVATE
    tests/Snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(classic_bus_snapshot PRIVATE
    JucePlugin_Name="Classic Bus Compressor"
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(classic_bus_snapshot PRIVATE classic_bus_dsp ClassicBusCompressorAssets juce::juce_audio_utils
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
