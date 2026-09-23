#include "Presets.h"

#include "Parameters.h"

#include <cstring>

namespace ltb::presets
{
namespace
{
struct Setting
{
    juce::String id;
    float value; // plain value; choice parameters take the item index
};

struct Preset
{
    const char* name;
    std::vector<Setting> settings;
};

float scaleIndex (const char* scaleName)
{
    for (int i = 0; i < kNumScales; ++i)
        if (std::strcmp (kScales[i].name, scaleName) == 0)
            return (float) i;
    jassertfalse;
    return 0.0f;
}

juce::String row (int kind, const char* suffix) { return params::rowId (kind, suffix); }

// Choice indices, for readability below.
enum { Note = 0, Chord, PadRole, BassRole, Hit };
enum { Bell = 0, Pluck, Pad, Glass, Bass, Hat, MidiOnly };
enum { Sixteenth = 0, Eighth, Quarter, Half, Bar };
enum { Clusters = 0, Thirds, Fourths, Fifths };
enum { Hold = 0, Drift, Pop, Rise, Pendulum, Andalusian, RandomWalk };

const std::vector<Preset>& all()
{
    namespace P = params;
    static const std::vector<Preset> presets = {
        { "Night Drift", {} }, // the defaults: D Dorian, slow I-IV-I-V
        { "Maqam Rast",
          { { P::kScale, scaleIndex ("Rast") }, { P::kRoot, 0 }, { P::kProgression, Pendulum }, { P::kBpm, 76 },
            { P::kReverb, 0.5f }, { row (kMdns, "sound"), Glass }, { row (kLlmnr, "sound"), Pluck },
            { row (kLanSync, "on"), 0 } } },
        { "Kyoto Garden",
          { { P::kScale, scaleIndex ("In (Miyako-bushi)") }, { P::kRoot, 4 }, { P::kProgression, Hold },
            { P::kChordStack, Fourths }, { P::kBpm, 64 }, { P::kReverb, 0.65f }, { row (kMdns, "sound"), Pluck },
            { row (kMdns, "rhythm"), Quarter }, { row (kLanSync, "on"), 0 } } },
        { "Raga Todi",
          { { P::kScale, scaleIndex ("Todi") }, { P::kRoot, 2 }, { P::kProgression, Hold }, { P::kChordSize, 2 },
            { P::kChordStack, Fifths }, { P::kBpm, 60 }, { P::kReverb, 0.55f }, { row (kNetbios, "len"), 64 },
            { row (kNetbios, "rhythm"), Bar }, { row (kLanSync, "on"), 0 } } },
        { "Gamelan",
          { { P::kScale, scaleIndex ("Slendro (5-equal)") }, { P::kRoot, 2 }, { P::kProgression, RandomWalk },
            { P::kChordStack, Thirds }, { P::kBpm, 92 }, { P::kReverb, 0.4f }, { row (kLlmnr, "sound"), Bell },
            { row (kWsd, "sound"), Bell }, { row (kLanSync, "role"), Note }, { row (kLanSync, "sound"), Pluck } } },
        { "Sparse Bells",
          { { P::kScale, scaleIndex ("Major Pentatonic") }, { P::kRoot, 5 }, { P::kBpm, 70 }, { P::kReverb, 0.7f },
            { P::kMaxNotes, 3 }, { row (kMdns, "chance"), 0.5f }, { row (kLlmnr, "sound"), Bell },
            { row (kWsd, "sound"), Bell }, { row (kSsdp, "on"), 0 }, { row (kLanSync, "on"), 0 } } },
        { "Pulse",
          { { P::kScale, scaleIndex ("Minor Pentatonic") }, { P::kRoot, 9 }, { P::kBpm, 112 }, { P::kSwing, 0.35f },
            { P::kProgression, Pop }, { P::kChangeBars, 2 }, { P::kReverb, 0.25f },
            { row (kLanSync, "rhythm"), Eighth }, { row (kLanSync, "max"), 16 }, { row (kLanSync, "vel"), 70 },
            { row (kNetbios, "rhythm"), Quarter } } },
    };
    return presets;
}
} // namespace

int count() { return (int) all().size(); }

juce::String name (int index)
{
    return juce::isPositiveAndBelow (index, count()) ? juce::String (all()[(size_t) index].name) : juce::String();
}

void apply (juce::AudioProcessorValueTreeState& state, int index)
{
    if (! juce::isPositiveAndBelow (index, count()))
        return;
    auto set = [&state] (const juce::String& id, float normalised) {
        if (auto* p = state.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (normalised);
            p->endChangeGesture();
        }
    };
    for (auto* p : state.processor.getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (p))
            set (withId->getParameterID(), withId->getDefaultValue());
    for (const auto& s : all()[(size_t) index].settings)
        if (auto* p = state.getParameter (s.id))
            set (s.id, p->convertTo0to1 (s.value));
}
} // namespace ltb::presets
