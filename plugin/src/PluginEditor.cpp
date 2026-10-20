#include "PluginEditor.h"
#include <BinaryData.h>
#include <cstdlib>
#include <set>

namespace {
const juce::Colour gemColours[5] = {
    juce::Colour(0xff33cc3d), juce::Colour(0xffe63232), juce::Colour(0xfff2d02a),
    juce::Colour(0xff3378e6), juce::Colour(0xfff2921f),
};
const juce::Colour openBarColour(0xffa05ff0);
const juce::Colour gold(0xfff2d02a);
const char* modeNames[4] = { "CHORDS", "NOTES", "SOLO", "CHART" };
const char* keyNames[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

juce::Typeface::Ptr ghTypeface()
{
    static juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor(
        BinaryData::MetalManiaRegular_ttf, (size_t) BinaryData::MetalManiaRegular_ttfSize);
    return t;
}
juce::Font ghFont(float h)
{
    return juce::Font(juce::FontOptions(ghTypeface()).withHeight(h));
}

const juce::String arrowL(juce::CharPointer_UTF8("\xE2\x97\x80"));   // left-pointing triangle
const juce::String arrowR(juce::CharPointer_UTF8("\xE2\x96\xB6"));   // right-pointing triangle
const juce::String arrowU(juce::CharPointer_UTF8("\xE2\x96\xB2"));   // up-pointing triangle
const juce::String arrowD(juce::CharPointer_UTF8("\xE2\x96\xBC"));   // down-pointing triangle
const juce::String arrowNE(juce::CharPointer_UTF8("\xE2\x86\x97"));  // north-east arrow

const char* kVersion = "v1.2";

juce::Rectangle<int> panelBounds(int W, int H)
{
    return { W / 2 - 250, H / 2 - 250, 500, 500 };
}
juce::Rectangle<int> helpBounds(int W, int H)
{
    return { W / 2 - 230, H / 2 - 215, 460, 430 };
}
// FRETS editor: CHORDS = one-fret column + two-fret column, SOLO = one column.
// Each row: five gem tabs, then the clickable chord/note button.
juce::Rectangle<int> fretsBounds(int W, int H)
{
    return { W / 2 - 260, H / 2 - 220, 520, 440 };
}
int fretsTop(juce::Rectangle<int> fb) { return fb.getY() + 96; }
juce::Point<int> chordRowPos(int slot, juce::Rectangle<int> fb)
{
    if (slot < 5)
        return { fb.getX() + 30, fretsTop(fb) + 18 + slot * 27 };
    return { fb.getX() + 276, fretsTop(fb) + 18 + (slot - 5) * 27 };
}
juce::Point<int> soloRowPos(int slot, juce::Rectangle<int> fb)
{
    return { fb.getX() + 30, fretsTop(fb) + 18 + slot * 30 };
}

// bottom row: MODE / STRUM / KEY / OCTAVE plate — label row, value row with
// arrows, then a badge naming the guitar control (minus and plus side by side)
juce::Rectangle<int> hudPlate(int W, int H)
{
    return { 16, H - 72, W - 32, 60 };
}
} // namespace

GHMidiEditor::GHMidiEditor(GHMidiProcessor& p)
    : AudioProcessorEditor(p), proc(p), highway(p, *this)
{
    setOpaque(true);
    highway.setContext(&context);
    context.setOpenGLVersionRequired(juce::OpenGLContext::openGL3_2);
    context.setRenderer(&highway);
    context.setContinuousRepainting(true);
    context.setComponentPaintingEnabled(true);
    context.attachTo(*this);

    addAndMakeVisible(gearBtn);
    gearBtn.onClick = [this] { setHelpVisible(false); setFretsVisible(false); setPanelVisible(! panelOpen); };
    addAndMakeVisible(helpBtn);
    helpBtn.onClick = [this] { setPanelVisible(false); setFretsVisible(false); setHelpVisible(! helpOpen); };
    addAndMakeVisible(fretsBtn);
    fretsBtn.onClick = [this] { setPanelVisible(false); setHelpVisible(false); setFretsVisible(! fretsOpen); };

    // FRETS editor
    for (auto* b : { &fretsTabChords, &fretsTabSolo })
    {
        addChildComponent(*b);
        b->setWantsKeyboardFocus(false);
        b->setClickingTogglesState(true);
        b->setRadioGroupId(1002);
        b->setColour(juce::TextButton::buttonOnColourId, gold);
        b->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff15151f));
    }
    fretsTabChords.setConnectedEdges(juce::Button::ConnectedOnRight);
    fretsTabSolo.setConnectedEdges(juce::Button::ConnectedOnLeft);
    fretsTabChords.onClick = [this] { setFretsTab(0); };
    fretsTabSolo.onClick   = [this] { setFretsTab(1); };
    addChildComponent(fretsResetBtn);
    fretsResetBtn.onClick = [this]
    {
        if (fretsTab == 0) proc.guitar().resetChordSlots();
        else               proc.guitar().resetSoloSlots();
        refreshFretButtons();
    };
    addChildComponent(fretsCloseBtn);
    fretsCloseBtn.onClick = [this] { setFretsVisible(false); };
    for (auto* b : { &fretsKeyPrev, &fretsKeyNext })
    {
        addChildComponent(*b);
        b->setWantsKeyboardFocus(false);
    }
    fretsKeyPrev.setButtonText(arrowL);
    fretsKeyNext.setButtonText(arrowR);
    fretsKeyPrev.onClick = [this] { proc.guitar().nudgeKey(-1); };
    fretsKeyNext.onClick = [this] { proc.guitar().nudgeKey(1); };
    // ALL KEYS = the shared binds; THIS KEY ONLY = this key gets its own copy
    for (auto* b : { &allKeysBtn, &thisKeyBtn })
    {
        addChildComponent(*b);
        b->setWantsKeyboardFocus(false);
        b->setClickingTogglesState(true);
        b->setRadioGroupId(1004);
        b->setColour(juce::TextButton::buttonOnColourId, gold);
        b->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff15151f));
    }
    allKeysBtn.setConnectedEdges(juce::Button::ConnectedOnRight);
    thisKeyBtn.setConnectedEdges(juce::Button::ConnectedOnLeft);
    allKeysBtn.onClick = [this] { auto& g = proc.guitar(); g.setKeyOwnBinds(g.uiKey.load(), false); refreshFretButtons(); };
    thisKeyBtn.onClick = [this] { auto& g = proc.guitar(); g.setKeyOwnBinds(g.uiKey.load(), true);  refreshFretButtons(); };
    for (int i = 0; i < GuitarService::kChordSlots; ++i)
    {
        auto* b = chordSlotBtns.add(new juce::TextButton());
        addChildComponent(b);
        b->setWantsKeyboardFocus(false);
        b->onClick = [this, i] { showChordMenu(i); };
    }
    for (int i = 0; i < GuitarService::kSoloSlots; ++i)
    {
        auto* b = soloSlotBtns.add(new juce::TextButton());
        addChildComponent(b);
        b->setWantsKeyboardFocus(false);
        b->onClick = [this, i] { showSoloMenu(i); };
    }

    // sustain selector: a radio pair, the active side in gold
    sustainLabel.setText("SUSTAIN", juce::dontSendNotification);
    sustainLabel.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    sustainLabel.setJustificationType(juce::Justification::centredRight);
    sustainLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.38f));
    addAndMakeVisible(sustainLabel);
    for (auto* b : { &sustainFretBtn, &sustainStrumBtn })
    {
        addAndMakeVisible(*b);
        b->setWantsKeyboardFocus(false);
        b->setClickingTogglesState(true);
        b->setRadioGroupId(1001);
        b->setColour(juce::TextButton::buttonOnColourId, gold);
        b->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff15151f));
    }
    sustainFretBtn.setConnectedEdges(juce::Button::ConnectedOnRight);
    sustainStrumBtn.setConnectedEdges(juce::Button::ConnectedOnLeft);
    sustainFretBtn.onClick  = [this] { proc.guitar().strumSustain = false; proc.guitar().requestSave(); };
    sustainStrumBtn.onClick = [this] { proc.guitar().strumSustain = true;  proc.guitar().requestSave(); };
    syncSustainButtons();

    auto initArrow = [this](juce::TextButton& b, const juce::String& glyph, std::function<void()> fn)
    {
        addAndMakeVisible(b);
        b.setButtonText(glyph);
        b.setWantsKeyboardFocus(false);
        b.onClick = std::move(fn);
    };
    initArrow(modePrev, arrowL, [this] { proc.guitar().nudgeMode(-1); });
    initArrow(modeNext, arrowR, [this] { proc.guitar().nudgeMode(1); });
    initArrow(strumPrev, arrowL, [this] { proc.guitar().nudgeStrum(-1); });
    initArrow(strumNext, arrowR, [this] { proc.guitar().nudgeStrum(1); });
    initArrow(keyPrev,  arrowL, [this] { proc.guitar().nudgeKey(-1); });
    initArrow(keyNext,  arrowR, [this] { proc.guitar().nudgeKey(1); });
    initArrow(octPrev,  arrowL, [this] { proc.guitar().nudgeOctave(-1); });
    initArrow(octNext,  arrowR, [this] { proc.guitar().nudgeOctave(1); });

    auto initLabel = [this](juce::Label& l, const juce::String& text, float alpha = 0.85f)
    {
        addChildComponent(l);
        l.setText(text, juce::dontSendNotification);
        l.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(alpha));
    };
    initLabel(deviceLabel, "Controller");
    initLabel(learnHint, "");
    learnHint.setColour(juce::Label::textColourId, gold);

    addChildComponent(deviceBox);
    deviceBox.setTextWhenNothingSelected("(pick your controller)");
    deviceBox.onChange = [this]
    {
        const int idx = deviceBox.getSelectedId() - 1;
        if (idx >= 0 && idx < shownDevices.size())
            proc.guitar().selectDevice(shownDevices[idx].vid, shownDevices[idx].pid);
    };
    addChildComponent(rescanBtn);
    rescanBtn.onClick = [this]
    {
        proc.guitar().requestDeviceScan();
        refreshMidiOutBox();
    };

    // Windows: the panel shows a MIDI OUT picker in place of the virtual-output
    // toggle. A loopMIDI port makes the app appear as a MIDI input in any DAW,
    // the way the virtual "GH MIDI" source does on macOS.
    initLabel(midiOutLabel, "MIDI out");
    addChildComponent(midiOutBox);
    midiOutBox.setTextWhenNothingSelected("(none - pick a port, e.g. loopMIDI)");
    midiOutBox.onChange = [this]
    {
        const int id = midiOutBox.getSelectedId();   // 1 = none, 2.. = shownMidiOuts
        if (id == 1)
            proc.guitar().selectMidiOutput({}, {});
        else if (id >= 2 && id - 2 < shownMidiOuts.size())
            proc.guitar().selectMidiOutput(shownMidiOuts[id - 2].identifier, shownMidiOuts[id - 2].name);
    };

    // mapping table: one row per control, LEARN / CLEAR each
    for (int t = 0; t < GuitarService::LTargetCount; ++t)
    {
        auto* name = rowNames.add(new juce::Label());
        addChildComponent(name);
        name->setText(GuitarService::targetName(t), juce::dontSendNotification);
        name->setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        name->setColour(juce::Label::textColourId,
                        t < 5 ? gemColours[t] : juce::Colours::white.withAlpha(0.85f));

        auto* desc = rowDescs.add(new juce::Label());
        addChildComponent(desc);
        desc->setFont(juce::Font(juce::FontOptions(12.0f)));
        desc->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.55f));

        auto* learn = rowLearn.add(new juce::TextButton("LEARN"));
        addChildComponent(learn);
        learn->onClick = [this, t]
        {
            auto& svc = proc.guitar();
            if (svc.getLearnTarget() == t)
                svc.cancelLearn();
            else
                svc.startLearn(t);
        };

        auto* clear = rowClear.add(new juce::TextButton("X"));
        addChildComponent(clear);
        clear->onClick = [this, t] { proc.guitar().clearMapping(t); };
    }

    addChildComponent(vmidiToggle);
    vmidiToggle.onClick = [this]
    {
        proc.guitar().virtualMidiOn = vmidiToggle.getToggleState();
        proc.guitar().requestSave();
    };
    addChildComponent(leftyToggle);
    leftyToggle.onClick = [this]
    {
        proc.guitar().lefty = leftyToggle.getToggleState();
        proc.guitar().requestSave();
    };
    initLabel(whammyLabel, "Whammy");
    whammyLabel.setJustificationType(juce::Justification::centredRight);
    for (auto* b : { &whamBendBtn, &whamPalmBtn })
    {
        addChildComponent(*b);
        b->setWantsKeyboardFocus(false);
        b->setClickingTogglesState(true);
        b->setRadioGroupId(1003);
        b->setColour(juce::TextButton::buttonOnColourId, gold);
        b->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff15151f));
    }
    whamBendBtn.setConnectedEdges(juce::Button::ConnectedOnRight);
    whamPalmBtn.setConnectedEdges(juce::Button::ConnectedOnLeft);
    addChildComponent(settingsResetBtn);
    settingsResetBtn.onClick = [this]
    {
        juce::Component::SafePointer<GHMidiEditor> safe(this);
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Reset settings?",
            "This controller's button mapping, lefty flip and the whammy mode go back to defaults. "
            "Your FRETS binds are kept.",
            "RESET", "CANCEL", this,
            juce::ModalCallbackFunction::create([safe](int ok)
            {
                if (ok == 1 && safe != nullptr)
                    safe->proc.guitar().requestSettingsReset();
            }));
    };
    whamBendBtn.onClick = [this] { proc.guitar().palmWhammy = false; proc.guitar().requestSave(); };
    whamPalmBtn.onClick = [this] { proc.guitar().palmWhammy = true;  proc.guitar().requestSave(); };
    addChildComponent(closeBtn);
    closeBtn.onClick = [this]
    {
        proc.guitar().cancelLearn();
        setPanelVisible(false);
    };

    addChildComponent(helpCloseBtn);
    helpCloseBtn.onClick = [this] { setHelpVisible(false); };
    addChildComponent(moreBtn);
    moreBtn.setButtonText("MORE HELP  " + arrowNE);
    moreBtn.setColour(juce::TextButton::buttonColourId, gold);
    moreBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff15151f));
    moreBtn.onClick = []
    {
        juce::URL("https://michaelloya.studio/gh-midi").launchInDefaultBrowser();
    };

    if (auto* env = std::getenv("GHMIDI_HUDSNAP"); env != nullptr && juce::JUCEApplicationBase::isStandaloneApp())
    {
        hudSnapDir = env;
        if (std::getenv("GHMIDI_LEFTY") != nullptr)   // dev: check the mirrored highway
            proc.guitar().lefty = true;
        juce::Logger::writeToLog("editor: created, HUD snapshots -> " + hudSnapDir);
    }

    setSize(720, 620);
    startTimerHz(30);
}

