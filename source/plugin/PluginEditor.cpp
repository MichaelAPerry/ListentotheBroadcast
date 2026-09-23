#include "PluginEditor.h"
#include "Presets.h"
#include <cstring>

using namespace ltb;
namespace P = ltb::params;

namespace
{
const juce::Colour kBg (0xff0e1116), kPanel (0xff161b22), kLine (0xff2a323d), kText (0xffd7dde5),
    kMuted (0xff8391a2), kAccent (0xff7cc4ff), kAccent2 (0xffb89cff), kWarn (0xffff8a7a), kOk (0xff7be0a8);

constexpr int kMargin = 14;
constexpr int kHeaderH = 52;
constexpr int kRowH = 34;

// Traffic → Sound table columns: header text and width.
const std::pair<const char*, int> kColumns[] = {
    { "Traffic", 206 }, { "Activity", 70 }, { "On", 34 }, { "Role", 80 }, { "Sound", 98 }, { "MIDI ch", 58 },
    { "Octave", 60 }, { "Rhythm", 74 }, { "Length", 60 }, { "Velocity", 80 }, { "Max/bar", 58 }, { "Chance", 78 },
    { "Hit note", 56 }, { "", 34 },
};
constexpr int kColumnGap = 6;
} // namespace

// ---------------------------------------------------------------- theme

ListenEditor::Theme::Theme()
{
    setColourScheme ({ kBg, kPanel, kPanel, kLine, kText, kAccent, juce::Colours::white, kAccent.withAlpha (0.25f), kText });
    setColour (juce::Slider::trackColourId, kAccent);
    setColour (juce::Slider::backgroundColourId, kBg);
    setColour (juce::Slider::thumbColourId, kAccent);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, kBg);
    setColour (juce::ComboBox::outlineColourId, kLine);
    setColour (juce::ComboBox::arrowColourId, kMuted);
    setColour (juce::PopupMenu::backgroundColourId, kPanel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha (0.25f));
    setColour (juce::PopupMenu::headerTextColourId, kAccent2);
    setColour (juce::TextButton::buttonColourId, kPanel);
    setColour (juce::TextButton::textColourOffId, kText);
    setColour (juce::ToggleButton::tickColourId, kAccent);
    setColour (juce::ToggleButton::tickDisabledColourId, kMuted);
    setColour (juce::TextEditor::backgroundColourId, kBg);
    setColour (juce::TextEditor::outlineColourId, kLine);
    setColour (juce::TextEditor::textColourId, kMuted);
    setColour (juce::Label::textColourId, kText);
    setColour (juce::TooltipWindow::backgroundColourId, kPanel);
    setColour (juce::TooltipWindow::textColourId, kText);
    setColour (juce::TooltipWindow::outlineColourId, kLine);
}

void ListenEditor::Theme::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos,
                                            float maxPos, juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
        return;
    }
    // Compact "value in a bar" style for the table.
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (0.5f, 3.0f);
    g.setColour (kBg);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (kAccent.withAlpha (0.28f));
    g.fillRoundedRectangle (r.withWidth (juce::jmax (0.0f, pos - (float) x)), 4.0f);
    g.setColour (kLine);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void ListenEditor::Meter::paint (juce::Graphics& g)
{
    const auto target = value ? juce::jlimit (0.0f, 1.0f, value()) : 0.0f;
    shown = target > shown ? target : shown * 0.85f + target * 0.15f;
    const auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), 8.0f);
    g.setColour (kBg);
    g.fillRoundedRectangle (r, 4.0f);
    if (shown > 0.005f)
    {
        g.setGradientFill (juce::ColourGradient (kAccent, r.getX(), 0, kAccent2, r.getRight(), 0, false));
        g.fillRoundedRectangle (r.withWidth (juce::jmax (8.0f, r.getWidth() * shown)), 4.0f);
    }
}

// ---------------------------------------------------------------- construction

void ListenEditor::attachSlider (juce::Slider& s, const juce::String& id)
{
    sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.state, id, s));
}

void ListenEditor::attachCombo (juce::ComboBox& c, const juce::String& id)
{
    if (c.getNumItems() == 0)
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (processor.state.getParameter (id)))
            c.addItemList (p->choices, 1);
    comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.state, id, c));
}

static void tip (ListenEditor::Field& f, const juce::String& text)
{
    if (auto* t = dynamic_cast<juce::SettableTooltipClient*> (f.control.get()))
        t->setTooltip (text);
    if (f.label != nullptr)
        f.label->setTooltip (text);
}

