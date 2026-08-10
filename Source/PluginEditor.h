#pragma once

#include "PluginProcessor.h"

//==============================================================================
class AutoMixerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AutoMixerLookAndFeel()
    {
        setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (0xffff5c1a));
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff2a2a2e));
        setColour (juce::Slider::thumbColourId,               juce::Colours::white);
        setColour (juce::Label::textColourId,                 juce::Colour (0xffcfcfd4));
        setColour (juce::ComboBox::backgroundColourId,        juce::Colour (0xff222228));
        setColour (juce::ComboBox::outlineColourId,           juce::Colour (0xff34343c));
        setColour (juce::ComboBox::textColourId,              juce::Colour (0xffe6e6ea));
        setColour (juce::ComboBox::arrowColourId,             juce::Colour (0xffff5c1a));
        setColour (juce::PopupMenu::backgroundColourId,       juce::Colour (0xff1b1b1f));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xffff5c1a));
        setColour (juce::TextButton::buttonColourId,          juce::Colour (0xff222228));
        setColour (juce::TextButton::buttonOnColourId,        juce::Colour (0xffff5c1a));
        setColour (juce::TextButton::textColourOffId,         juce::Colour (0xffe6e6ea));
        setColour (juce::TextButton::textColourOnId,          juce::Colours::white);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height)
                          .reduced (6.0f);
        auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto centre = bounds.getCentre();
        auto angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const float lineW = radius * 0.14f;
        const float arcR  = radius - lineW * 0.5f;

        juce::Path bg;
        bg.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                          rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (findColour (juce::Slider::rotarySliderOutlineColourId));
        g.strokePath (bg, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

        juce::Path val;
        val.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                           rotaryStartAngle, angle, true);
        g.setColour (findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (val, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        auto knobR = arcR - lineW * 1.4f;
        juce::ColourGradient kg (juce::Colour (0xff26262c), centre.x, centre.y - knobR,
                                 juce::Colour (0xff141417), centre.x, centre.y + knobR, false);
        g.setGradientFill (kg);
        g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);

        juce::Path tick;
        tick.addRoundedRectangle (-lineW * 0.35f, -knobR, lineW * 0.7f, knobR * 0.55f, lineW * 0.3f);
        g.setColour (juce::Colours::white);
        g.fillPath (tick, juce::AffineTransform::rotation (angle).translated (centre));
    }
};

//==============================================================================
class UndergroundVoxEditor : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit UndergroundVoxEditor (UndergroundVoxProcessor&);
    ~UndergroundVoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void makeKnob (Knob& k, const juce::String& paramID, const juce::String& name);
    void layoutKnobGrid (juce::Rectangle<int> area, Knob* knobs[], int count, int cols);

    UndergroundVoxProcessor& processor;
    AutoMixerLookAndFeel lnf;

    Knob gate, tune, clean, punch, deess, air, drive, echo, space, duck, output;

    juce::ComboBox keyBox, scaleBox;
    juce::TextButton analyzeButton { "ANALYZE" };
    juce::TextButton liveButton    { "LIVE" };
    juce::TextButton tuneOnButton  { "TUNE" };
    juce::TextButton fxOnButton    { "FX" };
    juce::Label statusLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttachment, scaleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   liveAttachment, tuneOnAttachment, fxOnAttachment;

    juce::Rectangle<int> tunePanel, voicePanel, fxPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndergroundVoxEditor)
};
