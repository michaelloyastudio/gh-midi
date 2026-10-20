#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdlib>
#include <cstring>

#if JUCE_WINDOWS
// Windows sleeps in 15.6 ms ticks unless a process asks for 1 ms timers --
// that alone made every poll of the guitar up to 15 ms late. winmm, declared
// here so this file doesn't pull in <windows.h>.
extern "C" __declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int);
extern "C" __declspec(dllimport) unsigned int __stdcall timeEndPeriod(unsigned int);
#pragma comment(lib, "winmm.lib")
#endif

namespace {
int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
// penta mode: fret -> major pentatonic degree of the key
constexpr int kPentaDeg[5] = { 0, 2, 4, 7, 9 };
constexpr int kChordRootBase = 48, kBassBase = 36, kRealBase = 40;
constexpr int kVelDown = 100, kVelUp = 78;
// strum edges wait this long so a fret that lands a hair late still joins
// the chord (and a one-report contact glitch is filtered out). Was 10 ms.
constexpr double kStrumLatchS = 0.004;
// palm mute: notes are choked this long after the strum, played softer, and
// chords drop to a low power-chord voicing -- the chug
constexpr double kPalmS = 0.11;
constexpr float kPalmVel = 0.72f;
constexpr float kPalmWhammy = 0.22f;   // whammy pressed past this = palm on
// CHART mode: Clone Hero / Rock Band PART GUITAR lanes, green..orange = base..base+4,
// one base per difficulty (Easy, Medium, Hard, Expert). Every difficulty is written at once.
constexpr int kChartLanes[4] = { 60, 72, 84, 96 };
// strum-sustain: a bar tap can be 20-30ms of contact, which as a MIDI note is
// a click. The off is held back so a flick still sounds like a note.
constexpr double kMinSustainS = 0.08;

const char* noteNames[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

juce::String noteName(int n)
{
    return juce::String(noteNames[((n % 12) + 12) % 12]) + juce::String(floorDiv(n, 12) - 1);
}

// chord qualities: intervals above the root. third < 0 = power chord (no
// third); top = the highest chord tone (0 = the octave)
struct Quality { const char* suffix; int third; int fifth; int top; };
constexpr Quality kQualities[] = {
    { "",     4, 7, 0 },  { "m",    3, 7, 0 },  { "7",    4, 7, 10 }, { "maj7", 4, 7, 11 },
    { "m7",   3, 7, 10 }, { "dim",  3, 6, 0 },  { "m7b5", 3, 6, 10 }, { "sus2", 2, 7, 0 },
    { "sus4", 5, 7, 0 },  { "5",   -1, 7, 0 },  { "aug",  4, 8, 0 },  { "6",    4, 7, 9 },
};
constexpr int kNumQualities = (int) (sizeof(kQualities) / sizeof(kQualities[0]));
enum { QMaj = 0, QMin = 1, Q7 = 2, QMaj7 = 3, QMin7 = 4 };

// CHORDS shapes: the five single frets, then every two-fret pair
//   neighbours = sevenths of the lower fret's chord,
//   stretches  = the chords the five frets can't make
struct ChordSlotDef { int mask; int root; int quality; };
constexpr ChordSlotDef kChordDefaults[GuitarService::kChordSlots] = {
    { 0x01, 0, QMaj },   // I
    { 0x02, 7, QMaj },   // V
    { 0x04, 9, QMin },   // vi
    { 0x08, 5, QMaj },   // IV
    { 0x10, 2, QMin },   // ii
    { 0x03, 0, QMaj7 },  // Imaj7
    { 0x06, 7, Q7 },     // V7
    { 0x0C, 9, QMin7 },  // vi m7
    { 0x18, 5, QMaj7 },  // IVmaj7
    { 0x05, 4, QMin },   // iii
    { 0x14, 4, QMaj },   // III (V/vi)
    { 0x0A, 10, QMaj },  // bVII
    { 0x09, 5, QMin },   // iv
    { 0x12, 8, QMaj },   // bVI
    { 0x11, 2, QMin },   // G+O: same as orange until someone maps it
};
constexpr int kSoloDefaults[GuitarService::kSoloSlots] = { 0, 2, 4, 7, 9, -12 };

int pentaAt(int i) { return 12 * floorDiv(i, 5) + kPentaDeg[i - 5 * floorDiv(i, 5)]; }

GuitarService::ControllerMap defaultWusbMap()
{
    GuitarService::ControllerMap m;
    for (int i = 0; i < 5; ++i)
        m.frets[i] = { 13, (uint8_t) (1 << i) };
    m.strumDown = { 13, 0x20 };
    m.strumUp = { 14, 0x01 };
    m.plusBtn = { 13, 0x40 };
    m.minusBtn = { 13, 0x80 };
    m.whammy = { 12, 220, 0 };
    m.stickX = { 2, 62, 12, 113 };
    m.stickY = { 4, 62, 12, 113 };
    return m;
}
} // namespace

// ============================== GuitarService ==============================

// ---------- fret editor tables ----------
int GuitarService::chordSlotMask(int slot)        { return kChordDefaults[juce::jlimit(0, kChordSlots - 1, slot)].mask; }
int GuitarService::defaultChordRoot(int slot)     { return kChordDefaults[juce::jlimit(0, kChordSlots - 1, slot)].root; }
int GuitarService::defaultChordQuality(int slot)  { return kChordDefaults[juce::jlimit(0, kChordSlots - 1, slot)].quality; }
int GuitarService::defaultSoloOffset(int slot)    { return kSoloDefaults[juce::jlimit(0, kSoloSlots - 1, slot)]; }
int GuitarService::numQualities()                 { return kNumQualities; }
juce::String GuitarService::qualitySuffix(int q)  { return kQualities[juce::jlimit(0, kNumQualities - 1, q)].suffix; }
juce::String GuitarService::noteName(int n)       { return ::noteName(n); }

juce::String GuitarService::chordName(int key, int root, int quality)
{
    return juce::String(noteNames[((key + root) % 12 + 12) % 12]) + qualitySuffix(quality);
}

// SOLO range: slide a note `steps` places along the key's major pentatonic
// (5 steps = an octave). The other in-key notes (4th, 7th) slide along the
// major scale by the same distance so they stay in key; out-of-key notes keep
// their distance above the pentatonic note below them.
int GuitarService::soloShift(int offset, int steps)
{
    if (steps == 0)
        return offset;
    const int pc = ((offset % 12) + 12) % 12;
    if (pc == 5 || pc == 11)
    {
        static const int kMajor[7] = { 0, 2, 4, 5, 7, 9, 11 };
        auto majAt = [](int j) { return 12 * floorDiv(j, 7) + kMajor[j - 7 * floorDiv(j, 7)]; };
        const int j = 7 * floorDiv(offset, 12) + (pc == 5 ? 3 : 6);
        const int dj = steps >= 0 ? (steps * 7 + 2) / 5 : -((-steps * 7 + 2) / 5);
        return majAt(j + dj);
    }
    int i = 5 * floorDiv(offset, 12);
    while (pentaAt(i + 1) <= offset) ++i;
    while (pentaAt(i) > offset) --i;
    return pentaAt(i + steps) + (offset - pentaAt(i));
}

int GuitarService::soloNoteWith(int slot, int off) const
{
    const int k = uiKey.load(), st = uiSoloStep.load();
    // the open note is the low anchor: it only moves by whole octaves
    const int n = slot == kSoloOpen ? 48 + k + off + 12 * floorDiv(st, 5)
                                    : 48 + k + soloShift(off, st);
    return juce::jlimit(0, 127, n);
}

void GuitarService::fillDefaults(int set)
{
    for (int i = 0; i < kChordSlots; ++i)
    {
        chordRootA[set][i] = kChordDefaults[i].root;
        chordQualA[set][i] = kChordDefaults[i].quality;
    }
    for (int i = 0; i < kSoloSlots; ++i)
        soloOffA[set][i] = kSoloDefaults[i];
}

void GuitarService::setKeyOwnBinds(int key, bool own)
{
    const int k = ((key % 12) + 12) % 12;
    if (own && ! keyOwn[k].load())
    {
        // start from what the key plays now: the shared set
        for (int i = 0; i < kChordSlots; ++i)
        {
            chordRootA[k][i] = chordRootA[kSharedSet][i].load();
            chordQualA[k][i] = chordQualA[kSharedSet][i].load();
        }
        for (int i = 0; i < kSoloSlots; ++i)
            soloOffA[k][i] = soloOffA[kSharedSet][i].load();
    }
    keyOwn[k] = own;
    saveRequest = true;
    notify();
}

void GuitarService::setChordSlot(int slot, int root, int quality)
{
    if (slot < 0 || slot >= kChordSlots) return;
    const int set = bindSet();
    chordRootA[set][slot] = ((root % 12) + 12) % 12;
    chordQualA[set][slot] = juce::jlimit(0, kNumQualities - 1, quality);
    saveRequest = true;
    notify();
}

void GuitarService::setSoloSlot(int slot, int offset)
{
    if (slot < 0 || slot >= kSoloSlots) return;
    soloOffA[bindSet()][slot] = juce::jlimit(-24, 36, offset);
    saveRequest = true;
    notify();
}

void GuitarService::resetChordSlots()
{
    const int set = bindSet();
    for (int i = 0; i < kChordSlots; ++i)
    {
        chordRootA[set][i] = kChordDefaults[i].root;
        chordQualA[set][i] = kChordDefaults[i].quality;
    }
    saveRequest = true;
    notify();
}

void GuitarService::resetSoloSlots()
{
    const int set = bindSet();
    for (int i = 0; i < kSoloSlots; ++i)
        soloOffA[set][i] = kSoloDefaults[i];
    saveRequest = true;
    notify();
}

GuitarService::GuitarService() : juce::Thread("gh-guitar-poll")
{
    for (int s = 0; s < 13; ++s)
        fillDefaults(s);
    for (auto& o : keyOwn)
        o = false;
    standalone = juce::JUCEApplicationBase::isStandaloneApp();
    saveRequest = false;
    map = defaultWusbMap();
    loadSettings();

    demoMode = std::getenv("GHMIDI_DEMO") != nullptr
               && juce::JUCEApplicationBase::isStandaloneApp();
    selfTest = std::getenv("GHMIDI_SELFTEST") != nullptr
               && juce::JUCEApplicationBase::isStandaloneApp();
    if (demoMode)
    {
        // headless verification (CI has no Windows machine to watch): milestones
        // go to a log next to the snapshots, and a crash leaves a backtrace there
        const char* dir = std::getenv("GHMIDI_HUDSNAP");
        if (dir == nullptr) dir = std::getenv("GHMIDI_SNAPSHOT");
        const auto logFile = (dir != nullptr ? juce::File(juce::String(dir))
                                             : juce::File::getSpecialLocation(juce::File::tempDirectory))
                                 .getChildFile("ghmidi_demo.log");
        logFile.deleteFile();
        demoLogger = std::make_unique<juce::FileLogger>(logFile, "GH MIDI demo mode");
        juce::Logger::setCurrentLogger(demoLogger.get());
        juce::SystemStats::setApplicationCrashHandler([](void*)
        {
            juce::Logger::writeToLog("CRASH\n" + juce::SystemStats::getStackBacktrace());
        });
        juce::Logger::writeToLog("service: " + juce::SystemStats::getOperatingSystemName() + ", exe "
                                 + juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName());
    }

    const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                         .getFileNameWithoutExtension();
    if (demoMode || selfTest
        || (! exe.containsIgnoreCase("juce_vst3_helper") && ! exe.containsIgnoreCase("auval")))
        startThread();
}

GuitarService::~GuitarService()
{
    stopThread(-1);  // never force-kill: hidapi teardown must finish on its thread
    if (demoLogger != nullptr)
    {
        juce::Logger::writeToLog("service: shutdown");
        juce::Logger::setCurrentLogger(nullptr);
    }
}

// ---------- settings persistence ----------
juce::File GuitarService::settingsFile()
{
    // macOS: ~/Library/Application Support/GH MIDI
    // Windows: %APPDATA%\GH MIDI  ·  Linux: ~/.config/GH MIDI
#if JUCE_MAC
    const char* sub = "Application Support/GH MIDI/settings.json";
#else
    const char* sub = "GH MIDI/settings.json";
#endif
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile(sub);
}

static void varToMap(const juce::var& v, GuitarService::ControllerMap& m)
{
    auto btn = [&](const char* k, GuitarService::ButtonMap& b)
    {
        if (auto* a = v[k].getArray(); a != nullptr && a->size() >= 2)
            b = { (int) (*a)[0], (uint8_t) (int) (*a)[1] };
    };
    btn("fretG", m.frets[0]); btn("fretR", m.frets[1]); btn("fretY", m.frets[2]);
    btn("fretB", m.frets[3]); btn("fretO", m.frets[4]);
    btn("strumDown", m.strumDown); btn("strumUp", m.strumUp);
    btn("plus", m.plusBtn); btn("minus", m.minusBtn);
    if (auto* a = v["whammy"].getArray(); a != nullptr && a->size() >= 3)
        m.whammy = { (int) (*a)[0], (int) (*a)[1], (int) (*a)[2] };
    auto stick = [&](const char* k, GuitarService::StickMap& sm)
    {
        if (auto* a = v[k].getArray(); a != nullptr && a->size() >= 4)
            sm = { (int) (*a)[0], (int) (*a)[1], (int) (*a)[2], (int) (*a)[3] };
    };
    stick("stickX", m.stickX);
    stick("stickY", m.stickY);
}

static juce::var mapToVar(const GuitarService::ControllerMap& m)
{
    auto* o = new juce::DynamicObject();
    auto btn = [&](const char* k, const GuitarService::ButtonMap& b)
    {
        o->setProperty(k, juce::Array<juce::var> { b.byteIdx, (int) b.mask });
    };
    btn("fretG", m.frets[0]); btn("fretR", m.frets[1]); btn("fretY", m.frets[2]);
    btn("fretB", m.frets[3]); btn("fretO", m.frets[4]);
    btn("strumDown", m.strumDown); btn("strumUp", m.strumUp);
    btn("plus", m.plusBtn); btn("minus", m.minusBtn);
    o->setProperty("whammy", juce::Array<juce::var> { m.whammy.byteIdx, m.whammy.rest, m.whammy.extreme });
    o->setProperty("stickX", juce::Array<juce::var> { m.stickX.byteIdx, m.stickX.center, m.stickX.lo, m.stickX.hi });
    o->setProperty("stickY", juce::Array<juce::var> { m.stickY.byteIdx, m.stickY.center, m.stickY.lo, m.stickY.hi });
    return juce::var(o);
}

juce::String GuitarService::deviceKey() const
{
    return "dev_" + juce::String::toHexString(targetVid.load())
         + "_" + juce::String::toHexString(targetPid.load());
}

void GuitarService::applyMapForDevice()
{
    // known guitar ships pre-mapped; unknown controllers start blank (LEARN)
    ControllerMap m;
    if (targetVid.load() == 0x289B && targetPid.load() == 0x0080)
        m = defaultWusbMap();
    if (auto* o = controllersVar.getDynamicObject())
    {
        const juce::Identifier k(deviceKey());
        if (o->hasProperty(k))
            varToMap(o->getProperty(k), m);
    }
    {
        const juce::ScopedLock sl(mapLock);
        map = m;
    }
    ++mapVersion;
}

void GuitarService::loadSettings()
{
    const auto v = juce::JSON::parse(settingsFile());
    controllersVar = juce::var(new juce::DynamicObject());
    if (v.isObject())
    {
        auto get = [&](const char* k, int fallback) { return v.hasProperty(k) ? (int) v[k] : fallback; };
        targetVid = get("vid", targetVid.load());
        targetPid = get("pid", targetPid.load());
        hammerOn = get("hammer", 0) != 0;
        whammyMode = juce::jlimit(0, 2, get("whammyMode", 0));
        virtualMidiOn = get("virtualMidi", 1) != 0;
        strumSustain = get("strumSustain", 0) != 0;
        strumRollMs = juce::jlimit(0, 50, get("strumRoll", 10));
        lefty = get("lefty", 0) != 0;
        palmWhammy = get("palmWhammy", 0) != 0;
        // binds: "chords"/"solo" = the shared set; "keyBinds" = keys with their own
        auto readSet = [&](const juce::var& ch, const juce::var& so, int set)
        {
            if (auto* a = ch.getArray(); a != nullptr && a->size() == kChordSlots)
                for (int i = 0; i < kChordSlots; ++i)
                    if (auto* e = (*a)[i].getArray(); e != nullptr && e->size() >= 2)
                    {
                        chordRootA[set][i] = (((int) (*e)[0]) % 12 + 12) % 12;
                        chordQualA[set][i] = juce::jlimit(0, kNumQualities - 1, (int) (*e)[1]);
                    }
            if (auto* a = so.getArray(); a != nullptr && a->size() == kSoloSlots)
                for (int i = 0; i < kSoloSlots; ++i)
                    soloOffA[set][i] = juce::jlimit(-24, 36, (int) (*a)[i]);
        };
        readSet(v["chords"], v["solo"], kSharedSet);
        if (auto* kb = v["keyBinds"].getDynamicObject())
            for (int k = 0; k < 12; ++k)
                if (kb->hasProperty(juce::String(k)))
                {
                    const auto e = kb->getProperty(juce::String(k));
                    keyOwn[k] = true;
                    readSet(e["chords"], e["solo"], k);
                }
        {
            const juce::ScopedLock sl(midiOutLock);
            midiOutId = v["midiOut"].toString();
            midiOutName = v["midiOutName"].toString();
        }
        if (v["controllers"].isObject())
            controllersVar = v["controllers"];
        else if (v.hasProperty("fretG"))
        {
            // migrate the old single-controller format
            ControllerMap tmp = defaultWusbMap();
            varToMap(v, tmp);
            controllersVar.getDynamicObject()->setProperty(juce::Identifier(deviceKey()),
                                                           mapToVar(tmp));
        }
    }
    applyMapForDevice();
}

void GuitarService::saveSettings()
{
    {
        ControllerMap snap;
        {
            const juce::ScopedLock sl(mapLock);
            snap = map;
        }
        if (auto* o = controllersVar.getDynamicObject())
            o->setProperty(juce::Identifier(deviceKey()), mapToVar(snap));
    }
    auto* o = new juce::DynamicObject();
    o->setProperty("vid", targetVid.load());
    o->setProperty("pid", targetPid.load());
    o->setProperty("hammer", hammerOn.load() ? 1 : 0);
    o->setProperty("whammyMode", whammyMode.load());
    o->setProperty("virtualMidi", virtualMidiOn.load() ? 1 : 0);
    o->setProperty("strumSustain", strumSustain.load() ? 1 : 0);
    o->setProperty("strumRoll", strumRollMs.load());
    o->setProperty("lefty", lefty.load() ? 1 : 0);
    o->setProperty("palmWhammy", palmWhammy.load() ? 1 : 0);
    {
        auto writeSet = [&](juce::DynamicObject* dst, int set)
        {
            juce::Array<juce::var> ch, so;
            for (int i = 0; i < kChordSlots; ++i)
                ch.add(juce::Array<juce::var> { chordRootA[set][i].load(), chordQualA[set][i].load() });
            for (int i = 0; i < kSoloSlots; ++i)
                so.add(soloOffA[set][i].load());
            dst->setProperty("chords", ch);
            dst->setProperty("solo", so);
        };
        writeSet(o, kSharedSet);
        auto* kb = new juce::DynamicObject();
        for (int k = 0; k < 12; ++k)
            if (keyOwn[k].load())
            {
                auto* e = new juce::DynamicObject();
                writeSet(e, k);
                kb->setProperty(juce::String(k), juce::var(e));
            }
        o->setProperty("keyBinds", juce::var(kb));
    }
    {
        const juce::ScopedLock sl(midiOutLock);
        o->setProperty("midiOut", midiOutId);
        o->setProperty("midiOutName", midiOutName);
    }
    o->setProperty("controllers", controllersVar);

    auto f = settingsFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText(juce::JSON::toString(juce::var(o)));
}

// ---------- device scan ----------
void GuitarService::scanDevices()
{
    juce::Array<DeviceInfo> found;
    if (auto* list = hid_enumerate(0, 0))
    {
        for (auto* i = list; i != nullptr; i = i->next)
        {
            const bool gamepad = i->usage_page == 1 && (i->usage == 4 || i->usage == 5);
            if (! gamepad)
                continue;
            DeviceInfo d;
            d.vid = i->vendor_id;
            d.pid = i->product_id;
            juce::String name;
            if (i->manufacturer_string != nullptr) name << juce::String(i->manufacturer_string) << " ";
            if (i->product_string != nullptr) name << juce::String(i->product_string);
            d.label = name.trim().isEmpty() ? juce::String::toHexString(d.vid) + ":" + juce::String::toHexString(d.pid)
                                            : name.trim();
            bool dup = false;
            for (auto& e : found)
                if (e.vid == d.vid && e.pid == d.pid) { dup = true; break; }
            if (! dup)
                found.add(d);
        }
        hid_free_enumeration(list);
    }
    {
        const juce::ScopedLock sl(deviceLock);
        devices = found;
    }
    ++deviceListVersion;
}

// ---------- demo (standalone UI development only) ----------
void GuitarService::demoRun()
{
    struct Ev { int mask; bool legato; double hold; double gap; const char* name; };
    static const Ev seq[] = {
        { 0x01, false, 0.55, 0.16, "C"  }, { 0x04, true,  0.34, 0.10, "Am" },
        { 0x08, false, 0.70, 0.18, "F"  }, { 0x02, false, 0.30, 0.10, "G"  },
        { 0x00, false, 0.16, 0.10, "C2" }, { 0x00, false, 0.16, 0.12, "C2" },
        { 0x12, false, 0.85, 0.20, "D5" }, { 0x05, true,  0.40, 0.14, "E"  },
        { 0x10, false, 0.30, 0.10, "Dm" }, { 0x18, true,  0.55, 0.30, "F5" },
    };
    guitarFound = true;
    int i = 0;
    while (! threadShouldExit())
    {
        const auto& e = seq[i % (int) (sizeof(seq) / sizeof(seq[0]))];
        ++i;
        uiFretBits = e.mask;
        beginGem(e.mask, e.legato);
        announce(e.name);
        for (double t = 0.0; t < e.hold && ! threadShouldExit(); t += 0.016)
        {
            uiWhammy = e.hold > 0.6 ? (float) (0.5 - 0.5 * std::cos(t * 9.0)) * 0.7f : 0.0f;
            wait(16);
        }
        endGem();
        uiWhammy = 0.0f;
        uiFretBits = 0;
        for (double t = 0.0; t < e.gap && ! threadShouldExit(); t += 0.016)
            wait(16);
    }
}

// ---------- self-test (dev only): drive step() with synthetic WUSBMote reports ----------
void GuitarService::selfTestRun()
{
    map = defaultWusbMap();
    key = 0; uiKey = 0;
    strumRollMs = 10;
    strumSustain = false;
    lefty = false;
    palmWhammy = false;
    virtualMidiOn = false;
    for (int s = 0; s < 13; ++s) fillDefaults(s);
    for (auto& o : keyOwn) o = false;
    uint8_t r[16] {};
    auto setIdle = [&] { std::memset(r, 0, sizeof(r)); r[12] = 220; r[2] = 62; r[4] = 62; };
    auto run = [&](double secs)
    {
        const double end = juce::Time::getMillisecondCounterHiRes() * 0.001 + secs;
        while (juce::Time::getMillisecondCounterHiRes() * 0.001 < end)
        {
            step(r, 16);
            juce::Thread::sleep(1);
        }
    };
    auto scene = [&](const juce::String& name)
    {
        setIdle();
        run(0.05);
        selfTestLog << "\n== " << name << " ==\n";
        selfTestT0 = juce::Time::getMillisecondCounterHiRes() * 0.001;
    };
    auto strumDown = [&](double hold) { r[13] |= 0x20; selfTestLog << "  (strum down at " << juce::String((juce::Time::getMillisecondCounterHiRes() * 0.001 - selfTestT0) * 1000.0, 1) << " ms)\n"; run(hold); r[13] &= (uint8_t) ~0x20; };

    scene("CHORDS green, down strum, 10 ms roll");
    r[13] = 0x01; run(0.02); strumDown(0.05); run(0.15); r[13] = 0; run(0.05);

    scene("CHORDS orange remapped to Em");
    setChordSlot(4, 4, 1);
    r[13] = 0x10; run(0.02); strumDown(0.05); run(0.1); r[13] = 0; run(0.05);

    scene("CHORDS G+O shape (slot 14) remapped to D7");
    setChordSlot(14, 2, 2);
    r[13] = 0x11; run(0.02); strumDown(0.05); run(0.1); r[13] = 0; run(0.05);

    scene("CHORDS 3-fret shape G+R+Y falls back to yellow");
    r[13] = 0x07; run(0.02); strumDown(0.05); run(0.1); r[13] = 0; run(0.05);

    scene("PALM MUTE via whammy: red held 400 ms, expect choke ~130 ms");
    palmWhammy = true;
    r[12] = 0; r[13] = 0x02; run(0.03); strumDown(0.05); run(0.35); r[13] = 0; r[12] = 220; run(0.05);
    palmWhammy = false;

    scene("roll 0 ms: all chord notes at once");
    strumRollMs = 0;
    r[13] = 0x08; run(0.02); strumDown(0.05); run(0.05); r[13] = 0; run(0.05);
    strumRollMs = 10;

    scene("LEFTY: physical down strum = up strum (vel 78, high->low)");
    lefty = true;
    r[13] = 0x01; run(0.02); strumDown(0.05); run(0.1); r[13] = 0; run(0.05);
    lefty = false;

    scene("SOLO: minus x2, range +1 step, green+yellow down strum");
    r[13] = 0x80; run(0.03); r[13] = 0; run(0.03); r[13] = 0x80; run(0.03); r[13] = 0; run(0.03);
    shiftOctave(1);
    selfTestLog << "  mode " << mode << " soloStep " << soloStep << " green " << noteName(soloNote(0)) << "\n";
    r[13] = 0x05; run(0.02); strumDown(0.05); run(0.05);
    r[13] = 0x04; run(0.05);   // lift green: only its note stops
    r[13] = 0; run(0.05);

    scene("SOLO: range walk, green fret note at each step");
    for (int st = -3; st <= 6; ++st)
    {
        soloStep = st; uiSoloStep = st;
        selfTestLog << "  step " << st << ": ";
        for (int i = 0; i < 6; ++i) selfTestLog << noteName(soloNote(i)) << " ";
        selfTestLog << "\n";
    }
    setSoloSlot(4, 5);   // orange -> the 4th (F in C)
    selfTestLog << "  orange=4th: ";
    for (int st = 0; st <= 5; ++st) { soloStep = st; uiSoloStep = st; selfTestLog << noteName(soloNote(4)) << " "; }
    selfTestLog << "\n";

    if (auto* path = std::getenv("GHMIDI_SELFTEST"))
        juce::File(juce::String(path)).replaceWithText(selfTestLog);
    juce::MessageManager::callAsync([] { juce::JUCEApplicationBase::quit(); });
}

// ---------- MIDI plumbing ----------
void GuitarService::addClient(MidiInbox* c)
{
    const juce::ScopedLock sl(clientLock);
    clients.addIfNotAlreadyThere(c);
}

void GuitarService::removeClient(MidiInbox* c)
{
    const juce::ScopedLock sl(clientLock);
    clients.removeAllInstancesOf(c);
}

void GuitarService::sendMsg(const juce::MidiMessage& m)
{
    auto msg = m;
    msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    if (selfTest)
        selfTestLog << juce::String((msg.getTimeStamp() - selfTestT0) * 1000.0, 1) << " ms  "
                    << msg.getDescription() << "\n";
    {
        const juce::ScopedLock sl(clientLock);
        for (auto* c : clients)
            c->push(msg);
    }
    // "GH MIDI" virtual source: lets the DAW record the performance as notes
    if ((standalone || virtualMidiOn.load()) && virtualOut != nullptr)
        virtualOut->sendMessageNow(msg);
    // user-picked port (Windows: loopMIDI or a real synth)
    if (portOut != nullptr)
        portOut->sendMessageNow(msg);
}

// ---------- picked MIDI output port ----------
void GuitarService::selectMidiOutput(const juce::String& identifier, const juce::String& name)
{
    {
        const juce::ScopedLock sl(midiOutLock);
        midiOutId = identifier;
        midiOutName = name;
    }
    midiOutRequest = true;
    saveRequest = true;
    notify();
}

// guitar thread only. Finds the saved port by identifier first, then by name
// (identifiers can shift when devices are re-plugged on Windows).
void GuitarService::openMidiOutput()
{
    juce::String id, name;
    {
        const juce::ScopedLock sl(midiOutLock);
        id = midiOutId;
        name = midiOutName;
    }
    if (portOut != nullptr)
        allOff();   // don't leave notes hanging on the old port
    portOut.reset();
    if (id.isEmpty() && name.isEmpty())
        return;
    const auto outs = juce::MidiOutput::getAvailableDevices();
    for (const auto& d : outs)
        if (d.identifier == id)
        {
            portOut = juce::MidiOutput::openDevice(d.identifier);
            break;
        }
    if (portOut == nullptr && name.isNotEmpty())
        for (const auto& d : outs)
            if (d.name == name)
            {
                portOut = juce::MidiOutput::openDevice(d.identifier);
                break;
            }
}

void GuitarService::noteOn(int note, int vel)
{
    if (note < 0 || note > 127)
        return;
    sendMsg(juce::MidiMessage::noteOn(1, note, (juce::uint8) vel));
    ringing.add(note);
}

void GuitarService::scheduleOn(int note, int vel, double due)
{
    if (note < 0 || note > 127)
        return;
    pending.add({ due, note, vel });
}

void GuitarService::releaseNote(int note)
{
    for (int i = pending.size(); --i >= 0;)
        if (pending.getReference(i).note == note)
        {
            pending.remove(i);
            return;
        }
    if (ringing.contains(note))
    {
        sendMsg(juce::MidiMessage::noteOff(1, note));
        ringing.removeValue(note);
    }
}

void GuitarService::flushPending(double now)
{
    for (int i = 0; i < pending.size();)
    {
        const auto p = pending.getReference(i);
        if (now >= p.due)
        {
            pending.remove(i);
            noteOn(p.note, p.vel);
        }
        else
            ++i;
    }
}

void GuitarService::allOff()
{
    pending.clearQuick();
    palmCutAt = -1.0;
    for (int n : ringing)
        sendMsg(juce::MidiMessage::noteOff(1, n));
    ringing.clear();
    for (auto& sn : soloNotes)
        sn = -1;
    chartHeld = 0;
    endGem();
}

void GuitarService::beginGem(int mask, bool legato)
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const juce::ScopedLock sl(gemLock);
    if (! gems.isEmpty() && gems.getReference(gems.size() - 1).t1 < 0)
        gems.getReference(gems.size() - 1).t1 = now;
    Gem g;
    g.mask = mask;
    g.t0 = now;
    g.legato = legato;
    gems.add(g);
    while (gems.size() > 64)
        gems.remove(0);
}

