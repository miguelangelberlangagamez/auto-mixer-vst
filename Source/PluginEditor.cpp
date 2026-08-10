#include "PluginEditor.h"

UndergroundVoxEditor::UndergroundVoxEditor (UndergroundVoxProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);

    makeKnob (tune,   "tune",   "TUNE");
    makeKnob (gate,   "gate",   "GATE");
    makeKnob (clean,  "clean",  "EQ");
    makeKnob (punch,  "punch",  "PUNCH");
    makeKnob (deess,  "deess",  "DE-ESS");
    makeKnob (air,    "air",    "AIR");
    makeKnob (drive,  "drive",  "COLOR");
    makeKnob (echo,   "echo",   "ECHO");
    makeKnob (space,  "space",  "SPACE");
    makeKnob (duck,   "duck",   "DUCK");
    makeKnob (output, "output", "OUTPUT");

    keyBox.addItemList ({ "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 1);
    addAndMakeVisible (keyBox);
    keyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "key", keyBox);

    scaleBox.addItemList ({ "Menor", "Mayor", "Cromatica" }, 1);
    addAndMakeVisible (scaleBox);
    scaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "scale", scaleBox);

    analyzeButton.onClick = [this]
    {
        if (! processor.getTuner().isAnalyzing())
        {
            processor.getTuner().startKeyAnalysis();
            statusLabel.setText ("dale play y canta...", juce::dontSendNotification);
        }
    };
    addAndMakeVisible (analyzeButton);

    liveButton.setClickingTogglesState (true);
    liveButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe03434));
    liveButton.setTooltip ("Modo directo: latencia ~21 ms (estudio: ~43 ms)");
    addAndMakeVisible (liveButton);
    liveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "live", liveButton);

    tuneOnButton.setClickingTogglesState (true);
    tuneOnButton.setTooltip ("Auto-tune ON/OFF (apagalo para usar un afinador externo)");
    addAndMakeVisible (tuneOnButton);
    tuneOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "tuneOn", tuneOnButton);

    fxOnButton.setClickingTogglesState (true);
    fxOnButton.setTooltip ("Todos los demas efectos ON/OFF (bypass de la cadena)");
    addAndMakeVisible (fxOnButton);
    fxOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "fxOn", fxOnButton);

    statusLabel.setFont (juce::FontOptions (12.0f));
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffff5c1a));
    statusLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (statusLabel);

    startTimerHz (5);
    setSize (920, 430);
}

UndergroundVoxEditor::~UndergroundVoxEditor()
{
    setLookAndFeel (nullptr);
}

void UndergroundVoxEditor::timerCallback()
{
    auto& tuner = processor.getTuner();

    if (tuner.isAnalyzing())
    {
        statusLabel.setText ("analizando... "
                                 + juce::String ((int) std::ceil (tuner.analysisSecondsLeft())) + " s",
                             juce::dontSendNotification);
    }
    else if (tuner.consumeFinishedFlag())
    {
        keyBox.setSelectedItemIndex (tuner.getResultKey(), juce::sendNotification);
        scaleBox.setSelectedItemIndex (tuner.getResultScale() == 1 ? 1 : 0, juce::sendNotification);
        statusLabel.setText ("detectado: " + tuner.getResultText(), juce::dontSendNotification);
    }
}

void UndergroundVoxEditor::makeKnob (Knob& k, const juce::String& paramID, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    k.slider.setPopupDisplayEnabled (true, true, this);
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, paramID, k.slider);
}

static void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
{
    auto rf = r.toFloat();
    g.setColour (juce::Colour (0xff1a1a1f));
    g.fillRoundedRectangle (rf, 10.0f);
    g.setColour (juce::Colour (0xff2b2b32));
    g.drawRoundedRectangle (rf.reduced (0.5f), 10.0f, 1.0f);

    g.setColour (juce::Colour (0xff8a8a92));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (title, r.removeFromTop (24), juce::Justification::centred);
}

