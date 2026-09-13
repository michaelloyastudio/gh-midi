#pragma once
#include "PluginProcessor.h"
#include "HighwayRenderer.h"

// 3D highway underneath (OpenGL); HUD, settings panel (device + mapping table)
// and help overlay composited on top.
class GHMidiEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GHMidiEditor(GHMidiProcessor&);
    ~GHMidiEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setPanelVisible(bool visible);
    void setHelpVisible(bool visible);
    void refreshDeviceBox();
    void refreshMappingRows();

    GHMidiProcessor& proc;
    HighwayRenderer highway;
    juce::OpenGLContext context;

    bool panelOpen = false, helpOpen = false;

    juce::TextButton gearBtn { "SETTINGS" }, helpBtn { "?" };

    // settings panel
    juce::Label deviceLabel, learnHint;
    juce::ComboBox deviceBox;
    juce::TextButton rescanBtn { "RESCAN" }, closeBtn { "CLOSE" };
    juce::Slider strumSlider;
    juce::Label strumLabel;
    juce::ToggleButton vmidiToggle { "Virtual MIDI output (record notes in your DAW)" };
    juce::OwnedArray<juce::Label> rowNames, rowDescs;
    juce::OwnedArray<juce::TextButton> rowLearn, rowClear;

    // help overlay
    juce::TextButton helpCloseBtn { "CLOSE" }, guideBtn { juce::String(juce::CharPointer_UTF8("\xE2\x86\x97")) };

    juce::Array<GuitarService::DeviceInfo> shownDevices;
    int lastDevVersion = -1, lastMapVersion = -1, lastLearnTarget = -999;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GHMidiEditor)
};