void GuitarService::endGem()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const juce::ScopedLock sl(gemLock);
    if (! gems.isEmpty() && gems.getReference(gems.size() - 1).t1 < 0)
        gems.getReference(gems.size() - 1).t1 = now;
}

void GuitarService::announce(const juce::String& s)
{
    {
        const juce::ScopedLock sl(labelLock);
        lastPlayed = s;
    }
    lastPlayedFrets = 0;
    lastPlayedAt = juce::Time::getMillisecondCounterHiRes() * 0.001;
}

void GuitarService::announceFrets(int mask)
{
    juce::String s;
    for (int i = 0; i < 5; ++i)
        if (mask & (1 << i))
            s << "GRYBO"[i];
    {
        const juce::ScopedLock sl(labelLock);
        lastPlayed = s;
    }
    lastPlayedFrets = mask;
    lastPlayedAt = juce::Time::getMillisecondCounterHiRes() * 0.001;
}

// ---------- main loop ----------
void GuitarService::run()
{
    if (demoMode)
    {
        demoRun();
        return;
    }
    if (selfTest)
    {
        selfTestRun();
        return;
    }

    uint8_t buf[64];
    int shorts = 0, polls = 0;
#if JUCE_WINDOWS
    timeBeginPeriod(1);
#endif
    hid_init();
#if JUCE_MAC || JUCE_LINUX || JUCE_IOS
    // createNewDevice only exists on these platforms (Windows has no virtual
    // MIDI ports); a preprocessor guard, because even a discarded
    // `if constexpr` branch has to name-resolve.
    virtualOut = juce::MidiOutput::createNewDevice("GH MIDI");
#endif
    openMidiOutput();

    while (! threadShouldExit())
    {
        const int mReq = modeRequest.exchange(-1);
        if (mReq >= 0) { mode = mReq % 4; uiMode = mode; }
        const int kReq = keyRequest.exchange(-1);
        if (kReq >= 0) { key = kReq % 12; uiKey = key; }
        if (const int d = modeNudge.exchange(0); d != 0)   cycleMode(d);
        if (const int d = keyNudge.exchange(0); d != 0)    shiftKey(d);
        if (const int d = octaveNudge.exchange(0); d != 0) shiftOctave(d);
        if (const int d = strumNudge.exchange(0); d != 0)
            setStrum(juce::jlimit(0, kStrumMaxMs, strumRollMs.load() + d * kStrumStepMs));

        if (midiOutRequest.exchange(false))
            openMidiOutput();
        if (settingsResetRequest.exchange(false))
        {
            if (auto* o = controllersVar.getDynamicObject())
                o->removeProperty(juce::Identifier(deviceKey()));
            applyMapForDevice();   // known guitar: factory map; others: blank
            lefty = false;
            palmWhammy = false;
            virtualMidiOn = true;
            allOff();
            saveRequest = true;
        }
        if (saveRequest.exchange(false))
            saveSettings();
        if (scanRequest.exchange(false))
            scanDevices();
        if (reconnectRequest.exchange(false))
        {
            if (dev != nullptr)
            {
                allOff();
                hid_close(dev);
                dev = nullptr;
                guitarFound = false;
            }
            applyMapForDevice();   // each controller keeps its own setup
        }

        if (dev == nullptr)
        {
            dev = hid_open((unsigned short) targetVid.load(), (unsigned short) targetPid.load(), nullptr);
            guitarFound = dev != nullptr;
            if (dev == nullptr)
            {
                wait(1000);
                continue;
            }
            shorts = polls = 0;
            lastLen = 0;
            // probe how this device likes to be read
            ioMode = 0;
            for (int rid = 1; rid >= 0 && ioMode == 0; --rid)
                for (int tries = 0; tries < 3; ++tries)
                {
                    buf[0] = (uint8_t) rid;
                    if (hid_get_input_report(dev, buf, sizeof(buf)) >= 2)
                    {
                        ioMode = rid == 1 ? 1 : 2;
                        break;
                    }
                }
            if (ioMode == 0)
            {
                ioMode = 3;  // interrupt-report stream
                hid_set_nonblocking(dev, 1);
            }
        }

        int got = 0;
        if (ioMode == 3)
        {
            const int r = hid_read_timeout(dev, buf, sizeof(buf), 1);
            if (r < 0)
            {
                hid_close(dev);
                dev = nullptr;
                guitarFound = false;
                continue;
            }
            if (r > 0)
            {
                std::memcpy(lastBuf, buf, (size_t) juce::jmin(r, 64));
                lastLen = juce::jmin(r, 64);
            }
            got = lastLen;
            if (got > 0)
                std::memcpy(buf, lastBuf, (size_t) got);
        }
        else
        {
            buf[0] = ioMode == 1 ? 0x01 : 0x00;
            got = hid_get_input_report(dev, buf, sizeof(buf));
            if (got < 0)
            {
                hid_close(dev);
                dev = nullptr;
                guitarFound = false;
                continue;
            }
        }

        ++polls;
        if (got < 2)
        {
            ++shorts;
            adapterEmpty = polls > 1000 && shorts > polls - 200;
            wait(4);   // nothing to read: no need to hammer the adapter
            continue;
        }
        adapterEmpty = false;

        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (learnT0req.exchange(false))
        {
            allOff();
            ringFret = ringCombo = candCombo = -1;
            learnPhase = 0;
            learnByte = -1;
            learnT0 = now;
        }
        if (learnTarget.load() >= 0)
            learnTick(buf, got, now);
        else
            step(buf, got);
        // ~1 ms between polls (was 4): the guitar is read up to ~1000x a
        // second. A stream device (ioMode 3) already blocked in hid_read.
        if (ioMode != 3)
            wait(1);
    }
    allOff();
    if (saveRequest.exchange(false))
        saveSettings();   // a change made just before quitting still lands
    virtualOut.reset();
    portOut.reset();
    if (dev != nullptr)
    {
        hid_close(dev);
        dev = nullptr;
    }
    hid_exit();  // on this thread, while its run loop still exists
#if JUCE_WINDOWS
    timeEndPeriod(1);
#endif
}