void GHMidiEditor::saveHudSnapshot(const juce::String& name)
{
    // 2x, transparent: composited over a (2x) GL frame for the guide's screenshots
    juce::Image img(juce::Image::ARGB, getWidth() * 2, getHeight() * 2, true);
    {
        juce::Graphics g(img);
        g.addTransform(juce::AffineTransform::scale(2.0f));
        paintEntireComponent(g, false);
    }
    const auto file = juce::File(hudSnapDir).getChildFile("hud_" + name + ".png");
    file.deleteFile();
    juce::FileOutputStream os(file);
    if (os.openedOk())
        juce::PNGImageFormat().writeImageToStream(img, os);
    juce::Logger::writeToLog("hud: " + name + (os.openedOk() ? " saved " : " FAILED ") + file.getFullPathName());
}

GHMidiEditor::~GHMidiEditor()
{
    context.detach();
}

void GHMidiEditor::setPanelVisible(bool visible)
{
    panelOpen = visible;
    for (auto* c : { (juce::Component*) &sustainLabel, (juce::Component*) &sustainFretBtn, (juce::Component*) &sustainStrumBtn })
        c->setVisible(! panelOpen && ! helpOpen && ! fretsOpen);
    for (auto* c : std::initializer_list<juce::Component*> {
             &deviceLabel, &deviceBox, &rescanBtn, &closeBtn, &learnHint,
             &leftyToggle, &whammyLabel, &whamBendBtn, &whamPalmBtn, &settingsResetBtn })
        c->setVisible(visible);
    // the app's only output IS the "GH MIDI" input, so it's always on there;
    // the plugin also sends straight down its track, so it can be switched off
    vmidiToggle.setVisible(visible && GuitarService::kHasVirtualMidi && ! proc.guitar().isStandalone());
    midiOutLabel.setVisible(visible && ! GuitarService::kHasVirtualMidi);
    midiOutBox.setVisible(visible && ! GuitarService::kHasVirtualMidi);
    for (int t = 0; t < rowNames.size(); ++t)
    {
        rowNames[t]->setVisible(visible);
        rowDescs[t]->setVisible(visible);
        rowLearn[t]->setVisible(visible);
        rowClear[t]->setVisible(visible);
    }
    if (visible)
    {
        proc.guitar().requestDeviceScan();
        vmidiToggle.setToggleState(proc.guitar().virtualMidiOn.load(), juce::dontSendNotification);
        syncSettingsButtons();
        refreshMidiOutBox();
        refreshMappingRows();
    }
    else
        proc.guitar().cancelLearn();
    updateHudButtons();
    repaint();
}