static std::unique_ptr<juce::Label> makeLabel (const juce::String& text)
{
    auto l = std::make_unique<juce::Label> (juce::String(), text);
    l->setColour (juce::Label::textColourId, kMuted);
    l->setFont (juce::FontOptions (14.0f));
    return l;
}

ListenEditor::Field& ListenEditor::addSlider (std::vector<Field>& group, const char* id, const juce::String& label,
                                              const juce::String& suffix)
{
    auto s = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    if (suffix.isNotEmpty())
        s->setTextValueSuffix (suffix);
    attachSlider (*s, id);
    group.push_back ({ makeLabel (label), std::move (s) });
    addAndMakeVisible (*group.back().label);
    addAndMakeVisible (*group.back().control);
    return group.back();
}

ListenEditor::Field& ListenEditor::addCombo (std::vector<Field>& group, const char* id, const juce::String& label)
{
    auto c = std::make_unique<juce::ComboBox>();
    if (juce::String (id) == P::kScale)
    {
        // Grouped by tradition; item IDs follow the parameter's choice order.
        const char* family = nullptr;
        for (int i = 0; i < kNumScales; ++i)
        {
            if (family == nullptr || std::strcmp (family, kScales[i].family) != 0)
                c->addSectionHeading (family = kScales[i].family);
            c->addItem (juce::String (kScales[i].name) + (kScales[i].isMicrotonal() ? juce::String::fromUTF8 ("  \xc2\xbc") : ""), i + 1);
        }
    }
    attachCombo (*c, id);
    group.push_back ({ makeLabel (label), std::move (c) });
    addAndMakeVisible (*group.back().label);
    addAndMakeVisible (*group.back().control);
    return group.back();
}

ListenEditor::Field& ListenEditor::addToggle (std::vector<Field>& group, const char* id, const juce::String& label)
{
    auto b = std::make_unique<juce::ToggleButton> (label);
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.state, id, *b));
    group.push_back ({ nullptr, std::move (b) });
    addAndMakeVisible (*group.back().control);
    return group.back();
}