void UndergroundVoxEditor::paint (juce::Graphics& g)
{
    juce::ColourGradient bg (juce::Colour (0xff0d0d10), 0.0f, 0.0f,
                             juce::Colour (0xff17171c), 0.0f, (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();

    // barra de acento superior
    g.setColour (juce::Colour (0xffff5c1a));
    g.fillRect (0, 0, getWidth(), 3);

    // titulo
    auto header = getLocalBounds().removeFromTop (52);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    {
        juce::GlyphArrangement ga;
        const juce::String a = "AUTO-", b = "MIXER";
        const float fw = juce::FontOptions (26.0f, juce::Font::bold).getHeight();
        juce::ignoreUnused (fw);
        const int total = juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)), a + b);
        const int x0 = header.getCentreX() - total / 2;
        g.setColour (juce::Colours::white);
        g.drawText (a, x0, header.getY(), total, header.getHeight(), juce::Justification::centredLeft);
        const int aw = juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)), a);
        g.setColour (juce::Colour (0xffff5c1a));
        g.drawText (b, x0 + aw, header.getY(), total - aw, header.getHeight(), juce::Justification::centredLeft);
    }

    drawPanel (g, tunePanel,  "AFINACION");
    drawPanel (g, voicePanel, "VOZ");
    drawPanel (g, fxPanel,    "ESPACIO Y SALIDA");

    // solo la version, discreta, abajo a la derecha
    g.setColour (juce::Colour (0xff5a5a60));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("v5.5", getLocalBounds().reduced (10).removeFromBottom (14),
                juce::Justification::centredRight);
}

void UndergroundVoxEditor::layoutKnobGrid (juce::Rectangle<int> area, Knob* knobs[], int count, int cols)
{
    const int rows = (count + cols - 1) / cols;
    const int cw = area.getWidth() / cols;
    const int ch = area.getHeight() / rows;
    for (int i = 0; i < count; ++i)
    {
        auto cell = juce::Rectangle<int> (area.getX() + (i % cols) * cw,
                                          area.getY() + (i / cols) * ch, cw, ch);
        auto lab = cell.removeFromBottom (16);
        knobs[i]->label.setBounds (lab);
        const int side = juce::jmin (cell.getWidth(), cell.getHeight());
        knobs[i]->slider.setBounds (cell.withSizeKeepingCentre (side, side));
    }
}

void UndergroundVoxEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (48);
    area.removeFromBottom (16);

    tunePanel  = area.removeFromLeft (232);
    area.removeFromLeft (12);
    fxPanel    = area.removeFromRight (300);
    area.removeFromRight (12);
    voicePanel = area;

    // --- panel AFINACION ---
    auto t = tunePanel.reduced (14);
    t.removeFromTop (22);
    {
        auto knobArea = t.removeFromTop (128);
        auto lab = knobArea.removeFromBottom (16);
        tune.label.setBounds (lab);
        const int side = juce::jmin (knobArea.getWidth(), knobArea.getHeight());
        tune.slider.setBounds (knobArea.withSizeKeepingCentre (side, side));
    }
    t.removeFromTop (8);
    {
        auto row = t.removeFromTop (28);
        keyBox.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
        row.removeFromLeft (6);
        scaleBox.setBounds (row);
    }
    t.removeFromTop (8);
    {
        auto row = t.removeFromTop (30);
        analyzeButton.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
        row.removeFromLeft (6);
        liveButton.setBounds (row);
    }
    t.removeFromTop (6);
    {
        auto row = t.removeFromTop (30);
        tuneOnButton.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
        row.removeFromLeft (6);
        fxOnButton.setBounds (row);
    }
    t.removeFromTop (6);
    statusLabel.setBounds (t.removeFromTop (20));

    // --- panel VOZ: 3 x 2 ---
    {
        auto v = voicePanel.reduced (12);
        v.removeFromTop (24);
        Knob* ks[6] = { &gate, &clean, &punch, &deess, &air, &drive };
        layoutKnobGrid (v, ks, 6, 3);
    }

    // --- panel ESPACIO Y SALIDA: 2 x 2 ---
    {
        auto f = fxPanel.reduced (12);
        f.removeFromTop (24);
        Knob* ks[4] = { &echo, &space, &duck, &output };
        layoutKnobGrid (f, ks, 4, 2);
    }
}