void GHMidiEditor::syncSustainButtons()
{
    const bool strum = proc.guitar().strumSustain.load();
    sustainStrumBtn.setToggleState(strum,  juce::dontSendNotification);
    sustainFretBtn.setToggleState(! strum, juce::dontSendNotification);
}

void GHMidiEditor::setHelpVisible(bool visible)
{
    helpOpen = visible;
    for (auto* c : { (juce::Component*) &sustainLabel, (juce::Component*) &sustainFretBtn, (juce::Component*) &sustainStrumBtn })
        c->setVisible(! panelOpen && ! helpOpen && ! fretsOpen);
    helpCloseBtn.setVisible(visible);
    moreBtn.setVisible(visible);
    updateHudButtons();
    repaint();
}

void GHMidiEditor::syncSettingsButtons()
{
    auto& svc = proc.guitar();
    leftyToggle.setToggleState(svc.lefty.load(), juce::dontSendNotification);
    whamPalmBtn.setToggleState(svc.palmWhammy.load(), juce::dontSendNotification);
    whamBendBtn.setToggleState(! svc.palmWhammy.load(), juce::dontSendNotification);
}

void GHMidiEditor::setFretsVisible(bool visible)
{
    fretsOpen = visible;
    for (auto* c : { (juce::Component*) &sustainLabel, (juce::Component*) &sustainFretBtn, (juce::Component*) &sustainStrumBtn })
        c->setVisible(! panelOpen && ! helpOpen && ! fretsOpen);
    for (auto* c : { (juce::Component*) &fretsTabChords, (juce::Component*) &fretsTabSolo,
                     (juce::Component*) &fretsResetBtn, (juce::Component*) &fretsCloseBtn,
                     (juce::Component*) &fretsKeyPrev, (juce::Component*) &fretsKeyNext,
                     (juce::Component*) &allKeysBtn, (juce::Component*) &thisKeyBtn })
        c->setVisible(visible);
    // open on the tab for the mode being played
    const int mode = proc.guitar().uiMode.load() % 4;
    setFretsTab(! visible ? fretsTab : mode == GuitarService::Penta ? 1 : mode == GuitarService::Easy ? 0 : fretsTab);
    updateHudButtons();
    repaint();
}