ListenEditor::ListenEditor (ListenProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&theme);

    title.setText ("Listen to the Broadcast", juce::dontSendNotification);
    title.setFont (juce::FontOptions (19.0f, juce::Font::bold));
    addAndMakeVisible (title);
    status.setColour (juce::Label::textColourId, kMuted);
    status.setFont (juce::FontOptions (14.0f));
    addAndMakeVisible (status);

    version.setText (juce::String ("v") + LTB_VERSION, juce::dontSendNotification);
    version.setColour (juce::Label::textColourId, kMuted.withAlpha (0.7f));
    version.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (version);

    for (int i = 0; i < presets::count(); ++i)
        presetMenu.addItem (presets::name (i), i + 1);
    presetMenu.setSelectedItemIndex (processor.getCurrentProgram(), juce::dontSendNotification);
    presetMenu.setTooltip ("Factory presets. Choosing one resets every control to that preset.");
    presetMenu.onChange = [this] {
        const int index = presetMenu.getSelectedItemIndex();
        if (index >= 0 && index != processor.getCurrentProgram())
            processor.setCurrentProgram (index);
    };
    addAndMakeVisible (presetMenu);

    panic.setColour (juce::TextButton::textColourOffId, kWarn);
    panic.setTooltip ("Stop every note now (built-in sound and MIDI out)");
    panic.onClick = [this] { processor.requestPanic(); };
    addAndMakeVisible (panic);
    testAll.setTooltip ("Play one note from every enabled row, without waiting for traffic");
    testAll.onClick = [this] {
        for (int k = 0; k < kNumKinds; ++k)
            if (processor.state.getRawParameterValue (P::rowId (k, "on"))->load() > 0.5f)
                processor.requestTest (k);
    };
    addAndMakeVisible (testAll);

    // Sound & beat
    tip (addToggle (beatFields, P::kSynthOn, "Built-in sound"), "Play through the plugin's own voices");
    tip (addToggle (beatFields, P::kMidiOut, "MIDI out"), "Also send notes to other plugins/instruments (one channel per traffic type)");
    addSlider (beatFields, P::kLevel, "Level");
    addSlider (beatFields, P::kReverb, "Reverb");
    tip (addCombo (beatFields, P::kSync, "Sync"), "Follow the host's play position when it's playing; when it's stopped, keep playing on the plugin's own clock");
    addToggle (beatFields, P::kHostTempo, "Use host tempo");
    tip (addSlider (beatFields, P::kBpm, "Tempo"), "Used when the host doesn't report a tempo, or 'Use host tempo' is off");
    addSlider (beatFields, P::kSwing, "Swing");
    addSlider (beatFields, P::kMaxNotes, "Max notes/step");
    tip (addCombo (beatFields, P::kMicrotones, "MIDI microtones"), "Quarter-tone scales: the built-in sound is always exact; MIDI uses pitch bend or rounds");
    addSlider (beatFields, P::kBendRange, "Bend range");

    // Harmony
    addCombo (harmonyFields, P::kRoot, "Root");
    addCombo (harmonyFields, P::kScale, "Scale");
    addSlider (harmonyFields, P::kOctave, "Octave");
    addCombo (harmonyFields, P::kChordDegree, "Chord degree");
    addSlider (harmonyFields, P::kChordSize, "Chord size");
    addCombo (harmonyFields, P::kChordStack, "Stacking");
    addSlider (harmonyFields, P::kChordSpread, "Voicing spread");
    addCombo (harmonyFields, P::kProgression, "Progression");
    addCombo (harmonyFields, P::kChangeMode, "Change chord");
    addSlider (harmonyFields, P::kChangeBars, "Bars per chord");
    tip (addCombo (harmonyFields, P::kMelody, "Device melody"),
         "Note rows: each device walks its own short phrase, or always plays its one home note");

    // Traffic → Sound
    for (int k = 0; k < kNumKinds; ++k)
    {
        auto row = std::make_unique<Row>();
        auto& r = *row;
        r.kind = k;
        juce::String ports;
        for (auto port : kKinds[k].ports)
            if (port != 0)
                ports << (ports.isEmpty() ? "udp " : ", ") << (int) port;
        r.name.setText (kKinds[k].label, juce::dontSendNotification);
        r.name.setFont (juce::FontOptions (14.0f));
        r.name.setTooltip (ports + (kKinds[k].multicastGroup ? juce::String (" (multicast ") + kKinds[k].multicastGroup + ")" : " (broadcast)"));
        r.counts.setColour (juce::Label::textColourId, kMuted);
        r.counts.setFont (juce::FontOptions (11.5f));
        r.meter.value = [this, k] { return processor.engine.getActivity (k); };

        buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.state, P::rowId (k, "on"), r.on));
        attachCombo (r.role, P::rowId (k, "role"));
        attachCombo (r.sound, P::rowId (k, "sound"));
        attachCombo (r.rhythm, P::rowId (k, "rhythm"));
        using SliderId = std::pair<juce::Slider*, const char*>;
        for (auto [slider, suffix] : std::initializer_list<SliderId> { { &r.channel, "ch" }, { &r.octave, "oct" },
                                                                     { &r.length, "len" }, { &r.velocity, "vel" },
                                                                     { &r.maxPerBar, "max" }, { &r.chance, "chance" },
                                                                     { &r.hitNote, "note" } })
        {
            slider->setSliderStyle (juce::Slider::LinearBar);
            slider->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 20);
            attachSlider (*slider, P::rowId (k, suffix));
        }
        r.length.setTooltip ("Note length in 16th notes");
        r.maxPerBar.setTooltip ("At most this many triggers per bar, however busy the traffic");
        r.hitNote.setTooltip ("Fixed MIDI note for the Hit role (e.g. 42 = closed hi-hat on drum machines)");
        r.test.setTooltip ("Play this row now");
        r.test.onClick = [this, k] { processor.requestTest (k); };

        for (juce::Component* c : std::initializer_list<juce::Component*> { &r.name, &r.counts, &r.meter, &r.on, &r.role, &r.sound, &r.channel, &r.octave,
                                                                            &r.rhythm, &r.length, &r.velocity, &r.maxPerBar, &r.chance, &r.hitNote, &r.test })
            addAndMakeVisible (c);
        rows.push_back (std::move (row));
    }

    feed.setMultiLine (true, false); // one packet per line, no wrapping
    feed.setTabKeyUsedAsCharacter (true);
    feed.setReadOnly (true);
    feed.setScrollbarsShown (true);
    feed.setCaretVisible (false);
    feed.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::plain));
    addAndMakeVisible (feed);
    portSummary.setColour (juce::Label::textColourId, kMuted);
    portSummary.setFont (juce::FontOptions (12.5f));
    addAndMakeVisible (portSummary);

    // Controls were built before they had a parent; re-apply the theme so their text boxes pick it up.
    sendLookAndFeelChange();

    setResizable (true, true);
    setResizeLimits (1000, 700, 2000, 1400);
    setSize (1180, 760);
    startTimerHz (15);
    timerCallback();
}

