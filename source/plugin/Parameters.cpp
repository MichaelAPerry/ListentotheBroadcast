#include "Parameters.h"

namespace ltb::params
{
namespace
{
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

juce::StringArray names (const char* const* items, int count)
{
    juce::StringArray out;
    for (int i = 0; i < count; ++i)
        out.add (items[i]);
    return out;
}

void addChoice (Layout& l, const juce::String& id, const juce::String& name, const juce::StringArray& items, int def)
{
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def));
}

void addInt (Layout& l, const juce::String& id, const juce::String& name, int lo, int hi, int def)
{
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name, lo, hi, def));
}

enum class Format { decimal1, percent };

void addFloat (Layout& l, const juce::String& id, const juce::String& name, float lo, float hi, float def,
               const juce::String& unit = {}, Format format = Format::decimal1)
{
    auto attributes = juce::AudioParameterFloatAttributes().withLabel (unit);
    if (format == Format::percent)
        attributes = attributes
                         .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })
                         .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue() / 100.0f; });
    else
        attributes = attributes.withStringFromValueFunction ([unit] (float v, int) {
            return juce::String (v, 1) + (unit.isEmpty() ? "" : " " + unit);
        });
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                        juce::NormalisableRange<float> (lo, hi), def, attributes));
}

void addBool (Layout& l, const juce::String& id, const juce::String& name, bool def)
{
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
}
} // namespace

juce::String rowId (int kind, const char* suffix) { return juce::String (kKinds[kind].id) + "_" + suffix; }

Layout createLayout()
{
    Layout l;
    const auto d = EngineSettings::defaults();

    addChoice (l, kSync, "Sync", { "Follow host transport", "Free-run" }, 0);
    addBool (l, kHostTempo, "Use host tempo", true);
    addFloat (l, kBpm, "Tempo", 40.0f, 200.0f, 84.0f, "bpm");
    addFloat (l, kSwing, "Swing", 0.0f, 1.0f, 0.0f, {}, Format::percent);
    addFloat (l, kLevel, "Level", -40.0f, 6.0f, -6.0f, "dB");
    addFloat (l, kReverb, "Reverb", 0.0f, 1.0f, 0.35f, {}, Format::percent);
    addBool (l, kSynthOn, "Built-in sound", true);
    addBool (l, kMidiOut, "MIDI out", true);
    addChoice (l, kMicrotones, "MIDI microtones", { "Pitch bend", "Round to semitone" }, 0);
    addInt (l, kBendRange, "MIDI bend range", 1, 24, 2);
    addInt (l, kTrafficCC, "Traffic CC (MIDI)", -1, 127, 1);
    addInt (l, kMaxNotes, "Max notes per step", 1, 16, d.maxNotesPerStep);

    juce::StringArray scaleNames;
    for (const auto& s : kScales)
        scaleNames.add (s.name);
    addChoice (l, kRoot, "Root", names (kNoteNames, 12), d.root);
    addChoice (l, kScale, "Scale", scaleNames, d.scale);
    addInt (l, kOctave, "Octave", 1, 7, d.baseOctave);
    addChoice (l, kChordDegree, "Chord degree", { "I", "II", "III", "IV", "V", "VI", "VII" }, d.chordDegree);
    addInt (l, kChordSize, "Chord size", 1, 6, d.chordSize);
    addChoice (l, kChordStack, "Stacking", { "Clusters", "Thirds", "Fourths", "Fifths" }, d.chordStack - 1);
    addInt (l, kChordSpread, "Voicing spread", 0, 2, d.chordSpread);
    juce::StringArray progNames;
    for (const auto& p : kProgressions)
        progNames.add (p.name);
    addChoice (l, kProgression, "Progression", progNames, d.progression);
    addChoice (l, kChangeMode, "Change chord", { "Every N bars", "When a new device appears" }, 0);
    addInt (l, kChangeBars, "Bars per chord", 1, 16, d.changeBars);

    for (int k = 0; k < kNumKinds; ++k)
    {
        const auto& r = d.rows[(size_t) k];
        const juce::String label = kKinds[k].label;
        addBool (l, rowId (k, "on"), label + " on", r.on);
        addChoice (l, rowId (k, "role"), label + " role", names (kRoleNames, kNumRoles), r.role);
        addChoice (l, rowId (k, "sound"), label + " sound", names (kSoundNames, kNumSounds), r.sound);
        addInt (l, rowId (k, "ch"), label + " MIDI channel", 1, 16, r.channel);
        addInt (l, rowId (k, "oct"), label + " octave", -3, 3, r.octave);
        addChoice (l, rowId (k, "rhythm"), label + " rhythm", names (kDivisionNames, kNumDivisions), r.division);
        addInt (l, rowId (k, "len"), label + " length", 1, 128, r.length);
        addInt (l, rowId (k, "vel"), label + " velocity", 1, 127, r.velocity);
        addInt (l, rowId (k, "max"), label + " max per bar", 1, 16, r.density);
        addFloat (l, rowId (k, "chance"), label + " chance", 0.0f, 1.0f, r.chance, {}, Format::percent);
        addInt (l, rowId (k, "note"), label + " hit note", 0, 127, r.hitNote);
    }
    return l;
}