void GHMidiEditor::setFretsTab(int tab)
{
    fretsTab = tab;
    fretsTabChords.setToggleState(tab == 0, juce::dontSendNotification);
    fretsTabSolo.setToggleState(tab == 1, juce::dontSendNotification);
    for (auto* b : chordSlotBtns) b->setVisible(fretsOpen && tab == 0);
    for (auto* b : soloSlotBtns)  b->setVisible(fretsOpen && tab == 1);
    refreshFretButtons();
    repaint();
}

void GHMidiEditor::refreshFretButtons()
{
    auto& svc = proc.guitar();
    const int key = svc.uiKey.load() % 12;
    const bool own = svc.keyHasOwnBinds(key);
    thisKeyBtn.setToggleState(own, juce::dontSendNotification);
    allKeysBtn.setToggleState(! own, juce::dontSendNotification);
    for (int i = 0; i < chordSlotBtns.size(); ++i)
        chordSlotBtns[i]->setButtonText(GuitarService::chordName(key, svc.chordRoot(i), svc.chordQuality(i)));
    for (int i = 0; i < soloSlotBtns.size(); ++i)
        soloSlotBtns[i]->setButtonText(GuitarService::noteName(svc.soloNote(i)));
}

// Chords for a fret: the key's own chords first (the "can't miss" set), then
// its sevenths, the common outside-the-key chords, and every chord by root.
void GHMidiEditor::showChordMenu(int slot)
{
    auto& svc = proc.guitar();
    const int key = svc.uiKey.load() % 12;
    const int curR = svc.chordRoot(slot), curQ = svc.chordQuality(slot);
    auto item = [&](juce::PopupMenu& menu, int root, int q)
    {
        menu.addItem(1 + root * 100 + q, GuitarService::chordName(key, root, q), true, root == curR && q == curQ);
    };
    static const int dRoot[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int dTri[7]  = { 0, 1, 1, 0, 0, 1, 5 };    // I ii iii IV V vi vii°
    static const int dSev[7]  = { 3, 4, 4, 3, 2, 4, 6 };    // maj7 m7 m7 maj7 7 m7 m7b5
    static const int oRoot[6] = { 10, 8, 3, 5, 4, 2 };      // bVII bVI bIII iv III II
    static const int oQ[6]    = { 0, 0, 0, 1, 0, 0 };
    juce::PopupMenu m;
    m.addSectionHeader(juce::String("IN THE KEY OF ") + keyNames[key]);
    for (int i = 0; i < 7; ++i) item(m, dRoot[i], dTri[i]);
    m.addSectionHeader("SEVENTHS");
    for (int i = 0; i < 7; ++i) item(m, dRoot[i], dSev[i]);
    m.addSectionHeader("OUTSIDE THE KEY");
    for (int i = 0; i < 6; ++i) item(m, oRoot[i], oQ[i]);
    juce::PopupMenu any;
    for (int r = 0; r < 12; ++r)
    {
        juce::PopupMenu sub;
        for (int q = 0; q < GuitarService::numQualities(); ++q)
            item(sub, r, q);
        any.addSubMenu(keyNames[(key + r) % 12], sub, true, nullptr, r == curR);
    }
    m.addSeparator();
    m.addSubMenu("ANY CHORD", any);
    m.addItem(9999, "Default (" + GuitarService::chordName(key, GuitarService::defaultChordRoot(slot),
                                                           GuitarService::defaultChordQuality(slot)) + ")");
    juce::Component::SafePointer<GHMidiEditor> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(chordSlotBtns[slot]),
                    [safe, slot](int r)
                    {
                        if (safe == nullptr || r <= 0)
                            return;
                        auto& g = safe->proc.guitar();
                        if (r == 9999)
                            g.setChordSlot(slot, GuitarService::defaultChordRoot(slot), GuitarService::defaultChordQuality(slot));
                        else
                            g.setChordSlot(slot, (r - 1) / 100, (r - 1) % 100);
                        safe->refreshFretButtons();
                    });
}