// ---------- per-control mapping ----------
juce::String GuitarService::targetName(int t)
{
    static const char* names[] = { "GREEN FRET", "RED FRET", "YELLOW FRET", "BLUE FRET",
                                   "ORANGE FRET", "STRUM DOWN", "STRUM UP", "PLUS BUTTON",
                                   "MINUS BUTTON", "WHAMMY", "JOYSTICK LEFT/RIGHT", "JOYSTICK UP/DOWN" };
    return t >= 0 && t < LTargetCount ? names[t] : juce::String();
}

static GuitarService::ButtonMap* buttonSlot(GuitarService::ControllerMap& m, int t)
{
    switch (t)
    {
        case 0: case 1: case 2: case 3: case 4: return &m.frets[t];
        case 5: return &m.strumDown;
        case 6: return &m.strumUp;
        case 7: return &m.plusBtn;
        case 8: return &m.minusBtn;
        default: return nullptr;
    }
}

juce::String GuitarService::describeMapping(int t) const
{
    const juce::ScopedLock sl(mapLock);
    auto& mm = const_cast<ControllerMap&> (map);
    if (auto* b = buttonSlot(mm, t))
    {
        if (! b->valid())
            return "-";
        int bit = 0;
        for (int i = 0; i < 8; ++i)
            if (b->mask & (1 << i)) { bit = i; break; }
        return "byte " + juce::String(b->byteIdx) + " / bit " + juce::String(bit);
    }
    if (t == LWhammy)
        return map.whammy.valid() ? "byte " + juce::String(map.whammy.byteIdx) + " / axis" : "-";
    if (t == LStickX)
        return map.stickX.valid() ? "byte " + juce::String(map.stickX.byteIdx) + " / axis" : "-";
    if (t == LStickY)
        return map.stickY.valid() ? "byte " + juce::String(map.stickY.byteIdx) + " / axis" : "-";
    return "-";
}

