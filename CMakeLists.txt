cmake_minimum_required(VERSION 3.22)
project(MOSAIC VERSION 0.1.0 LANGUAGES C CXX)   # the one version number: plugin, installer and download names

# Continuum Mosaic as a VST3 instrument (+ standalone app). Mosaic's own page is the interface (WebView2);
# its sound is made in C++. JUCE is downloaded at configure time; on Windows so is the WebView2 SDK.
# Before building, refresh the page from Mosaic:  python3 tools/build_ui.py path/to/mosaic_subtractive_fm/index.html

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # no VC++ redistributable needed
    add_compile_options(/bigobj)
endif()

set(MOSAIC_JUCE_DIR "" CACHE PATH "Use a local JUCE checkout instead of downloading it")
if(MOSAIC_JUCE_DIR)
    add_subdirectory(${MOSAIC_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(WIN32)
    set(WV2_VERSION 1.0.1901.177)
    set(WV2_ROOT "${CMAKE_BINARY_DIR}/webview2")
    set(WV2_DIR "${WV2_ROOT}/Microsoft.Web.WebView2.${WV2_VERSION}")
    if(NOT EXISTS "${WV2_DIR}/build/native/include/WebView2.h")
        file(DOWNLOAD "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WV2_VERSION}" "${WV2_ROOT}/webview2.zip" SHOW_PROGRESS STATUS wv2_status)
        list(GET wv2_status 0 wv2_code)
        if(NOT wv2_code EQUAL 0)
            message(FATAL_ERROR "Could not download the WebView2 SDK from nuget.org: ${wv2_status}")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${WV2_ROOT}/webview2.zip" DESTINATION "${WV2_DIR}")
    endif()
    set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${WV2_ROOT}")
endif()

juce_add_plugin(MOSAIC
    PRODUCT_NAME "Mosaic"
    COMPANY_NAME "Continuum"
    BUNDLE_ID "com.continuum.mosaic"
    PLUGIN_MANUFACTURER_CODE Cntm
    PLUGIN_CODE Msc1
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE
    VST3_CATEGORIES Instrument Synth)

juce_add_binary_data(MosaicUI NAMESPACE BinaryData SOURCES ui/index.html ui/wtbank.json)

target_sources(MOSAIC PRIVATE
    Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/BrowserHost.cpp
    Source/dsp/MosaicVoice.cpp Source/dsp/MosaicState.cpp Source/dsp/MosaicFx.cpp
    Source/dsp/TableEngine.cpp Source/dsp/WtCore.cpp Source/dsp/PeriodicOsc.cpp)
target_include_directories(MOSAIC PRIVATE Source)
target_compile_definitions(MOSAIC PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(MOSAIC
    PRIVATE MosaicUI juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------------------------------------------
# Mosaic FX: Mosaic's FX bus as a stereo audio effect (VST3). A separate plugin with its own name, codes, bundle ID,
# data folder and installer; it shares only the FX engine's source and the page with the synth, and never writes
# anything the Mosaic synth uses.
juce_add_plugin(MOSAIC_FX
    PRODUCT_NAME "Mosaic FX"
    COMPANY_NAME "Continuum"
    BUNDLE_ID "com.continuum.mosaicfx"
    PLUGIN_MANUFACTURER_CODE Cntm
    PLUGIN_CODE MscF
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE
    VST3_CATEGORIES Fx)

target_sources(MOSAIC_FX PRIVATE Source/fx/FxProcessor.cpp Source/fx/FxBrowserHost.cpp Source/dsp/MosaicFx.cpp)
target_include_directories(MOSAIC_FX PRIVATE Source)
target_compile_definitions(MOSAIC_FX PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(MOSAIC_FX
    PRIVATE MosaicUI juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