// Notes for a SOLO fret, named as they sound at the current key and range:
// the key's scale over three octaves, everything else in a submenu.
void GHMidiEditor::showSoloMenu(int slot)
{
    auto& svc = proc.guitar();
    const int key = svc.uiKey.load() % 12;
    const int curOff = svc.soloOffset(slot);
    static const bool inKey[12] = { true, false, true, false, true, true, false, true, false, true, false, true };
    std::set<int> seen;
    auto addNote = [&](juce::PopupMenu& menu, int off)
    {
        const int n = svc.soloNoteWith(slot, off);
        if (! seen.insert(n).second)
            return;   // two choices that sound the same at this range: list it once
        menu.addItem(1100 + off, GuitarService::noteName(n), true, off == curOff);
    };
    juce::PopupMenu m;
    const char* bands[3] = { "LOW", "MIDDLE", "HIGH" };
    for (int b = 0; b < 3; ++b)
    {
        m.addSectionHeader(juce::String(b == 0 ? juce::String("IN THE KEY OF ") + keyNames[key] + "  -  " : juce::String())
                           + bands[b]);
        for (int off = -12 + 12 * b; off < 12 * b; ++off)
            if (inKey[((off % 12) + 12) % 12])
                addNote(m, off);
        if (b == 2)
            addNote(m, 24);
    }
    juce::PopupMenu other;
    for (int off = -12; off < 24; ++off)
        if (! inKey[((off % 12) + 12) % 12])
            addNote(other, off);
    m.addSeparator();
    m.addSubMenu("OTHER NOTES", other);
    m.addItem(9999, "Default (" + GuitarService::noteName(svc.soloNoteWith(slot, GuitarService::defaultSoloOffset(slot))) + ")");
    juce::Component::SafePointer<GHMidiEditor> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(soloSlotBtns[slot]),
                    [safe, slot](int r)
                    {
                        if (safe == nullptr || r <= 0)
                            return;
                        auto& g = safe->proc.guitar();
                        g.setSoloSlot(slot, r == 9999 ? GuitarService::defaultSoloOffset(slot) : r - 1100);
                        safe->refreshFretButtons();
                    });
}

void GHMidiEditor::updateHudButtons()
{
    const bool show = ! panelOpen && ! helpOpen && ! fretsOpen;
    for (auto* c : std::initializer_list<juce::Component*> {
             &modePrev, &modeNext, &strumPrev, &strumNext, &keyPrev, &keyNext, &octPrev, &octNext })
        c->setVisible(show);
    // octave and strum arrows grey out at the ends of their ranges; in CHART
    // mode strum, key and octave don't apply at all
    const int mode = proc.guitar().uiMode.load() % 4;
    const bool chart = mode == GuitarService::Chart;
    const int oct = mode == 0 ? proc.guitar().uiEasyOct.load() : proc.guitar().uiOctave.load();
    keyPrev.setEnabled(! chart);
    keyNext.setEnabled(! chart);
    if (mode == GuitarService::Penta)   // SOLO: RANGE, in scale steps
    {
        const int st = proc.guitar().uiSoloStep.load();
        octPrev.setEnabled(st > GuitarService::kSoloStepMin);
        octNext.setEnabled(st < GuitarService::kSoloStepMax);
    }
    else
    {
        octPrev.setEnabled(! chart && oct > GuitarService::kOctaveMin);
        octNext.setEnabled(! chart && oct < GuitarService::kOctaveMax);
    }
    const int ms = proc.guitar().strumRollMs.load();
    strumPrev.setEnabled(! chart && ms > 0);
    strumNext.setEnabled(! chart && ms < GuitarService::kStrumMaxMs);
}

void GHMidiEditor::refreshDeviceBox()
{
    shownDevices = proc.guitar().getDevices();
    deviceBox.clear(juce::dontSendNotification);
    int selected = 0;
    for (int i = 0; i < shownDevices.size(); ++i)
    {
        deviceBox.addItem(shownDevices[i].label, i + 1);
        if (shownDevices[i].vid == proc.guitar().currentVid()
            && shownDevices[i].pid == proc.guitar().currentPid())
            selected = i + 1;
    }
    if (selected > 0)
        deviceBox.setSelectedId(selected, juce::dontSendNotification);
}

void GHMidiEditor::refreshMidiOutBox()
{
    if (GuitarService::kHasVirtualMidi)
        return;
    shownMidiOuts = juce::MidiOutput::getAvailableDevices();
    midiOutBox.clear(juce::dontSendNotification);
    midiOutBox.addItem("(none)", 1);
    const auto current = proc.guitar().currentMidiOutputId();
    int selected = current.isEmpty() ? 1 : 0;
    for (int i = 0; i < shownMidiOuts.size(); ++i)
    {
        midiOutBox.addItem(shownMidiOuts[i].name, i + 2);
        if (shownMidiOuts[i].identifier == current)
            selected = i + 2;
    }
    if (selected > 0)
        midiOutBox.setSelectedId(selected, juce::dontSendNotification);
}

void GHMidiEditor::refreshMappingRows()
{
    for (int t = 0; t < rowDescs.size(); ++t)
        rowDescs[t]->setText(proc.guitar().describeMapping(t), juce::dontSendNotification);
}

void GHMidiEditor::timerCallback()
{
    syncSustainButtons();   // cheap, and keeps the pair honest after a state restore
    if (! isShowing())
        return;
    if (hudSnapDir.isNotEmpty())
    {
        // scripted tour of the HUD states (dev only, standalone only)
        auto& svc = proc.guitar();
        switch (++hudSnapTick)
        {
            case 1:   juce::Logger::writeToLog("hud: window showing, tour started");
                      svc.uiKey = 0; svc.strumRollMs = 10;   // match the demo's C-major chord names
                      break;
            case 45:  saveHudSnapshot("chords"); break;
            case 50:  svc.uiMode = 1; svc.uiKey = 7; svc.uiOctave = 12; break;
            case 60:  saveHudSnapshot("notes"); break;
            case 65:  svc.uiMode = 2; svc.uiSoloStep = 2;
                      svc.uiButtonBits = (1 << GuitarService::LMinus) | (1 << GuitarService::LStickY)
                                       | (1 << GuitarService::LPlus);
                      break;
            case 75:  saveHudSnapshot("solo_range"); break;
            case 77:  svc.uiMode = 3; svc.uiButtonBits = 0; svc.announceFrets(0x03); break;
            case 79:  saveHudSnapshot("chart"); break;
            case 80:  svc.uiMode = 0; svc.uiKey = 7; svc.uiFretBits = 0x06; setFretsVisible(true); break;
            case 88:  saveHudSnapshot("frets_chords"); break;
            case 90:  svc.uiMode = 2; svc.uiSoloStep = 0; svc.uiFretBits = 0x04; setFretsTab(1); break;
            case 96:  saveHudSnapshot("frets_solo"); break;
            case 98:  svc.uiFretBits = 0; setFretsVisible(false); setHelpVisible(true); break;
            case 106: saveHudSnapshot("help"); break;
            case 108: setHelpVisible(false); setPanelVisible(true); break;
            case 118: saveHudSnapshot("settings"); break;
            case 140: juce::Logger::writeToLog("hud: tour done, quitting"); juce::JUCEApplicationBase::quit(); break;
            default: break;
        }
    }
    updateHudButtons();
    if (fretsOpen)
        refreshFretButtons();   // key / range changes rename every row
    if (panelOpen)
    {
        auto& svc = proc.guitar();
        syncSettingsButtons();
        if (svc.getDeviceListVersion() != lastDevVersion)
        {
            lastDevVersion = svc.getDeviceListVersion();
            refreshDeviceBox();
        }
        if (svc.getMapVersion() != lastMapVersion)
        {
            lastMapVersion = svc.getMapVersion();
            refreshMappingRows();
        }
        const int liveBits = svc.uiButtonBits.load();
        for (int t = 0; t < rowNames.size(); ++t)
        {
            const bool lit = (liveBits & (1 << t)) != 0;
            rowNames[t]->setColour(juce::Label::backgroundColourId,
                                   lit ? gold.withAlpha(0.22f) : juce::Colours::transparentBlack);
            rowDescs[t]->setColour(juce::Label::textColourId,
                                   juce::Colours::white.withAlpha(lit ? 0.95f : 0.55f));
        }
        const int lt = svc.getLearnTarget();
        if (lt != lastLearnTarget)
        {
            lastLearnTarget = lt;
            for (int t = 0; t < rowLearn.size(); ++t)
                rowLearn[t]->setButtonText(t == lt ? "..." : "LEARN");
            learnHint.setText(lt < 0 ? juce::String()
                                     : (lt >= GuitarService::LWhammy && lt <= GuitarService::LStickY
                                            ? "Now SWEEP the " + GuitarService::targetName(lt)
                                            : "Now PRESS the " + GuitarService::targetName(lt)),
                              juce::dontSendNotification);
        }
    }
    repaint();
}