void GuitarService::clearMapping(int t)
{
    {
        const juce::ScopedLock sl(mapLock);
        if (auto* b = buttonSlot(map, t))
            *b = {};
        else if (t == LWhammy)
            map.whammy = {};
        else if (t == LStickX)
            map.stickX = {};
        else if (t == LStickY)
            map.stickY = {};
    }
    ++mapVersion;
    saveRequest = true;
    notify();
}

void GuitarService::learnTick(const uint8_t* d, int len, double now)
{
    const int t = learnTarget.load();
    if (t < 0)
        return;
    const int n = juce::jmin(len, 64);

    if (learnPhase == 0)  // settle, then take a baseline
    {
        if (now - learnT0 > 0.6)
        {
            learnBaseLen = n;
            std::memcpy(learnBase, d, (size_t) n);
            learnPhase = 1;
        }
        return;
    }
    if (now - learnT0 > 12.0)  // nothing happened: give up quietly
    {
        learnTarget = -1;
        return;
    }

    const int nb = juce::jmin(n, learnBaseLen);
    auto isButtonByte = [&](int i)
    {
        for (auto& b : map.frets)
            if (b.byteIdx == i) return true;
        return map.strumDown.byteIdx == i || map.strumUp.byteIdx == i
            || map.plusBtn.byteIdx == i || map.minusBtn.byteIdx == i;
    };

    if (t <= LMinus)  // buttons: first stable change binds
    {
        if (learnPhase == 1)
        {
            for (int i = 0; i < nb; ++i)
                if (d[i] != learnBase[i])
                {
                    learnByte = i;
                    learnMask = (uint8_t) (d[i] ^ learnBase[i]);
                    learnChangeAt = now;
                    learnPhase = 2;
                    break;
                }
        }
        else
        {
            if (learnByte < len && (uint8_t) (d[learnByte] ^ learnBase[learnByte]) == learnMask)
            {
                if (now - learnChangeAt > 0.09)
                {
                    {
                        const juce::ScopedLock sl(mapLock);
                        if (auto* b = buttonSlot(map, t))
                            *b = { learnByte, learnMask };
                    }
                    ++mapVersion;
                    saveRequest = true;
                    learnTarget = -1;
                }
            }
            else
                learnPhase = 1;
        }
        return;
    }

    // axes: watch a sweep, bind the byte that travelled furthest
    if (learnPhase == 1)
    {
        for (int i = 0; i < nb; ++i)
            if (std::abs((int) d[i] - (int) learnBase[i]) > 14 && ! isButtonByte(i))
            {
                for (int j = 0; j < 64; ++j)
                {
                    axMin[j] = j < len ? d[j] : 255;
                    axMax[j] = j < len ? d[j] : 0;
                }
                axStart = now;
                learnPhase = 2;
                break;
            }
        return;
    }
    for (int i = 0; i < n; ++i)
    {
        axMin[i] = juce::jmin(axMin[i], (int) d[i]);
        axMax[i] = juce::jmax(axMax[i], (int) d[i]);
    }
    if (now - axStart > 2.5)
    {
        int best = -1, bestSpan = 19;
        for (int i = 0; i < nb; ++i)
        {
            const int span = axMax[i] - axMin[i];
            if (span > bestSpan && ! isButtonByte(i))
            {
                best = i;
                bestSpan = span;
            }
        }
        if (best >= 0)
        {
            const juce::ScopedLock sl(mapLock);
            if (t == LWhammy)
            {
                const int rest = d[best];
                const int extreme = std::abs(axMin[best] - rest) > std::abs(axMax[best] - rest)
                                        ? axMin[best] : axMax[best];
                map.whammy = { best, rest, extreme };
            }
            else if (t == LStickX)
                map.stickX = { best, (int) d[best], axMin[best], axMax[best] };
            else
                map.stickY = { best, (int) d[best], axMin[best], axMax[best] };
        }
        ++mapVersion;
        saveRequest = true;
        learnTarget = -1;
    }
}

