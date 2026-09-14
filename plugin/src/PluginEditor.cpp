#include "PluginEditor.h"
#include <BinaryData.h>
#include <cstdlib>

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

juce::Rectangle<int> panelBounds(int W, int H)
{
    return { W / 2 - 250, H / 2 - 240, 500, 480 };
}
juce::Rectangle<int> helpBounds(int W, int H)
{
    return { W / 2 - 230, H / 2 - 205, 460, 410 };
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
    gearBtn.onClick = [this] { setHelpVisible(false); setPanelVisible(! panelOpen); };
    addAndMakeVisible(helpBtn);
    helpBtn.onClick = [this] { setPanelVisible(false); setHelpVisible(! helpOpen); };

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
    rescanBtn.onClick = [this] { proc.guitar().requestDeviceScan(); };

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
        hudSnapDir = env;

    setSize(720, 620);
    startTimerHz(30);
}

void GHMidiEditor::saveHudSnapshot(const juce::String& name)
{
    juce::Image img(juce::Image::ARGB, getWidth(), getHeight(), true);   // transparent: composite over a GL frame
    {
        juce::Graphics g(img);
        paintEntireComponent(g, false);
    }
    const auto file = juce::File(hudSnapDir).getChildFile("hud_" + name + ".png");
    file.deleteFile();
    juce::FileOutputStream os(file);
    if (os.openedOk())
        juce::PNGImageFormat().writeImageToStream(img, os);
}

GHMidiEditor::~GHMidiEditor()
{
    context.detach();
}

void GHMidiEditor::setPanelVisible(bool visible)
{
    panelOpen = visible;
    for (auto* c : std::initializer_list<juce::Component*> {
             &deviceLabel, &deviceBox, &rescanBtn, &vmidiToggle,
             &closeBtn, &learnHint })
        c->setVisible(visible);
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
        refreshMappingRows();
    }
    else
        proc.guitar().cancelLearn();
    updateHudButtons();
    repaint();
}

void GHMidiEditor::setHelpVisible(bool visible)
{
    helpOpen = visible;
    helpCloseBtn.setVisible(visible);
    moreBtn.setVisible(visible);
    updateHudButtons();
    repaint();
}

void GHMidiEditor::updateHudButtons()
{
    const bool show = ! panelOpen && ! helpOpen;
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
    octPrev.setEnabled(! chart && oct > GuitarService::kOctaveMin);
    octNext.setEnabled(! chart && oct < GuitarService::kOctaveMax);
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

void GHMidiEditor::refreshMappingRows()
{
    for (int t = 0; t < rowDescs.size(); ++t)
        rowDescs[t]->setText(proc.guitar().describeMapping(t), juce::dontSendNotification);
}

void GHMidiEditor::timerCallback()
{
    if (! isShowing())
        return;
    if (hudSnapDir.isNotEmpty())
    {
        // scripted tour of the HUD states (dev only, standalone only)
        auto& svc = proc.guitar();
        switch (++hudSnapTick)
        {
            case 45:  saveHudSnapshot("chords"); break;
            case 50:  svc.uiMode = 1; svc.uiKey = 7; svc.uiOctave = 12; break;
            case 60:  saveHudSnapshot("notes"); break;
            case 65:  svc.uiMode = 2; svc.uiOctave = 36;
                      svc.uiButtonBits = (1 << GuitarService::LMinus) | (1 << GuitarService::LStickY)
                                       | (1 << GuitarService::LPlus);
                      break;
            case 75:  saveHudSnapshot("solo_maxoct"); break;
            case 77:  svc.uiMode = 3; svc.uiButtonBits = 0; svc.announceFrets(0x03); break;
            case 79:  saveHudSnapshot("chart"); break;
            case 80:  setHelpVisible(true); break;
            case 90:  saveHudSnapshot("help"); break;
            case 95:  setHelpVisible(false); setPanelVisible(true); break;
            case 105: saveHudSnapshot("settings"); break;
            case 135: juce::JUCEApplicationBase::quit(); break;
            default: break;
        }
    }
    updateHudButtons();
    if (panelOpen)
    {
        auto& svc = proc.guitar();
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
                                     : (lt >= GuitarService::LWhammy
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
    r.removeFromTop(34);  // painted title
    auto devRow = r.removeFromTop(24);
    deviceLabel.setBounds(devRow.removeFromLeft(76));
    rescanBtn.setBounds(devRow.removeFromRight(72));
    devRow.removeFromRight(6);
    deviceBox.setBounds(devRow);
    r.removeFromTop(10);
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
        r.removeFromTop(2);
    }
    r.removeFromTop(8);
    vmidiToggle.setBounds(r.removeFromTop(24));

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
        const char* labels[4] = { "MODE", "STRUM SPREAD", notes ? "BASE" : "KEY", "OCTAVE" };
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
    if ((! found || empty) && ! panelOpen && ! helpOpen)
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
        g.drawText("v1.1", (int) pb.getX() + 20, (int) pb.getBottom() - 30,
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
        line("WHAMMY", "bend the note");
        line("MINUS", "switch mode");
        line("PLUS", "tap through strum speeds (0-50ms)");
        line("JOYSTICK", "left/right = key - up/down = octave");
        line("ON SCREEN", "the bottom arrows do all of the above too");
        rowY += 8;
        section("MODES");
        line("CHORDS", "every fret is a chord in your key. can't miss", 70);
        line("SOLO", "5 scale notes, strum any stack. can't miss", 70);
        line("NOTES", "fret combos pick all 32 notes. full control", 70);
        line("CHART", "frets = Clone Hero lanes. record a rough chart", 70);
    }
}