void GHMidiEditor::resized()
{
    highway.setViewSize(getWidth(), getHeight());
    gearBtn.setBounds(getWidth() - 104, 16, 92, 28);
    helpBtn.setBounds(getWidth() - 140, 16, 30, 28);
    fretsBtn.setBounds(getWidth() - 200, 16, 54, 28);

    // FRETS editor
    {
        const auto fb = fretsBounds(getWidth(), getHeight());
        fretsCloseBtn.setBounds(fb.getRight() - 40, fb.getY() + 12, 28, 26);
        fretsTabSolo.setBounds(fb.getRight() - 48 - 76, fb.getY() + 12, 76, 26);
        fretsTabChords.setBounds(fb.getRight() - 48 - 152, fb.getY() + 12, 76, 26);
        fretsResetBtn.setBounds(fb.getX() + 30, fb.getBottom() - 48, 150, 26);
        const int ky = fb.getY() + 52;
        fretsKeyPrev.setBounds(fb.getX() + 64, ky + 1, 24, 22);   // same size as the plate arrows
        fretsKeyNext.setBounds(fb.getX() + 64 + 24 + 52, ky + 1, 24, 22);
        thisKeyBtn.setBounds(fb.getRight() - 30 - 112, ky, 112, 24);
        allKeysBtn.setBounds(fb.getRight() - 30 - 112 - 84, ky, 84, 24);
        for (int i = 0; i < chordSlotBtns.size(); ++i)
        {
            const auto p = chordRowPos(i, fb);
            chordSlotBtns[i]->setBounds(p.x + 68, p.y - 4, 150, 23);
        }
        for (int i = 0; i < soloSlotBtns.size(); ++i)
        {
            const auto p = soloRowPos(i, fb);
            soloSlotBtns[i]->setBounds(p.x + 68, p.y - 4, 110, 23);
        }
    }
    // SUSTAIN  [FRET][STRUM]  -- flush right with SETTINGS, one row below it
    sustainStrumBtn.setBounds(getWidth() - 104 + 46, 50, 46, 22);
    sustainFretBtn.setBounds(getWidth() - 104, 50, 46, 22);
    sustainLabel.setBounds(getWidth() - 200, 50, 90, 22);

    // MODE / STRUM / KEY / OCTAVE plate: arrows either side of each value
    {
        const auto plate = hudPlate(getWidth(), getHeight());
        const int colW = plate.getWidth() / 4;
        juce::TextButton* arrows[4][2] = { { &modePrev, &modeNext },
                                           { &strumPrev, &strumNext },
                                           { &keyPrev, &keyNext },
                                           { &octPrev, &octNext } };
        for (int i = 0; i < 4; ++i)
        {
            auto row = juce::Rectangle<int>(plate.getX() + i * colW, plate.getY() + 19, colW, 22).reduced(6, 0);
            arrows[i][0]->setBounds(row.removeFromLeft(24));
            arrows[i][1]->setBounds(row.removeFromRight(24));
        }
    }

    // settings panel
    const auto pb = panelBounds(getWidth(), getHeight());
    closeBtn.setBounds(pb.getRight() - 40, pb.getY() + 12, 28, 26);
    auto r = pb.reduced(20);
    r.removeFromTop(30);  // painted title
    auto devRow = r.removeFromTop(24);
    deviceLabel.setBounds(devRow.removeFromLeft(76));
    rescanBtn.setBounds(devRow.removeFromRight(72));
    devRow.removeFromRight(6);
    deviceBox.setBounds(devRow);
    r.removeFromTop(8);
    learnHint.setBounds(r.removeFromTop(18));
    r.removeFromTop(4);
    for (int t = 0; t < rowNames.size(); ++t)
    {
        auto row = r.removeFromTop(22);
        rowNames[t]->setBounds(row.removeFromLeft(136));
        rowClear[t]->setBounds(row.removeFromRight(30).reduced(0, 1));
        row.removeFromRight(4);
        rowLearn[t]->setBounds(row.removeFromRight(62).reduced(0, 1));
        rowDescs[t]->setBounds(row);
        r.removeFromTop(1);
    }
    r.removeFromTop(8);
    {
        auto optRow = r.removeFromTop(24);
        leftyToggle.setBounds(optRow.removeFromLeft(130));
        whamPalmBtn.setBounds(optRow.removeFromRight(96));
        whamBendBtn.setBounds(optRow.removeFromRight(64));
        optRow.removeFromRight(8);
        whammyLabel.setBounds(optRow);
    }
    r.removeFromTop(6);
    auto outRow = r.removeFromTop(24);
    vmidiToggle.setBounds(outRow);              // macOS / Linux
    midiOutLabel.setBounds(outRow.removeFromLeft(76));   // Windows
    midiOutBox.setBounds(outRow);
    settingsResetBtn.setBounds(pb.getX() + 20, pb.getBottom() - 44, 160, 26);

    // help overlay
    auto hb = helpBounds(getWidth(), getHeight());
    moreBtn.setBounds(hb.getCentreX() - 90, hb.getBottom() - 78, 180, 32);
    helpCloseBtn.setBounds(hb.getRight() - 40, hb.getY() + 12, 28, 26);
}

