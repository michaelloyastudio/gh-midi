#include "PluginEditor.h"
#include <BinaryData.h>

namespace {
const juce::Colour gemColours[5] = {
    juce::Colour(0xff33cc3d), juce::Colour(0xffe63232), juce::Colour(0xfff2d02a),
    juce::Colour(0xff3378e6), juce::Colour(0xfff2921f),
};
const juce::Colour openBarColour(0xffa05ff0);
const juce::Colour gold(0xfff2d02a);
const char* modeNames[3] = { "CHORDS", "NOTES", "SOLO" };
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

juce::Rectangle<int> panelBounds(int W, int H)
{
    return { W / 2 - 250, H / 2 - 275, 500, 550 };
}
juce::Rectangle<int> helpBounds(int W, int H)
{
    return { W / 2 - 230, H / 2 - 225, 460, 450 };
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

    addAndMakeVisible(strumLabel);
    strumLabel.setText("STRUM", juce::dontSendNotification);
    strumLabel.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    strumLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.45f));
    addAndMakeVisible(strumSlider);
    strumSlider.setRange(0.0, 30.0, 1.0);
    strumSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 20);
    strumSlider.setTextValueSuffix(" ms");
    strumSlider.onValueChange = [this]
    { proc.guitar().strumRollMs = (int) strumSlider.getValue(); };
    strumSlider.onDragEnd = [this] { proc.guitar().requestSave(); };
    strumSlider.setValue(proc.guitar().strumRollMs.load(), juce::dontSendNotification);

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
    addChildComponent(guideBtn);
    guideBtn.onClick = []
    {
        juce::URL("https://michaelloya.studio/gh-midi").launchInDefaultBrowser();
    };

    setSize(720, 620);
    startTimerHz(30);
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
    repaint();
}

void GHMidiEditor::setHelpVisible(bool visible)
{
    helpOpen = visible;
    helpCloseBtn.setVisible(visible);
    guideBtn.setVisible(visible);
    repaint();
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
    gearBtn.setBounds(getWidth() - 300, 12, 92, 26);
    helpBtn.setBounds(getWidth() - 336, 12, 30, 26);

    // settings panel
    auto r = panelBounds(getWidth(), getHeight()).reduced(20);
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
        rowNames[t]->setBounds(row.removeFromLeft(112));
        rowClear[t]->setBounds(row.removeFromRight(30).reduced(0, 1));
        row.removeFromRight(4);
        rowLearn[t]->setBounds(row.removeFromRight(62).reduced(0, 1));
        rowDescs[t]->setBounds(row);
        r.removeFromTop(2);
    }
    r.removeFromTop(8);
    vmidiToggle.setBounds(r.removeFromTop(24));
    closeBtn.setBounds(r.removeFromBottom(28).withSizeKeepingCentre(110, 28));

    // strum spread: always at hand, bottom-right
    strumLabel.setBounds(getWidth() - 208, getHeight() - 46, 48, 16);
    strumSlider.setBounds(getWidth() - 162, getHeight() - 50, 146, 24);

    // help overlay
    auto hb = helpBounds(getWidth(), getHeight());
    guideBtn.setBounds(hb.getCentreX() + 34, hb.getBottom() - 94, 44, 30);
    helpCloseBtn.setBounds(hb.getCentreX() - 55, hb.getBottom() - 44, 110, 26);
}

