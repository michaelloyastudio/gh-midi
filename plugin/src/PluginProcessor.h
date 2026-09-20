#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <hidapi.h>

// One process-wide guitar reader shared by every GH MIDI instance.
//
// hidapi/macOS rule (learned the hard way): ALL hidapi calls live on this
// service's single thread, and that thread does its own teardown before dying.
//
// v1.0: controls are read through a ControllerMap (learned via the in-plugin
// calibration wizard and persisted to Application Support), so any HID
// controller can drive the instrument — not just the raphnet WUSBMote.
class GuitarService : private juce::Thread
{
public:
    enum Mode { Easy = 0, Real = 1, Penta = 2, Chart = 3 };
    static constexpr int kOctaveMin = -36, kOctaveMax = 36;   // semitones, ±3 octaves

    struct ButtonMap
    {
        int byteIdx = -1;
        uint8_t mask = 0;
        bool valid() const { return byteIdx >= 0 && mask != 0; }
    };
    struct AxisMap        // whammy: rest position -> fully pressed
    {
        int byteIdx = -1;
        int rest = 0, extreme = 255;
        bool valid() const { return byteIdx >= 0 && rest != extreme; }
    };
    struct StickMap       // joystick axis with centre + travel
    {
        int byteIdx = -1;
        int center = 128, lo = 0, hi = 255;
        bool valid() const { return byteIdx >= 0 && hi > lo; }
    };
    struct ControllerMap
    {
        ButtonMap frets[5], strumDown, strumUp, plusBtn, minusBtn;
        AxisMap whammy;
        StickMap stickX, stickY;
    };

    struct DeviceInfo
    {
        juce::String label;
        int vid = 0, pid = 0;
    };

    GuitarService();
    ~GuitarService() override;

    void addClient(juce::MidiMessageCollector* c);
    void removeClient(juce::MidiMessageCollector* c);

    // ---- live state for editors ----
    std::atomic<int> uiFretBits { 0 };
    std::atomic<int> uiMode { Easy };
    std::atomic<int> uiKey { 0 };
    std::atomic<int> uiOctave { 0 };     // NOTES / SOLO octave offset (semitones)
    std::atomic<int> uiEasyOct { 0 };    // CHORDS octave offset (semitones)
    std::atomic<float> uiWhammy { 0.0f };
    std::atomic<bool> guitarFound { false };
    std::atomic<double> lastPlayedAt { -1.0e9 };  // when the HUD label last changed
    std::atomic<int> lastPlayedFrets { 0 };       // CHART pops: fret mask, drawn as coloured letters (0 = plain text)
    void announceFrets(int mask);                 // CHART: pop the held frets as letters
    std::atomic<bool> adapterEmpty { false };
    std::atomic<bool> hammerOn { false };   // legacy blob compat; no longer user-facing
    std::atomic<int> uiButtonBits { 0 };    // live control test: bit per LearnTarget

    // ---- settings (all persisted) ----
    std::atomic<int> whammyMode { 0 };      // 0 = bend + CC20, 1 = bend, 2 = CC20
    std::atomic<bool> virtualMidiOn { true };
    std::atomic<bool> strumSustain { false }; // on: a note lasts while the strum BAR is held, not the fret
    std::atomic<int> strumRollMs { 10 };   // ms between rolled chord notes (0 = off)  // "GH MIDI" virtual source for DAW note recording

    // ---- MIDI output port ----
    // macOS/Linux publish a virtual "GH MIDI" source. Windows has no virtual
    // MIDI ports, so there the settings panel offers a picker instead: a
    // loopMIDI port to reach a DAW, or any real synth/port.
#if JUCE_MAC || JUCE_LINUX || JUCE_IOS   // where JUCE declares MidiOutput::createNewDevice
    static constexpr bool kHasVirtualMidi = true;
#else
    static constexpr bool kHasVirtualMidi = false;
#endif
    void selectMidiOutput(const juce::String& identifier, const juce::String& name);  // both empty = none
    juce::String currentMidiOutputId() const
    {
        const juce::ScopedLock sl(midiOutLock);
        return midiOutId;
    }

    // ---- state-restore requests (picked up by the guitar thread) ----
    std::atomic<int> modeRequest { -1 };
    std::atomic<int> keyRequest { -1 };

    // ---- on-screen arrows: the UI adds a delta, the guitar thread applies it ----
    void nudgeMode(int d)   { modeNudge += d;   notify(); }
    void nudgeKey(int d)    { keyNudge += d;    notify(); }
    void nudgeOctave(int d) { octaveNudge += d; notify(); }
    void nudgeStrum(int d)  { strumNudge += d;  notify(); }   // ±5 ms per step
    static constexpr int kStrumStepMs = 5, kStrumMaxMs = 50;

    // ---- devices & calibration (UI thread calls; work runs on the guitar thread) ----
    void requestDeviceScan() { scanRequest = true; notify(); }
    juce::Array<DeviceInfo> getDevices() const
    {
        const juce::ScopedLock sl(deviceLock);
        return devices;
    }
    int getDeviceListVersion() const { return deviceListVersion.load(); }
    void selectDevice(int vid, int pid)
    {
        targetVid = vid;
        targetPid = pid;
        reconnectRequest = true;
        saveRequest = true;
        notify();
    }
    int currentVid() const { return targetVid.load(); }
    int currentPid() const { return targetPid.load(); }

