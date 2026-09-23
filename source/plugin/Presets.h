// Factory presets, exposed to the host as programs and in the plugin's header menu.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ltb::presets
{
int count();
juce::String name (int index);
// Resets every parameter to its default, then applies the preset's settings (notifying the host).
void apply (juce::AudioProcessorValueTreeState& state, int index);
} // namespace ltb::presets