// ---------- mode / key / octave (guitar thread only) ----------
void GuitarService::cycleMode(int dir)
{
    allOff();
    ringFret = ringCombo = candCombo = -1;
    mode = ((mode + dir) % 4 + 4) % 4;
    uiMode = mode;
    announce(mode == Easy ? "CHORDS" : mode == Real ? "NOTES" : mode == Penta ? "SOLO" : "CHART");
}

void GuitarService::shiftKey(int d)
{
    key = ((key + d) % 12 + 12) % 12;
    uiKey = key;
    announce(mode == Real ? "base " + noteName(kRealBase + key + octaveReal)
                          : "key " + juce::String(noteNames[key]));
}

// octave offsets run -3..+3 in every mode: that spans the piano and keeps
// every mode's highest/lowest note inside MIDI 0..127
void GuitarService::shiftOctave(int d)
{
    if (mode == Penta)
    {
        // SOLO: the frets slide one scale note at a time (5 steps = an octave)
        soloStep = juce::jlimit(kSoloStepMin, kSoloStepMax, soloStep + d);
        uiSoloStep = soloStep;
        announce("green " + noteName(soloNote(0)));
    }
    else if (mode != Easy)
    {
        octaveReal = juce::jlimit(kOctaveMin, kOctaveMax, octaveReal + d * 12);
        uiOctave = octaveReal;
        announce("base " + noteName(kRealBase + key + octaveReal));
    }
    else
    {
        easyOct = juce::jlimit(kOctaveMin, kOctaveMax, easyOct + d * 12);
        uiEasyOct = easyOct;
        const int o = easyOct / 12;
        announce("octave " + (o > 0 ? "+" + juce::String(o) : juce::String(o)));
    }
}