    // per-control mapping: each row learned or cleared independently
    enum LearnTarget { LFretG = 0, LFretR, LFretY, LFretB, LFretO,
                       LStrumDown, LStrumUp, LPlus, LMinus, LWhammy, LStickX, LStickY,
                       LTargetCount };
    void startLearn(int target) { learnTarget = juce::jlimit(0, LTargetCount - 1, target); learnPhase = 0; learnT0req = true; notify(); }
    void cancelLearn() { learnTarget = -1; }
    int getLearnTarget() const { return learnTarget.load(); }
    int getMapVersion() const { return mapVersion.load(); }
    void clearMapping(int target);
    juce::String describeMapping(int target) const;
    static juce::String targetName(int target);

    void requestSave() { saveRequest = true; notify(); }

    juce::String getLastPlayed() const
    {
        const juce::ScopedLock sl(labelLock);
        return lastPlayed;
    }

    struct Gem
    {
        int mask = 0;
        double t0 = 0.0;
        double t1 = -1.0;
        bool legato = false;
    };
    juce::Array<Gem> getRecentGems() const
    {
        const juce::ScopedLock sl(gemLock);
        return gems;
    }

private:
    void run() override;
    void demoRun();
    void step(const uint8_t* d, int len);
    void learnTick(const uint8_t* d, int len, double now);
    void scanDevices();
    void loadSettings();
    void saveSettings();
    void applyMapForDevice();
    juce::String deviceKey() const;
    static juce::File settingsFile();

    bool demoMode = false;
    std::unique_ptr<juce::FileLogger> demoLogger;   // demo mode only: milestones + crash backtrace
    void sendMsg(const juce::MidiMessage& m);
    void noteOn(int note, int vel);
    void allOff();
    void announce(const juce::String& s);

    juce::CriticalSection clientLock;
    juce::Array<juce::MidiMessageCollector*> clients;
    std::unique_ptr<juce::MidiOutput> virtualOut;  // created/destroyed on the guitar thread
    std::unique_ptr<juce::MidiOutput> portOut;     // user-picked port (guitar thread only)
    mutable juce::CriticalSection midiOutLock;
    juce::String midiOutId, midiOutName;           // persisted; guarded by midiOutLock
    std::atomic<bool> midiOutRequest { false };
    void openMidiOutput();

    hid_device* dev = nullptr;
    int ioMode = 0;        // 1 = get_input_report id 1 · 2 = id 0 · 3 = hid_read stream
    uint8_t lastBuf[64] {};
    int lastLen = 0;

    // ---- controller mapping (guarded by mapLock; step copies it per tick) ----
    mutable juce::CriticalSection mapLock;
    ControllerMap map;
    juce::var controllersVar;   // per-device saved mappings (guitar thread only)
    std::atomic<int> mapVersion { 0 };
    std::atomic<int> targetVid { 0x289B }, targetPid { 0x0080 };

    // device list (guitar thread writes, UI reads)
    mutable juce::CriticalSection deviceLock;
    juce::Array<DeviceInfo> devices;
    std::atomic<int> deviceListVersion { 0 };
    std::atomic<bool> scanRequest { false }, reconnectRequest { false }, saveRequest { false };

    // per-row learn engine
    std::atomic<int> learnTarget { -1 };
    std::atomic<bool> learnT0req { false };
    int learnPhase = 0, learnByte = -1;
    uint8_t learnMask = 0;
    uint8_t learnBase[64] {};
    int learnBaseLen = 0;
    double learnT0 = 0, learnChangeAt = 0, axStart = 0;
    int axMin[64] {}, axMax[64] {};

    // engine state (guitar thread only)
    std::atomic<int> modeNudge { 0 }, keyNudge { 0 }, octaveNudge { 0 }, strumNudge { 0 };
    void cycleMode(int dir);
    void shiftKey(int d);
    void shiftOctave(int d);
    void setStrum(int ms);
    int mode = Easy;
    int key = 0, octaveReal = 0, easyOct = 0;
    bool prevDown = false, prevUp = false, prevPlus = false, prevMinus = false;
    double strumLatchAt = -1.0;
    int latchVel = 100;
    bool latchDown = true;
    juce::SortedSet<int> ringing;
    int ringFret = -1, ringCombo = -1;
    int soloNotes[5] { -1, -1, -1, -1, -1 };  // SOLO: ringing note per fret
    int chartHeld = 0;                         // CHART: frets whose lane notes are sounding
    int candCombo = -1;
    double candSince = 0.0;
    double sustainOnAt = -1.0;       // strum-sustain: when the ringing notes started
    bool sustainOffPending = false;  // strum-sustain: bar released before the minimum, off owed
    double lastLegatoAt = -1.0;
    int joyPos = 0, joyPosY = 0;
    float prevWham = 0.0f;
    double whamBusyUntil = -1.0;
    int bend = 0;

    void beginGem(int mask, bool legato);
    void endGem();

    mutable juce::CriticalSection labelLock;
    juce::String lastPlayed;

    mutable juce::CriticalSection gemLock;
    juce::Array<Gem> gems;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GuitarService)
};

class GHMidiProcessor : public juce::AudioProcessor
{
public:
    GHMidiProcessor();
    ~GHMidiProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { service->removeClient(&collector); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "GH MIDI"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    GuitarService& guitar() { return *service; }

private:
    juce::SharedResourcePointer<GuitarService> service;
    juce::MidiMessageCollector collector;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GHMidiProcessor)
};
