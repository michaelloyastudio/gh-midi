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
    void updateHudButtons();
    void refreshDeviceBox();
    void refreshMappingRows();

    GHMidiProcessor& proc;
    HighwayRenderer highway;
    juce::OpenGLContext context;

    bool panelOpen = false, helpOpen = false;

    juce::TextButton gearBtn { "SETTINGS" }, helpBtn { "?" };

    // which control decides how long a note lasts: the fret (default) or the
    // strum bar. Lives on the main view under ? / SETTINGS, not in the panel.
    juce::Label sustainLabel;
    juce::TextButton sustainFretBtn { "FRET" }, sustainStrumBtn { "STRUM" };
    void syncSustainButtons();

    // on-screen arrows for the three settings the controller also changes
    juce::TextButton modePrev, modeNext, strumPrev, strumNext, keyPrev, keyNext, octPrev, octNext;

    // settings panel
    juce::Label deviceLabel, learnHint;
    juce::ComboBox deviceBox;
    juce::TextButton rescanBtn { "RESCAN" }, closeBtn { "X" };
    juce::ToggleButton vmidiToggle { "Virtual MIDI output (record notes in your DAW)" };
    juce::OwnedArray<juce::Label> rowNames, rowDescs;
    juce::OwnedArray<juce::TextButton> rowLearn, rowClear;

    // help overlay
    juce::TextButton helpCloseBtn { "X" }, moreBtn { "MORE HELP" };

    // GHMIDI_HUDSNAP=<dir> (standalone only): paint the HUD states to PNGs and quit
    juce::String hudSnapDir;
    int hudSnapTick = 0;
    void saveHudSnapshot(const juce::String& name);

    juce::Array<GuitarService::DeviceInfo> shownDevices;
    int lastDevVersion = -1, lastMapVersion = -1, lastLearnTarget = -999;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GHMidiEditor)
};