void GHMidiEditor::paint(juce::Graphics& g)
{
    const float W = (float) getWidth();
    const float H = (float) getHeight();
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.setFont(ghFont(28.0f));
    g.drawText("GH MIDI", 20, 12, 180, 28, juce::Justification::left);

    // chord/note name pop
    {
        const float age = (float) (now - proc.guitar().lastPlayedAt.load());
        if (age >= 0.0f && age < 1.6f)
        {
            const float pop = 1.0f + 0.35f * std::exp(-age * 9.0f);
            const float a = juce::jlimit(0.0f, 1.0f, 1.8f - age * 1.2f);
            g.setColour(gold.withAlpha(a));
            g.setFont(ghFont(48.0f * pop));
            g.drawText(proc.guitar().getLastPlayed(), 0, (int) (H * 0.07f), getWidth(), 58,
                       juce::Justification::centred);
        }
    }

    // mode / key plate, with actual labels
    {
        g.setColour(juce::Colour(0x8810101a));
        g.fillRoundedRectangle(W - 196.0f, 12.0f, 178.0f, 30.0f, 6.0f);
        const int mode = proc.guitar().uiMode.load() % 3;
        const int uiK = proc.guitar().uiKey.load() % 12;
        const bool pro = mode == 1;
        g.setColour(juce::Colours::white.withAlpha(0.38f));
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.drawText("MODE", (int) W - 188, 14, 80, 10, juce::Justification::left);
        g.drawText(pro ? "BASE" : "KEY", (int) W - 100, 14, 84, 10, juce::Justification::right);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(ghFont(18.0f));
        juce::String modeTxt(modeNames[mode]);
        g.drawText(modeTxt, (int) W - 188, 22, 84, 18, juce::Justification::left);
        juce::String keyTxt;
        if (pro)
        {
            const int baseNote = 40 + uiK + proc.guitar().uiOctave.load();
            keyTxt = juce::String(keyNames[baseNote % 12]) + juce::String(baseNote / 12 - 1);
        }
        else
            keyTxt = keyNames[uiK];
        g.drawText(keyTxt, (int) W - 100, 22, 84, 18, juce::Justification::right);
    }

    // bottom legend, tidy
    {
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        const char* parts[] = { "MINUS  mode", "PLUS  octave", "STICK  key", "?  guide" };
        int x = 16;
        for (auto* part : parts)
        {
            g.setColour(juce::Colours::white.withAlpha(0.42f));
            g.drawText(part, x, (int) H - 24, 110, 14, juce::Justification::left);
            x += 104;
            g.setColour(gold.withAlpha(0.35f));
            if (part != parts[3])
                g.fillEllipse((float) x - 12.0f, H - 18.5f, 3.0f, 3.0f);
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

    // whammy meter
    {
        const float wham = proc.guitar().uiWhammy.load();
        juce::Rectangle<float> bar(W * 0.5f - W * 0.14f, H - 48.0f, W * 0.28f, 9.0f);
        g.setColour(juce::Colour(0xcc15151f));
        g.fillRoundedRectangle(bar, 4.0f);
        if (wham > 0.0f)
        {
            juce::ColourGradient wg(openBarColour.brighter(0.5f), bar.getX(), bar.getCentreY(),
                                    openBarColour.darker(0.3f), bar.getRight(), bar.getCentreY(), false);
            g.setGradientFill(wg);
            g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * wham), 4.0f);
        }
        g.setColour(juce::Colours::white.withAlpha(0.30f));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("WHAMMY", (int) bar.getX(), (int) (bar.getY() - 12), (int) bar.getWidth(), 11,
                   juce::Justification::centred);
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
        g.drawText("v1.1", (int) pb.getX() + 20, (int) pb.getY() + 12,
                   (int) pb.getWidth() - 40, 26, juce::Justification::right);
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
        auto line = [&](const juce::String& head, const juce::String& body)
        {
            g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.drawText(head, hb.getX() + 34, rowY, 116, 16, juce::Justification::left);
            g.setFont(juce::Font(juce::FontOptions(12.5f)));
            g.setColour(juce::Colours::white.withAlpha(0.62f));
            g.drawText(body, hb.getX() + 152, rowY, hb.getWidth() - 186, 16, juce::Justification::left);
            rowY += 20;
        };
        section("CONTROLS");
        line("FRETS + STRUM", "play - up and down strums feel different");
        line("WHAMMY", "bend the note");
        line("PLUS", "octave up");
        line("MINUS", "switch mode");
        line("STICK", "change key (any direction)");
        rowY += 8;
        section("MODES");
        line("CHORDS", "every fret is a chord in your key - can't miss");
        line("", "  neighbours = 7th chords / stretches = the");
        line("", "  missing ones (iii, III, bVII, iv, bVI)");
        line("SOLO", "5 frets = 5 safe scale notes. simple, can't miss");
        line("NOTES", "fret COMBOS unlock all 32 notes. full control");
        g.setColour(gold);
        g.setFont(ghFont(19.0f));
        g.drawText("FULL GUIDE", hb.getCentreX() - 112, hb.getBottom() - 92,
                   140, 26, juce::Justification::left);
        g.setColour(juce::Colours::white.withAlpha(0.38f));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("(leaves the plugin and opens your web browser)",
                   hb.getX(), hb.getBottom() - 58, hb.getWidth(), 14,
                   juce::Justification::centred);
    }
}