void GHMidiEditor::paint(juce::Graphics& g)
{
    const float W = (float) getWidth();
    const float H = (float) getHeight();
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.setFont(ghFont(28.0f));
    g.drawText("GH MIDI", 20, 14, 124, 32, juce::Justification::left);

    // small gold pill naming the guitar control that changes a setting;
    // fills solid while that control is being touched
    const int liveBits = proc.guitar().uiButtonBits.load();
    auto badge = [&](juce::Rectangle<float> r, const juce::String& text, bool lit)
    {
        g.setColour(gold.withAlpha(lit ? 0.95f : 0.10f));
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        g.setColour(gold.withAlpha(lit ? 1.0f : 0.45f));
        g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 1.0f);
        g.setColour(lit ? juce::Colour(0xff15151f) : gold.withAlpha(0.85f));
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.drawText(text, r.toNearestInt(), juce::Justification::centred);
    };

    // chord/note name pop
    {
        const float age = (float) (now - proc.guitar().lastPlayedAt.load());
        if (age >= 0.0f && age < 1.6f)
        {
            const float pop = 1.0f + 0.35f * std::exp(-age * 9.0f);
            const float a = juce::jlimit(0.0f, 1.0f, 1.8f - age * 1.2f);
            g.setFont(ghFont(48.0f * pop));
            const int mask = proc.guitar().lastPlayedFrets.load();
            if (mask != 0)   // CHART: one letter per held fret, in that fret's colour
            {
                const float adv = 40.0f * pop;
                int n = 0;
                for (int i = 0; i < 5; ++i)
                    n += (mask >> i) & 1;
                float x = W * 0.5f - adv * (float) n * 0.5f;
                for (int i = 0; i < 5; ++i)
                    if (mask & (1 << i))
                    {
                        g.setColour(gemColours[i].withAlpha(a));
                        g.drawText(juce::String::charToString((juce::juce_wchar) "GRYBO"[i]),
                                   (int) x, (int) (H * 0.08f), (int) adv, 58, juce::Justification::centred);
                        x += adv;
                    }
            }
            else
            {
                g.setColour(gold.withAlpha(a));
                g.drawText(proc.guitar().getLastPlayed(), 0, (int) (H * 0.08f), getWidth(), 58,
                           juce::Justification::centred);
            }
        }
    }

    // MODE / STRUM / KEY / OCTAVE plate: current settings, arrows either side (buttons)
    {
        const auto plate = hudPlate(getWidth(), getHeight());
        g.setColour(juce::Colour(0x8810101a));
        g.fillRoundedRectangle(plate.toFloat(), 6.0f);
        const int mode = proc.guitar().uiMode.load() % 4;
        const int uiK = proc.guitar().uiKey.load() % 12;
        const bool notes = mode == 1;
        const bool solo = mode == GuitarService::Penta;
        const bool chart = mode == GuitarService::Chart;
        const int octSemis = mode == 0 ? proc.guitar().uiEasyOct.load() : proc.guitar().uiOctave.load();
        const int oct = octSemis / 12;
        juce::String vals[4] = { modeNames[mode],
                                 juce::String(proc.guitar().strumRollMs.load()) + " ms",
                                 keyNames[uiK],
                                 oct > 0 ? "+" + juce::String(oct) : juce::String(oct) };
        if (notes)   // NOTES: show the lowest playable note, key and octave folded in
        {
            const int base = 40 + uiK + proc.guitar().uiOctave.load();
            vals[2] = juce::String(keyNames[base % 12]) + juce::String(base / 12 - 1);
        }
        if (solo)   // SOLO: RANGE = the note on the green fret, slid along the scale
            vals[3] = GuitarService::noteName(proc.guitar().soloNote(0));
        const char* labels[4] = { "MODE", "STRUM SPREAD", notes ? "BASE" : "KEY", solo ? "RANGE" : "OCTAVE" };
        // the guitar control for each column, mirrored on the on-screen arrows
        const juce::String guitarCtl[4] = { "MINUS", "PLUS",
                                            "JOYSTICK " + arrowL + " " + arrowR,
                                            "JOYSTICK " + arrowU + " " + arrowD };
        const float badgeW[4] = { 56.0f, 56.0f, 98.0f, 98.0f };
        const int badgeBit[4] = { GuitarService::LMinus, GuitarService::LPlus,
                                  GuitarService::LStickX, GuitarService::LStickY };
        const int colW = plate.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
        {
            const int cx = plate.getX() + i * colW;
            const bool dimmed = chart && i > 0;   // CHART: strum, key and octave don't apply
            if (dimmed)
                g.beginTransparencyLayer(0.3f);
            g.setColour(juce::Colours::white.withAlpha(0.38f));
            g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
            g.drawText(labels[i], cx, plate.getY() + 6, colW, 10, juce::Justification::centred);
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.setFont(ghFont(18.0f));
            g.drawText(vals[i], cx + 30, plate.getY() + 20, colW - 60, 20, juce::Justification::centred);
            badge({ (float) cx + ((float) colW - badgeW[i]) * 0.5f, (float) plate.getY() + 43.0f,
                    badgeW[i], 14.0f },
                  guitarCtl[i], (liveBits & (1 << badgeBit[i])) != 0);
            if (dimmed)
                g.endTransparencyLayer();
        }
    }

    // status banner
    const bool found = proc.guitar().guitarFound.load();
    const bool empty = proc.guitar().adapterEmpty.load();
    if ((! found || empty) && ! panelOpen && ! helpOpen && ! fretsOpen)
    {
        g.setColour(juce::Colour(0xd0e23b3b));
        g.fillRoundedRectangle(24.0f, H * 0.46f, W - 48.0f, 28.0f, 6.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        g.drawText(! found ? "CONTROLLER NOT FOUND - plug it in, or open SETTINGS to pick a device"
                           : "ADAPTER CAN'T SEE THE GUITAR - unplug USB, reseat plug, replug",
                   0, (int) (H * 0.46f), getWidth(), 28, juce::Justification::centred);
    }

    auto drawOverlayFrame = [&](juce::Rectangle<float> pb, const juce::String& title)
    {
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillAll();
        g.setColour(juce::Colour(0xf2151520));
        g.fillRoundedRectangle(pb, 10.0f);
        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.drawRoundedRectangle(pb, 10.0f, 1.5f);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(ghFont(24.0f));
        g.drawText(title, (int) pb.getX() + 20, (int) pb.getY() + 12,
                   (int) pb.getWidth() - 40, 26, juce::Justification::left);
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(kVersion, (int) pb.getX() + 20, (int) pb.getBottom() - 30,
                   (int) pb.getWidth() - 40, 16, juce::Justification::right);
    };

    if (panelOpen)
    {
        drawOverlayFrame(panelBounds(getWidth(), getHeight()).toFloat(), "SETTINGS");
    }
    else if (helpOpen)
    {
        auto hb = helpBounds(getWidth(), getHeight());
        drawOverlayFrame(hb.toFloat(), "HOW TO PLAY");
        auto rowY = hb.getY() + 52;
        auto section = [&](const juce::String& s)
        {
            g.setColour(gold);
            g.setFont(ghFont(17.0f));
            g.drawText(s, hb.getX() + 30, rowY, hb.getWidth() - 60, 20, juce::Justification::left);
            rowY += 24;
        };
        auto line = [&](const juce::String& head, const juce::String& body, int headW = 116)
        {
            g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.drawText(head, hb.getX() + 34, rowY, headW, 16, juce::Justification::left);
            g.setFont(juce::Font(juce::FontOptions(12.5f)));
            g.setColour(juce::Colours::white.withAlpha(0.62f));
            g.drawText(body, hb.getX() + 36 + headW, rowY, hb.getWidth() - 70 - headW, 16,
                       juce::Justification::left);
            rowY += 20;
        };
        section("CONTROLS");
        line("FRETS + STRUM", "play - up and down strums feel different");
        line("WHAMMY", "bend the note (or palm mute: SETTINGS)");
        line("MINUS", "switch mode");
        line("PLUS", "tap through strum speeds (0-50ms)");
        line("JOYSTICK", "left/right = key - up/down = octave (SOLO: range)");
        line("ON SCREEN", "the bottom arrows do all of the above too");
        line("FRETS", "change what any fret plays");
        rowY += 8;
        section("MODES");
        line("CHORDS", "every fret is a chord in your key. can't miss", 70);
        line("SOLO", "5 scale notes, strum any stack. can't miss", 70);
        line("NOTES", "fret combos pick all 32 notes. full control", 70);
        line("CHART", "frets = Clone Hero lanes. record a rough chart", 70);
    }
    else if (fretsOpen)
    {
        const auto fb = fretsBounds(getWidth(), getHeight());
        drawOverlayFrame(fb.toFloat(), "FRETS");
        auto& svc = proc.guitar();
        const int key = svc.uiKey.load() % 12;
        const int held = svc.uiFretBits.load();
        // KEY  < G >   -- the key these binds are for (the playing key)
        g.setColour(juce::Colours::white.withAlpha(0.38f));
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.drawText("KEY", fb.getX() + 30, fb.getY() + 52, 30, 24, juce::Justification::centredLeft);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(ghFont(18.0f));
        g.drawText(keyNames[key], fb.getX() + 88, fb.getY() + 52, 52, 24, juce::Justification::centred);
        auto tabs = [&](int x, int y, int mask)
        {
            for (int i = 0; i < 5; ++i)
            {
                juce::Path p;
                p.addRoundedRectangle((float) (x + i * 12), (float) y, 9.0f, 15.0f, 2.5f, 2.5f, false, false, true, true);
                g.setColour((mask & (1 << i)) ? gemColours[i] : juce::Colour(0xff2b2b36));
                g.fillPath(p);
            }
        };
        auto hilite = [&](juce::Point<int> p, int w)
        {
            g.setColour(gold.withAlpha(0.20f));
            g.fillRoundedRectangle((float) p.x - 6.0f, (float) p.y - 6.0f, (float) w, 27.0f, 4.0f);
        };
        auto header = [&](int x, int y, const juce::String& s)
        {
            g.setColour(juce::Colours::white.withAlpha(0.38f));
            g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
            g.drawText(s, x, y, 220, 10, juce::Justification::left);
        };
        const int top = fretsTop(fb);
        if (fretsTab == 0)
        {
            // the row the held frets would play: their shape, or (3+ frets) the top fret
            int hi = -1;
            for (int sl = 0; sl < GuitarService::kChordSlots && held != 0; ++sl)
                if (GuitarService::chordSlotMask(sl) == held) hi = sl;
            if (hi < 0 && held != 0)
                for (int i = 4; i >= 0 && hi < 0; --i)
                    if (held & (1 << i)) hi = i;
            header(fb.getX() + 30, top, "ONE FRET");
            header(fb.getX() + 276, top, "TWO FRETS");
            for (int sl = 0; sl < GuitarService::kChordSlots; ++sl)
            {
                const auto p = chordRowPos(sl, fb);
                if (sl == hi)
                    hilite(p, 232);
                tabs(p.x, p.y, GuitarService::chordSlotMask(sl));
            }
        }
        else
        {
            for (int sl = 0; sl < GuitarService::kSoloSlots; ++sl)
            {
                const auto p = soloRowPos(sl, fb);
                if ((sl < 5 && (held & (1 << sl)) != 0))
                    hilite(p, 190);
                if (sl < 5)
                    tabs(p.x, p.y, 1 << sl);
                else
                {
                    g.setColour(juce::Colours::white.withAlpha(0.6f));
                    g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
                    g.drawText("OPEN", p.x, p.y, 56, 15, juce::Justification::centredLeft);
                }
            }
        }
    }

}