Snapshot::Snapshot (juce::AudioProcessorValueTreeState& s)
{
    auto p = [&s] (const juce::String& id) {
        auto* v = s.getRawParameterValue (id);
        jassert (v != nullptr);
        return v;
    };
    sync = p (kSync);
    hostTempo = p (kHostTempo);
    bpmParam = p (kBpm);
    swing = p (kSwing);
    root = p (kRoot);
    scale = p (kScale);
    octave = p (kOctave);
    chordDegree = p (kChordDegree);
    chordSize = p (kChordSize);
    chordStack = p (kChordStack);
    chordSpread = p (kChordSpread);
    progression = p (kProgression);
    changeMode = p (kChangeMode);
    changeBars = p (kChangeBars);
    maxNotes = p (kMaxNotes);
    level = p (kLevel);
    reverb = p (kReverb);
    synthOnParam = p (kSynthOn);
    midiOutParam = p (kMidiOut);
    microtones = p (kMicrotones);
    bendRangeParam = p (kBendRange);
    trafficCCParam = p (kTrafficCC);
    for (int k = 0; k < kNumKinds; ++k)
        for (size_t i = 0; i < rows[(size_t) k].size(); ++i)
            rows[(size_t) k][i] = p (rowId (k, kRowSuffixes[i]));
}

EngineSettings Snapshot::engineSettings() const noexcept
{
    auto i = [] (const std::atomic<float>* v) { return (int) std::lround (get (v)); };
    EngineSettings s;
    s.swing = get (swing);
    s.root = i (root);
    s.scale = i (scale);
    s.baseOctave = i (octave);
    s.chordDegree = i (chordDegree);
    s.chordSize = i (chordSize);
    s.chordStack = i (chordStack) + 1;
    s.chordSpread = i (chordSpread);
    s.progression = i (progression);
    s.changeOnNewDevice = i (changeMode) == 1;
    s.changeBars = i (changeBars);
    s.maxNotesPerStep = i (maxNotes);
    s.level = 1.0f;
    for (int k = 0; k < kNumKinds; ++k)
    {
        const auto& p = rows[(size_t) k];
        auto& r = s.rows[(size_t) k];
        r.on = get (p[0]) > 0.5f;
        r.role = i (p[1]);
        r.sound = i (p[2]);
        r.channel = i (p[3]);
        r.octave = i (p[4]);
        r.division = i (p[5]);
        r.length = i (p[6]);
        r.velocity = i (p[7]);
        r.density = i (p[8]);
        r.chance = get (p[9]);
        r.hitNote = i (p[10]);
    }
    return s;
}
} // namespace ltb::params
