cmake_minimum_required(VERSION 3.24)
project(GHMidi VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# universal binary so Intel Macs can run it too
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)

# hidapi is compiled in statically — no Homebrew dependency for users
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)  # hidapi declares an old minimum
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(HIDAPI_BUILD_HIDTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(hidapi
    GIT_REPOSITORY https://github.com/libusb/hidapi.git
    GIT_TAG hidapi-0.14.0
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(JUCE hidapi)

juce_add_plugin(GHMidi
    COMPANY_NAME "Michael Loya Studio"
    COMPANY_WEBSITE "https://michaelloya.studio"
    BUNDLE_ID "studio.michaelloya.ghmidi"
    PRODUCT_NAME "GH MIDI"
    PLUGIN_MANUFACTURER_CODE Loya
    PLUGIN_CODE Ghmd
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    # JUCE's standalone Options menu always offers "Bluetooth MIDI", and
    # tapping it asks CoreBluetooth for access. Without a usage string in
    # Info.plist macOS does not deny the request, it SIGABRTs the process --
    # confirmed in the crash report as a TCC privacy violation. GH MIDI does
    # not use Bluetooth itself; this only stops the OS killing the app when
    # someone opens that menu item.
    # "GH" set in MetalMania on the app's own stage background. Regenerate
    # with tools_make_icon.py -- it ports kBgFrag out of HighwayRenderer.cpp
    # rather than upscaling a screenshot.
    ICON_BIG "assets/icon.png"
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "GH MIDI needs Bluetooth only if you want to connect a Bluetooth MIDI device. The guitar itself connects over USB."
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(GHMidi PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/HighwayRenderer.cpp)

juce_add_binary_data(GHMidiAssets SOURCES
    assets/MetalMania-Regular.ttf)


target_compile_definitions(GHMidi PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    GL_SILENCE_DEPRECATION=1)

target_link_libraries(GHMidi PRIVATE
    GHMidiAssets
    hidapi::hidapi
    juce::juce_audio_utils
    juce::juce_opengl
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
