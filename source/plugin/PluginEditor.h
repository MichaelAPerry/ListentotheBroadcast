#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

class ListenEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    struct Field
    {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Component> control;
    };

    explicit ListenEditor (ListenProcessor&);
    ~ListenEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Theme : juce::LookAndFeel_V4
    {
        Theme();
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
    };

    struct Meter : juce::Component
    {
        std::function<float()> value;
        float shown = 0.0f;
        void paint (juce::Graphics&) override;
    };

    struct Row
    {
        int kind = 0;
        juce::Label name, counts;
        Meter meter;
        juce::ToggleButton on;
        juce::ComboBox role, sound, rhythm;
        juce::Slider channel, octave, length, velocity, maxPerBar, chance, hitNote;
        juce::TextButton test { juce::String::fromUTF8 ("\xe2\x96\xb6") };
    };

    void timerCallback() override;
    Field& addSlider (std::vector<Field>& group, const char* id, const juce::String& label, const juce::String& suffix = {});
    Field& addCombo (std::vector<Field>& group, const char* id, const juce::String& label);
    Field& addToggle (std::vector<Field>& group, const char* id, const juce::String& label);
    void attachSlider (juce::Slider& s, const juce::String& id);
    void attachCombo (juce::ComboBox& c, const juce::String& id);
    void layoutGroup (std::vector<Field>& group, juce::Rectangle<int> area);

    ListenProcessor& processor;
    Theme theme;
    juce::TooltipWindow tooltips { this, 600 };

    juce::Label title, status;
    juce::TextButton panic { "Panic" }, testAll { "Test sound" };
    std::vector<Field> beatFields, harmonyFields;
    std::vector<std::unique_ptr<Row>> rows;
    juce::TextEditor feed;
    juce::Label portSummary;

    juce::Rectangle<int> beatArea, harmonyArea, feedArea, tableArea;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> columnHeaders;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;

    juce::String lastFeedTail;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ListenEditor)
};