void GuitarService::setStrum(int ms)
{
    strumRollMs = ms;
    announce("strum " + juce::String(ms) + "ms");
    saveRequest = true;
}

// ---------- the instrument ----------
void GuitarService::step(const uint8_t* d, int len)
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    ControllerMap m;
    {
        const juce::ScopedLock sl(mapLock);
        m = map;
    }

    // rolled chord notes that are due, then a palm-muted strum's choke
    flushPending(now);
    if (palmCutAt >= 0.0 && now >= palmCutAt)
    {
        allOff();
        ringFret = ringCombo = -1;
    }

    auto pressed = [&](const ButtonMap& b)
    {
        return b.valid() && b.byteIdx < len && (d[b.byteIdx] & b.mask) != 0;
    };

    int combo = 0;
    for (int i = 0; i < 5; ++i)
        if (pressed(m.frets[i]))
            combo |= 1 << i;
    bool down = pressed(m.strumDown);
    bool up = pressed(m.strumUp);
    const bool strum = down || up;
    const bool plusB = pressed(m.plusBtn);
    const bool minusB = pressed(m.minusBtn);
    const bool flip = lefty.load();

    uiFretBits = combo;

    // minus: cycle mode
    if (minusB && ! prevMinus)
        cycleMode(1);
    prevMinus = minusB;

    // plus: tap through strum speeds (wraps back to 0)
    if (plusB && ! prevPlus)
    {
        int roll = strumRollMs.load() + kStrumStepMs;
        if (roll > kStrumMaxMs)
            roll = 0;
        setStrum(roll);
    }
    prevPlus = plusB;

    // live control-test bits (settings panel highlights) -- the physical
    // controls, before lefty flip swaps anything
    {
        int bits = combo;
        if (down) bits |= 1 << LStrumDown;
        if (up) bits |= 1 << LStrumUp;
        if (plusB) bits |= 1 << LPlus;
        if (minusB) bits |= 1 << LMinus;
        if (m.stickX.valid() && m.stickX.byteIdx < len
            && std::abs((int) d[m.stickX.byteIdx] - m.stickX.center) > (m.stickX.hi - m.stickX.lo) / 5)
            bits |= 1 << LStickX;
        if (m.stickY.valid() && m.stickY.byteIdx < len
            && std::abs((int) d[m.stickY.byteIdx] - m.stickY.center) > (m.stickY.hi - m.stickY.lo) / 5)
            bits |= 1 << LStickY;
        uiButtonBits = bits;   // whammy bit added below
    }

    // lefty flip: the guitar is turned over, so the bar's down stroke is the
    // old up stroke
    if (flip)
        std::swap(down, up);

    // whammy -> pitch bend (+ CC20), or palm mute
    const bool palmMode = palmWhammy.load();
    float wamt = 0.0f;
    if (m.whammy.valid() && m.whammy.byteIdx < len)
    {
        const int v = d[m.whammy.byteIdx];
        float amt = (float) (v - m.whammy.rest) / (float) (m.whammy.extreme - m.whammy.rest);
        amt = juce::jlimit(0.0f, 1.0f, amt);
        if (amt < 0.03f)
            amt = 0.0f;
        wamt = amt;
        uiWhammy = palmMode ? 0.0f : amt;   // the highway wiggle is the bend
        if (amt > 0.05f)
            uiButtonBits = uiButtonBits.load() | (1 << LWhammy);
        // violent whammy motion shakes the strum bar into real contact
        // bounces -- flag the window so strum edges are ignored during it.
        // Measured over >= 4 ms so the faster poll rate doesn't hide it.
        // (Palm mute holds the whammy still, and a strum right after
        // pressing it must count.)
        if (now - prevWhamAt >= 0.004)
        {
            if (! palmMode && std::abs(amt - prevWham) > 0.04f)
                whamBusyUntil = now + 0.12;
            prevWham = amt;
            prevWhamAt = now;
        }
        const int pb = palmMode ? 8192 : 8192 - (int) (8191.0f * amt);
        if (pb != bend && (std::abs(pb - bend) > 64 || pb == 8192))
        {
            const int wm = whammyMode.load();
            if (wm != 2)
                sendMsg(juce::MidiMessage::pitchWheel(1, pb));
            if (wm != 1 && ! palmMode)
                sendMsg(juce::MidiMessage::controllerEvent(1, 20, juce::roundToInt(amt * 127.0f)));
            bend = pb;
        }
    }
    const bool palm = palmMode && wamt > kPalmWhammy;

    // joystick flicks: left/right = key, up/down = octave (SOLO: range).
    // Ignored while the whammy is in use -- on some guitars the whammy bleeds
    // into the joystick axes
    auto flick = [&](const StickMap& sm, int& pos) -> int
    {
        if (! sm.valid() || sm.byteIdx >= len)
            return 0;
        const int v = d[sm.byteIdx];
        const int upT = sm.center + juce::jmax(4, (int) (0.55f * (float) (sm.hi - sm.center)));
        const int dnT = sm.center - juce::jmax(4, (int) (0.55f * (float) (sm.center - sm.lo)));
        if (pos == 0)
        {
            if (v > upT) { pos = 1; return 1; }
            if (v < dnT) { pos = 1; return -1; }
        }
        else if (std::abs(v - sm.center) < juce::jmax(3, (sm.hi - sm.lo) / 4))
            pos = 0;
        return 0;
    };
    if (wamt <= 0.05f)
    {
        const int sgn = flip ? -1 : 1;   // upside down, the stick is too
        if (const int dx = flick(m.stickX, joyPos); dx != 0)
            shiftKey(dx * sgn);
        // HID Y axes grow DOWNWARD: pushing the joystick up reads as a smaller
        // value, and up has to mean octave up
        if (const int dy = -flick(m.stickY, joyPosY); dy != 0)
            shiftOctave(dy * sgn);
    }

    // strum edges + a short latch so late fingers still join the combo
    if (now >= whamBusyUntil && ((down && ! prevDown) || (up && ! prevUp)))
    {
        strumLatchAt = now;
        latchDown = down && ! prevDown;
        latchVel = latchDown ? kVelDown : kVelUp;
    }
    prevDown = down;
    prevUp = up;

    int topFretNow = -1;
    for (int i = 4; i >= 0; --i)
        if (combo & (1 << i)) { topFretNow = i; break; }

    if (strumLatchAt >= 0.0 && now - strumLatchAt >= kStrumLatchS)
    {
        strumLatchAt = -1.0;
        // ghost-pulse filter: a real strum is still engaged after the latch;
        // a one-report glitch (whammy leaking onto the strum contacts) has
        // already vanished by now
        if (latchDown ? down : up)
        {
            const bool muted = palm && mode != Chart;
            const int vel = muted ? juce::roundToInt((float) latchVel * kPalmVel) : latchVel;
            const double rollS = juce::jlimit(0, 50, strumRollMs.load()) * 0.001;
            allOff();
            candCombo = -1;
            sustainOnAt = now;
            sustainOffPending = false;
            int voices = 1;   // notes in this strum (for the palm choke)
            if (mode == Easy)
            {
                if (combo != 0)
                {
                    // the held shape, or (unmapped 3+ fret shapes) its top fret
                    int slot = -1;
                    for (int s2 = 0; s2 < kChordSlots; ++s2)
                        if (kChordDefaults[s2].mask == combo) { slot = s2; break; }
                    if (slot < 0)
                        slot = juce::jmax(0, topFretNow);   // single-fret slots are 0..4
                    const int mask = kChordDefaults[slot].mask;
                    const auto& q = kQualities[chordQuality(slot)];
                    const int root = kChordRootBase + key + easyOct + chordRoot(slot);
                    // chord tones low -> high, then the strum order
                    juce::Array<int> seq;
                    if (muted)
                        seq = { root - 12, root - 5, root };   // palm: low power chord
                    else
                    {
                        juce::Array<int> tones { root };
                        if (q.third >= 0) tones.add(root + q.third);
                        tones.add(root + q.fifth);
                        tones.add(q.top > 0 ? root + q.top : root + 12);
                        if (latchDown)
                        {
                            seq.add(root - 12);
                            seq.addArray(tones);
                        }
                        else
                        {
                            tones.add((q.third >= 0 ? root + q.third : root + q.fifth) + 12);
                            for (int i = tones.size(); --i >= 0;)
                                seq.add(tones[i]);
                        }
                    }
                    for (int i = 0; i < seq.size(); ++i)
                    {
                        if (i == 0 || rollS <= 0.0) noteOn(seq[i], vel);
                        else                        scheduleOn(seq[i], vel, now + rollS * i);
                    }
                    voices = seq.size();
                    ringFret = mask;   // in CHORDS this holds the fret MASK
                    beginGem(mask, false);
                    announce(chordName(key, chordRoot(slot), chordQuality(slot)));
                }
                else
                {
                    noteOn(kBassBase + key + easyOct, muted ? vel : kVelDown);
                    ringFret = -1;
                    beginGem(0, false);
                    announce(noteName(kBassBase + key + easyOct));
                }
            }
            else if (mode == Real)
            {
                const int nn = kRealBase + key + octaveReal + combo;
                noteOn(nn, vel);
                ringCombo = combo;
                beginGem(combo, false);
                announce(noteName(nn));
            }
            else if (mode == Penta)  // SOLO: every held fret sounds together, like strings
            {
                if (combo != 0)
                {
                    juce::String names;
                    int n = 0;
                    for (int k2 = 0; k2 < 5; ++k2)
                    {
                        const int i = latchDown ? k2 : 4 - k2;  // sweep direction
                        if (! (combo & (1 << i)))
                            continue;
                        soloNotes[i] = soloNote(i);
                        if (n == 0 || rollS <= 0.0) noteOn(soloNotes[i], vel);
                        else                        scheduleOn(soloNotes[i], vel, now + rollS * n);
                        names += (n == 0 ? juce::String() : juce::String("+"))
                                 + juce::String(noteNames[soloNotes[i] % 12]);
                        ++n;
                    }
                    voices = n;
                    ringFret = 1;   // marker: fretted (per-fret release)
                    beginGem(combo, false);
                    announce(names);
                }
                else
                {
                    const int pn = soloNote(kSoloOpen);
                    noteOn(pn, vel);
                    ringFret = -1;  // the open root follows the strum bar
                    beginGem(0, false);
                    announce(noteName(pn));
                }
            }
            else  // CHART: the held frets as Clone Hero lane notes, no roll; an open strum charts nothing
            {
                if (combo != 0)
                {
                    for (int i = 0; i < 5; ++i)
                        if (combo & (1 << i))
                            for (int base : kChartLanes)
                                noteOn(base + i, vel);
                    chartHeld = combo;
                    beginGem(combo, false);
                    announceFrets(combo);
                }
            }
            if (muted && ! ringing.isEmpty())
                palmCutAt = now + rollS * (voices - 1) + kPalmS;
        }
    }

    // release
    if (strumSustain.load())
    {
        // STRUM CONTROLS SUSTAIN: whatever is ringing lasts exactly while the
        // strum bar is held, in every mode. Frets are free to change
        // underneath for the next chord without cutting this one. A flick is
        // still a note: the off waits until kMinSustainS after the on.
        if (! ringing.isEmpty())
        {
            if (! strum || sustainOffPending)
            {
                if (now - sustainOnAt >= kMinSustainS)
                {
                    allOff();
                    ringFret = ringCombo = -1;
                    sustainOffPending = false;
                }
                else
                    sustainOffPending = true;
            }
        }
        else
            sustainOffPending = false;
    }
    else if (mode == Easy)
    {
        // a chord sounds exactly while its frets are held; the open bass
        // exactly while the strum bar is held
        if (! ringing.isEmpty())
        {
            if (ringFret > 0 && (combo & ringFret) != ringFret)
            {
                allOff();
                ringFret = -1;
            }
            else if (ringFret < 0 && ! strum)
                allOff();
        }
    }
    else if (mode == Real)
    {
        // a note sounds while its full combo stays held (extra frets are
        // fine); the open note exactly while the strum bar is held
        if (! ringing.isEmpty())
        {
            if (ringCombo > 0 && (combo & ringCombo) != ringCombo)
            {
                allOff();
                ringCombo = -1;
            }
            else if (ringCombo == 0 && ! strum)
            {
                allOff();
                ringCombo = -1;
            }
        }
    }
    else if (mode == Penta)  // SOLO: each note sounds while ITS fret is held, like strings
    {
        if (! ringing.isEmpty())
        {
            bool anyFretted = false;
            for (int i = 0; i < 5; ++i)
                if (soloNotes[i] >= 0)
                {
                    anyFretted = true;
                    if (! (combo & (1 << i)))
                    {
                        releaseNote(soloNotes[i]);
                        soloNotes[i] = -1;
                    }
                }
            if (anyFretted && ringing.isEmpty() && pending.isEmpty())
            {
                endGem();
                ringFret = -1;
            }
            else if (! anyFretted && ringFret < 0 && ! strum)
                allOff();   // open root follows the bar
        }
    }
    else  // CHART: a lane note lasts exactly while its fret is held
    {
        if (chartHeld != 0)
        {
            for (int i = 0; i < 5; ++i)
                if ((chartHeld & (1 << i)) && ! (combo & (1 << i)))
                {
                    for (int base : kChartLanes)
                    {
                        sendMsg(juce::MidiMessage::noteOff(1, base + i));
                        ringing.removeValue(base + i);
                    }
                    chartHeld &= ~(1 << i);
                }
            if (chartHeld == 0)
                endGem();
        }
    }
}

// ============================== GHMidiProcessor ==============================

GHMidiProcessor::GHMidiProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

GHMidiProcessor::~GHMidiProcessor()
{
    service->removeClient(&inbox);
}

void GHMidiProcessor::prepareToPlay(double sampleRate, int)
{
    inbox.prepare(sampleRate);
    service->addClient(&inbox);
}

void GHMidiProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    audio.clear();
    midi.clear();
    inbox.popInto(midi, audio.getNumSamples());
}

void GHMidiProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream out(dest, true);
    out.writeInt(service->uiMode.load());
    out.writeInt(service->uiKey.load());
    out.writeInt(service->hammerOn.load() ? 1 : 0);
}

void GHMidiProcessor::setStateInformation(const void* data, int size)
{
    juce::MemoryInputStream in(data, (size_t) size, false);
    in.readInt();   // mode is saved for compatibility but not restored: always start in CHORDS
    const int k = juce::jlimit(0, 11, in.readInt());
    service->uiKey = k;
    service->keyRequest = k;
    service->hammerOn = in.readInt() != 0;
}

juce::AudioProcessorEditor* GHMidiProcessor::createEditor()
{
    return new GHMidiEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GHMidiProcessor();
}