ListenEditor::~ListenEditor() { setLookAndFeel (nullptr); }

// ---------------------------------------------------------------- layout & painting

void ListenEditor::layoutGroup (std::vector<Field>& group, juce::Rectangle<int> area)
{
    const int h = juce::jmin (30, area.getHeight() / juce::jmax (1, (int) group.size()));
    for (auto& f : group)
    {
        auto line = area.removeFromTop (h).reduced (0, 3);
        if (f.label != nullptr)
            f.label->setBounds (line.removeFromLeft (118));
        f.control->setBounds (line);
    }
}

void ListenEditor::resized()
{
    auto area = getLocalBounds().reduced (kMargin);
    auto header = area.removeFromTop (kHeaderH - kMargin);
    panic.setBounds (header.removeFromRight (84).reduced (0, 4));
    header.removeFromRight (8);
    testAll.setBounds (header.removeFromRight (110).reduced (0, 4));
    header.removeFromRight (8);
    presetMenu.setBounds (header.removeFromRight (170).reduced (0, 5));
    title.setBounds (header.removeFromLeft (222));
    version.setBounds (header.removeFromLeft (44).withTrimmedTop (4));
    status.setBounds (header);
    area.removeFromTop (8);

    const int tableH = 14 + 22 + 22 + kNumKinds * kRowH + 14;
    tableArea = area.removeFromBottom (tableH);
    area.removeFromBottom (kMargin);

    beatArea = area.removeFromLeft (360);
    area.removeFromLeft (kMargin);
    harmonyArea = area.removeFromLeft (380);
    area.removeFromLeft (kMargin);
    feedArea = area;

    layoutGroup (beatFields, beatArea.reduced (14).withTrimmedTop (26));
    layoutGroup (harmonyFields, harmonyArea.reduced (14).withTrimmedTop (26));
    auto feedInner = feedArea.reduced (14).withTrimmedTop (26);
    portSummary.setBounds (feedInner.removeFromBottom (36));
    feed.setBounds (feedInner);

    // Table: scale columns to the available width.
    auto inner = tableArea.reduced (14).withTrimmedTop (22);
    int total = 0;
    for (auto& c : kColumns)
        total += c.second + kColumnGap;
    const double scale = (double) inner.getWidth() / total;
    columnHeaders.clear();
    std::vector<juce::Rectangle<int>> cols;
    int x = inner.getX();
    for (auto& c : kColumns)
    {
        const int w = (int) (c.second * scale);
        cols.emplace_back (x, 0, w, 0);
        columnHeaders.push_back ({ c.first, { x, inner.getY(), w, 20 } });
        x += w + (int) (kColumnGap * scale);
    }
    int y = inner.getY() + 22;
    for (auto& r : rows)
    {
        auto cell = [&] (size_t i) { return juce::Rectangle<int> (cols[i].getX(), y, cols[i].getWidth(), kRowH); };
        auto nameCell = cell (0);
        r->name.setBounds (nameCell.removeFromTop (19));
        r->counts.setBounds (nameCell);
        r->meter.setBounds (cell (1).reduced (0, 4));
        r->on.setBounds (cell (2).reduced (2, 4));
        r->role.setBounds (cell (3).reduced (0, 4));
        r->sound.setBounds (cell (4).reduced (0, 4));
        r->channel.setBounds (cell (5));
        r->octave.setBounds (cell (6));
        r->rhythm.setBounds (cell (7).reduced (0, 4));
        r->length.setBounds (cell (8));
        r->velocity.setBounds (cell (9));
        r->maxPerBar.setBounds (cell (10));
        r->chance.setBounds (cell (11));
        r->hitNote.setBounds (cell (12));
        r->test.setBounds (cell (13).reduced (0, 5));
        y += kRowH;
    }
}

void ListenEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kLine);
    g.drawHorizontalLine (kHeaderH, 0.0f, (float) getWidth());

    auto panel = [&g] (juce::Rectangle<int> r, const juce::String& heading) {
        g.setColour (kPanel);
        g.fillRoundedRectangle (r.toFloat(), 9.0f);
        g.setColour (kLine);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 9.0f, 1.0f);
        g.setColour (kMuted);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (heading.toUpperCase(), r.reduced (14, 10).removeFromTop (16), juce::Justification::topLeft);
    };
    panel (beatArea, "Sound & Beat");
    panel (harmonyArea, "Harmony");
    panel (feedArea, "Live traffic");
    panel (tableArea, juce::String::fromUTF8 ("Traffic \xe2\x86\x92 Sound"));

    g.setFont (juce::FontOptions (12.0f));
    g.setColour (kMuted);
    for (auto& [text, r] : columnHeaders)
        if (text.isNotEmpty() && text != "Traffic")
            g.drawText (text, r, juce::Justification::centredLeft);
}

// ---------------------------------------------------------------- live status

void ListenEditor::timerCallback()
{
    auto& e = processor.engine;
    const auto* listener = processor.getListener();
    const int devices = listener ? listener->getDeviceCount() : 0;
    status.setText (juce::String ("Bar ") + juce::String (e.getBar()) + "." + juce::String (e.getBeat())
                        + juce::String::fromUTF8 ("   \xc2\xb7   chord ") + kNoteNames[e.getChordRootPitchClass()]
                        + juce::String::fromUTF8 ("   \xc2\xb7   ") + juce::String (processor.getCurrentTempo(), 1) + " bpm"
                        + (processor.isFollowingHost() ? " (host)" : " (free-run)")
                        + juce::String::fromUTF8 ("   \xc2\xb7   ") + juce::String (devices) + " devices"
                        + juce::String::fromUTF8 ("   \xc2\xb7   ") + juce::String (e.getActiveNoteCount()) + " notes sounding",
                    juce::dontSendNotification);

    for (auto& r : rows)
    {
        r->counts.setText (juce::String (e.getPackets (r->kind)) + " packets " + juce::String::fromUTF8 ("\xc2\xb7 ")
                               + juce::String (e.getNotes (r->kind)) + " notes",
                           juce::dontSendNotification);
        r->meter.repaint();
        // "Hit note" only matters for the Hit role.
        const bool isHit = r->role.getSelectedItemIndex() == kRoleHit;
        r->hitNote.setEnabled (isHit);
        r->hitNote.setAlpha (isHit ? 1.0f : 0.35f);
    }
    if (presetMenu.getSelectedItemIndex() != processor.getCurrentProgram() && ! presetMenu.isPopupActive())
        presetMenu.setSelectedItemIndex (processor.getCurrentProgram(), juce::dontSendNotification);

    if (listener == nullptr)
    {
        portSummary.setText ("Network starts when the host starts audio.", juce::dontSendNotification);
        return;
    }
    int ok = 0, total = 0;
    juce::StringArray failed;
    for (const auto& st : listener->getStatus())
    {
        ++total;
        if (st.ok)
            ++ok;
        else
            failed.add (juce::String (st.name) + " (" + st.detail + ")");
    }
    if (listener->isSimulated())
    {
        portSummary.setText ("Simulated network (demo) - no real traffic is being read", juce::dontSendNotification);
    }
    else
    {
        portSummary.setText ("Listening on " + juce::String (ok) + " of " + juce::String (total) + " ports"
                                 + (failed.isEmpty() ? juce::String (" - no admin rights needed") : ". Unavailable: " + failed.joinIntoString (", ")),
                             juce::dontSendNotification);
    }
    portSummary.setColour (juce::Label::textColourId, ok > 0 ? kMuted : kWarn);

    const auto lines = listener->getRecentLines();
    const juce::String tail = lines.empty() ? juce::String() : juce::String (lines.back()) + juce::String ((int) lines.size());
    if (tail != lastFeedTail)
    {
        lastFeedTail = tail;
        juce::String text;
        for (auto it = lines.rbegin(); it != lines.rend() && it - lines.rbegin() < 60; ++it)
            text << *it << "\n";
        feed.setText (text, false);
    }
    else if (lines.empty() && feed.isEmpty())
    {
        feed.setText ("Waiting for network chatter...\nQuiet networks can take a minute.\n"
                      "If nothing appears, allow VSTHost through Windows Firewall (private networks).",
                      false);
    }
}
