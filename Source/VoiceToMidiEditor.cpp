#include "VoiceToMidiEditor.h"

#include <cmath>

#if JUCE_STANDALONE_APPLICATION
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace vm
{

namespace
{
    const juce::Colour kBackground   = juce::Colour (0xff0d1014);
    const juce::Colour kPanel        = juce::Colour (0xff151a21);
    const juce::Colour kPanelEdge    = juce::Colour (0xff242c36);
    const juce::Colour kText         = juce::Colour (0xffd8dee7);
    const juce::Colour kTextDim      = juce::Colour (0xff7b8794);
    const juce::Colour kAccent       = juce::Colour (0xff35d6c0);
    const juce::Colour kAccentWarm   = juce::Colour (0xffffc857);
    const juce::Colour kDanger       = juce::Colour (0xffff6b6b);

    constexpr float kPi = 3.14159265358979323846f;

    /** Première famille sans empattement disponible, pour un rendu texte
        homogène (accentué compris) quelle que soit la machine. */
    const juce::String& preferredFamily()
    {
        static juce::String family = [] {
            static const char* candidates[] = { "Inter", "Noto Sans", "DejaVu Sans",
                                                "Liberation Sans", "Segoe UI",
                                                "Helvetica Neue", "Arial", "Helvetica" };
            const auto available = juce::Font::findAllTypefaceNames();
            for (auto* name : candidates)
                if (available.contains (name))
                    return juce::String (name);

            return juce::String();
        }();

        return family;
    }

    juce::Font appFont (float height, int styleFlags = juce::Font::plain)
    {
        juce::Font font (height, styleFlags);

        if (preferredFamily().isNotEmpty())
            font.setTypefaceName (preferredFamily());

        return font;
    }

    juce::String noteName (int midiNote)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                       "F#", "G", "G#", "A", "A#", "B" };
        if (midiNote < 0)
            return "--";

        const int index = ((midiNote % 12) + 12) % 12;
        return juce::String (names[index]) + juce::String (midiNote / 12 - 1);
    }
}

//==============================================================================
//  Apparence des contrôles
//==============================================================================
class VoiceToMidiEditor::Look : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        setColour (juce::ResizableWindow::backgroundColourId, kBackground);
        setColour (juce::Label::textColourId, kText);
        setColour (juce::TextButton::buttonColourId, kPanel);
        setColour (juce::TextButton::buttonOnColourId, kAccent.withAlpha (0.25f));
        setColour (juce::TextButton::textColourOffId, kText);
        setColour (juce::TextButton::textColourOnId, kAccent);

        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1c222b));
        setColour (juce::ComboBox::textColourId, kText);
        setColour (juce::ComboBox::outlineColourId, kPanelEdge);
        setColour (juce::ComboBox::arrowColourId, kTextDim);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1c222b));
        setColour (juce::PopupMenu::textColourId, kText);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha (0.3f));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float> (static_cast<float> (x),
                                                    static_cast<float> (y),
                                                    static_cast<float> (width),
                                                    static_cast<float> (height));
        const float side  = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const auto centre = bounds.getCentre();
        const float radius = side * 0.40f;
        constexpr float thickness = 4.0f;

        juce::Path background;
        background.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                                  rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (juce::Colour (0xff2a323d));
        g.strokePath (background, juce::PathStrokeType (thickness,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

        if (slider.isEnabled())
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                                 rotaryStartAngle, angle, true);
            g.setColour (kAccent);
            g.strokePath (value, juce::PathStrokeType (thickness,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        juce::Path pointer;
        const float inner = radius * 0.42f;
        pointer.startNewSubPath (centre.x + std::cos (angle) * inner,
                                 centre.y + std::sin (angle) * inner);
        pointer.lineTo (centre.x + std::cos (angle) * (radius + thickness * 0.9f),
                        centre.y + std::sin (angle) * (radius + thickness * 0.9f));
        g.setColour (slider.isEnabled() ? kText : kTextDim.withAlpha (0.4f));
        g.strokePath (pointer, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<float> (0.0f, 0.0f,
                                                    static_cast<float> (width),
                                                    static_cast<float> (height));
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId)
                         .withMultipliedSaturation (isButtonDown ? 1.2f : 1.0f));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

        juce::Path arrow;
        const float cx = static_cast<float> (width) - 12.0f;
        const float cy = static_cast<float> (height) * 0.5f;
        arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.fillPath (arrow);

    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return appFont (13.0f);
    }

    juce::Font getPopupMenuFont() override
    {
        return appFont (13.0f);
    }
};

//==============================================================================
//  GroupPanel
//==============================================================================
GroupPanel::GroupPanel (juce::String title) : title_ (std::move (title))
{
    setInterceptsMouseClicks (false, true);
}

GroupPanel::~GroupPanel() = default;

void GroupPanel::addControl (juce::Component& control, const juce::String& caption)
{
    controls_.push_back (&control);
    captions_.push_back (caption);
    addAndMakeVisible (control);
}

void GroupPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (kPanel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (kPanelEdge);
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

    g.setColour (kTextDim);
    g.setFont (appFont (10.5f, juce::Font::bold));
    g.drawText (title_.toUpperCase(), getLocalBounds().reduced (12, 0).withHeight (20),
                juce::Justification::centredLeft, false);

    g.setColour (kTextDim.withAlpha (0.75f));
    g.setFont (appFont (10.0f));
    for (size_t i = 0; i < captions_.size() && i < captionAreas_.size(); ++i)
    {
        const auto area = captionAreas_[i];
        g.drawText (captions_[i], area, juce::Justification::centredTop, false);
    }
}

void GroupPanel::resized()
{
    const int n = static_cast<int> (controls_.size());
    captionAreas_.assign (static_cast<size_t> (n), {});

    if (n == 0)
        return;

    const auto available = getLocalBounds().reduced (10, 6).withTrimmedTop (20);

    const int minCellWidth  = 62;
    const int maxCellHeight = 92;
    const int captionHeight = 15;

    const int maxCols = juce::jmax (1, available.getWidth() / minCellWidth);
    const int cols    = juce::jmin (n, maxCols);
    const int rows    = (n + cols - 1) / cols;

    const int cellW = available.getWidth() / cols;
    const int cellH = juce::jmin (available.getHeight() / rows, maxCellHeight);
    const int top   = available.getY() + juce::jmax (0, (available.getHeight() - cellH * rows) / 2);

    for (int i = 0; i < n; ++i)
    {
        const int col = i % cols;
        const int row = i / cols;

        auto cell = juce::Rectangle<int> (available.getX() + col * cellW,
                                          top + row * cellH,
                                          cellW, cellH);

        auto control = cell.removeFromTop (cellH - captionHeight);
        captionAreas_[static_cast<size_t> (i)] = cell;

        if (auto* combo = dynamic_cast<juce::ComboBox*> (controls_[static_cast<size_t> (i)]))
        {
            const int w = juce::jmin (control.getWidth() - 6, 132);
            combo->setBounds (control.withSizeKeepingCentre (w, 24));
        }
        else
        {
            const int side = juce::jmin (juce::jmin (control.getWidth() - 4, control.getHeight()), 64);
            controls_[static_cast<size_t> (i)]->setBounds (control.withSizeKeepingCentre (side, side));
        }
    }
}

//==============================================================================
//  PitchDisplay
//==============================================================================
PitchDisplay::PitchDisplay (VoicelProcessorBase& p) : proc_ (p)
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

PitchDisplay::~PitchDisplay()
{
    stopTimer();
}

void PitchDisplay::timerCallback()
{
    proc_.getDisplay().read (snapshot_);
    repaint();
}

void PitchDisplay::paintGrid (juce::Graphics& g, const juce::Rectangle<float>& area,
                              int lo, int hi)
{
    const float range = static_cast<float> (juce::jmax (12, hi - lo));
    const float h = area.getHeight();

    g.setFont (appFont (9.0f));

    for (int note = lo; note <= hi; ++note)
    {
        const auto y = area.getBottom() - (note - lo) / range * h;
        const bool isC = ((note % 12) + 12) % 12 == 0;
        const bool isBlack = ((note % 12) + 12) % 12 == 1 || ((note % 12) + 12) % 12 == 3
                          || ((note % 12) + 12) % 12 == 6 || ((note % 12) + 12) % 12 == 8
                          || ((note % 12) + 12) % 12 == 10;

        g.setColour (isC ? juce::Colour (0xff2c3540) : juce::Colour (0xff1a2029));

        if (isBlack && ! isC)
            g.setColour (juce::Colour (0xff171d25));

        g.drawHorizontalLine (static_cast<int> (y), area.getX(), area.getRight());

        if (isC)
        {
            g.setColour (kTextDim.withAlpha (0.8f));
            g.drawText (noteName (note), area.getX() + 4.0f, y - 7.0f, 34, 14,
                        juce::Justification::centredLeft, false);
        }
    }
}

void PitchDisplay::paintNotes (juce::Graphics& g, const juce::Rectangle<float>& area,
                               const std::vector<DisplayFrame>& frames, int lo, int hi)
{
    if (frames.size() < 2)
        return;

    const float range = static_cast<float> (juce::jmax (12, hi - lo));
    const float h = area.getHeight();
    const float dx = area.getWidth() / static_cast<float> (frames.size() - 1);

    auto noteToY = [&] (int note)
    {
        return area.getBottom() - (note - lo) / range * h;
    };

    int i = 0;
    while (i < static_cast<int> (frames.size()))
    {
        const int note = frames[static_cast<size_t> (i)].note;
        int j = i;

        if (note >= 0)
        {
            while (j < static_cast<int> (frames.size()) && frames[static_cast<size_t> (j)].note == note)
                ++j;
        }
        else
        {
            ++j;
            continue;
        }

        const float x0 = area.getX() + i * dx;
        const float x1 = area.getX() + juce::jmin (j, static_cast<int> (frames.size()) - 1) * dx + dx;
        const float y  = noteToY (note);

        const float level = frames[static_cast<size_t> (i)].level;
        const juce::Colour colour = kAccent.withAlpha (0.18f + 0.55f * level);

        g.setColour (colour);
        g.fillRoundedRectangle (juce::Rectangle<float> (x0, y - 2.5f,
                                                         juce::jmax (dx * 1.2f, x1 - x0), 5.0f),
                                2.5f);
        i = j;
    }
}

void PitchDisplay::paintTrace (juce::Graphics& g, const juce::Rectangle<float>& area,
                               const std::vector<DisplayFrame>& frames, int lo, int hi)
{
    if (frames.size() < 2)
        return;

    const float range = static_cast<float> (juce::jmax (12, hi - lo));
    const float dx = area.getWidth() / static_cast<float> (frames.size() - 1);

    // Ligne de temps
    juce::Path line;
    bool started = false;

    float lastX = 0.0f, lastY = 0.0f;

    for (size_t i = 0; i < frames.size(); ++i)
    {
        const auto& f = frames[i];
        if (f.midi <= 0.0f)
            continue;

        const float x = area.getX() + static_cast<float> (i) * dx;
        const float y = area.getBottom()
                      - juce::jlimit (0.0f, range, f.midi - lo) / range * area.getHeight();

        if (! started)
        {
            line.startNewSubPath (x, y);
            started = true;
        }
        else
        {
            line.lineTo (x, y);
        }

        lastX = x;
        lastY = y;
    }

    if (started)
    {
        g.setColour (kAccentWarm.withAlpha (0.25f));
        g.strokePath (line, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        g.setColour (kAccentWarm);
        g.strokePath (line, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        g.setColour (kAccentWarm);
        g.fillEllipse (lastX - 3.5f, lastY - 3.5f, 7.0f, 7.0f);
    }

    // Tête de lecture
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawVerticalLine (static_cast<int> (area.getRight() - dx), area.getY(), area.getBottom());
}

void PitchDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll (kBackground);

    const auto current = proc_.getDisplay().getCurrent();

    const float rawLo = *proc_.getState().getRawParameterValue (pid::noteMin);
    const float rawHi = *proc_.getState().getRawParameterValue (pid::noteMax);
    const int noteLo = juce::jlimit (0, 127, static_cast<int> (std::lround (static_cast<double> (rawLo))));
    const int noteHi = juce::jlimit (0, 127, static_cast<int> (std::lround (static_cast<double> (rawHi))));

    const int gridLo = juce::jmax (0, juce::jmin (noteLo, noteHi) - 1);
    const int gridHi = juce::jmin (127, juce::jmax (noteLo, noteHi) + 1);

    juce::Path clip;
    clip.addRoundedRectangle (bounds, 8.0f);
    g.saveState();
    g.reduceClipRegion (clip);

    const auto area = bounds.reduced (8.0f, 8.0f);
    paintGrid (g, area, gridLo, gridHi);
    paintNotes (g, area, snapshot_, gridLo, gridHi);
    paintTrace (g, area, snapshot_, gridLo, gridHi);

    // Jauge de niveau
    const float meterY = bounds.getBottom() - 4.0f;
    const float meterW = bounds.getWidth() - 16.0f;
    g.setColour (juce::Colour (0xff1a2029));
    g.fillRoundedRectangle (juce::Rectangle<float> (8.0f, meterY, meterW, 3.0f), 1.5f);

    const juce::Colour meterColour = current.level > 0.92f ? kDanger
                                      : current.level > 0.7f ? kAccentWarm : kAccent;
    g.setColour (meterColour);
    g.fillRoundedRectangle (juce::Rectangle<float> (8.0f, meterY,
                                                    juce::jmax (3.0f, meterW * current.level), 3.0f),
                            1.5f);

    g.restoreState();

    g.setColour (kPanelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);
}

//==============================================================================
//  VoiceToMidiEditor
//==============================================================================
VoiceToMidiEditor::VoiceToMidiEditor (VoicelProcessorBase& p)
    : juce::AudioProcessorEditor (&p),
      proc_ (p),
      state_ (p.getState())
{
    look_ = std::make_unique<Look>();
    setLookAndFeel (look_.get());

    buildInterface();

    setResizable (true, true);
    setResizeLimits (960, 640, 1800, 1200);
    setSize (1220, 780);

    startTimerHz (30);
}

VoiceToMidiEditor::~VoiceToMidiEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

juce::Slider* VoiceToMidiEditor::addKnob (const char* paramId, const juce::String& caption,
                                          GroupPanel& panel)
{
    auto* slider = new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag,
                                     juce::Slider::NoTextBox);
    controls_.add (slider);
    slider->setDoubleClickReturnValue (true, 0.0);
    slider->setName (caption);
    panel.addControl (*slider, caption);

    if (state_.getParameter (paramId) != nullptr)
        sliderAttachments_.push_back (
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                state_, paramId, *slider));

    return slider;
}

juce::ComboBox* VoiceToMidiEditor::addCombo (const char* paramId, GroupPanel& panel,
                                             const juce::String& caption)
{
    auto* combo = new juce::ComboBox (paramId);
    controls_.add (combo);
    panel.addControl (*combo, caption);

    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state_.getParameter (paramId)))
    {
        combo->addItemList (p->choices, 1);
        combo->setSelectedItemIndex (p->getIndex(), juce::dontSendNotification);
        comboAttachments_.push_back (
            std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                state_, paramId, *combo));
    }

    return combo;
}

void VoiceToMidiEditor::buildInterface()
{
    panelVoix_   = std::make_unique<GroupPanel> ("Voix");
    panelNote_   = std::make_unique<GroupPanel> ("Note");
    panelAccord_ = std::make_unique<GroupPanel> ("Accord");
    panelSortie_ = std::make_unique<GroupPanel> ("Sortie");

    addAndMakeVisible (*panelVoix_);
    addAndMakeVisible (*panelNote_);
    addAndMakeVisible (*panelAccord_);
    addAndMakeVisible (*panelSortie_);

    display_ = std::make_unique<PitchDisplay> (proc_);
    addAndMakeVisible (*display_);

    // --- Voix
    addKnob (pid::inputGain,   "Gain",         *panelVoix_);
    addKnob (pid::gateDb,      "Seuil",        *panelVoix_);
    addKnob (pid::sensitivity, "Sensibilite",  *panelVoix_);
    addKnob (pid::fMin,        "Basse (Hz)",   *panelVoix_);
    addKnob (pid::fMax,        "Haute (Hz)",   *panelVoix_);
    addKnob (pid::holdMs,      "Maintien",     *panelVoix_);
    latencyCombo_ = addCombo (pid::latencyMode, *panelVoix_, "Latence");

    // --- Note
    addKnob (pid::noteMin,     "Note min",     *panelNote_);
    addKnob (pid::noteMax,     "Note max",     *panelNote_);
    addKnob (pid::glideMs,     "Glissando",    *panelNote_);
    addKnob (pid::octaveShift, "Octave",       *panelNote_);
    bendCombo_ = addCombo (pid::bendRange, *panelNote_, "Plage de bend");
    addKnob (pid::tuneLock,    "Verrouillage", *panelNote_);

    // --- Accord
    addKnob (pid::tuneAmount,   "Correction",   *panelAccord_);
    addKnob (pid::retuneSpeed,  "Vitesse",      *panelAccord_);
    scaleCombo_    = addCombo (pid::scale,         *panelAccord_, "Gamme");
    rootCombo_     = addCombo (pid::root,          *panelAccord_, "Tonalite");
    velocityCombo_ = addCombo (pid::velocityMode, *panelAccord_, "Velocite");
    addKnob (pid::velocityMax, "Velocite max",    *panelAccord_);

    // --- Sortie
    if (proc_.isSynthPlugin())
    {
        addKnob (pid::synthGain,   "Synthese",   *panelSortie_);
        addKnob (pid::monitorGain, "Echo voix",  *panelSortie_);
    }
    else
    {
        addKnob (pid::monitorGain, "Echo voix",  *panelSortie_);
        addKnob (pid::synthGain,   "Synthese",   *panelSortie_);
    }

    channelCombo_    = addCombo (pid::midiChannel, *panelSortie_, "Canal MIDI");
    expressionCombo_ = addCombo (pid::exprCC,      *panelSortie_, "Expression");

    // --- Selection du micro (version instrument uniquement)
    if (auto* mic = proc_.getMicCapture())
    {
        micPanel_ = std::make_unique<MicPanel> (*mic, proc_.getEngine());
        addAndMakeVisible (*micPanel_);
    }
}

void VoiceToMidiEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    auto bounds = getLocalBounds().toFloat().reduced (14.0f);
    const float headerHeight = 40.0f;

    auto header = bounds.removeFromTop (headerHeight);

    // --- Titre
    g.setFont (appFont (21.0f, juce::Font::bold));
    g.setColour (kText);
    g.drawText ("Voicel", header.removeFromLeft (150.0f), juce::Justification::centredLeft, false);

    g.setFont (appFont (11.5f));
    g.setColour (kTextDim);
    g.drawText (proc_.isSynthPlugin() ? "micro > voix > MIDI" : "voix > MIDI en temps reel",
                header.removeFromLeft (190.0f), juce::Justification::centredLeft, false);

    // --- Note courante
    const auto current = proc_.getDisplay().getCurrent();
    auto right = header;

    g.setFont (appFont (26.0f, juce::Font::bold));
    g.setColour (current.note >= 0 ? kAccent : kTextDim.withAlpha (0.5f));
    g.drawText (noteName (current.note), right.removeFromRight (86.0f),
                juce::Justification::centredRight, false);

    // --- Indicateur
    right.removeFromRight (22.0f);
    auto indicator = right.removeFromRight (140.0f);
    const float dotY = indicator.getCentreY();
    g.setColour (current.voiced ? kAccent : juce::Colour (0xff39424e));
    g.fillEllipse (indicator.getX(), dotY - 4.0f, 8.0f, 8.0f);

    g.setFont (appFont (11.5f));
    g.setColour (current.voiced ? kText : kTextDim);
    g.drawText (current.voiced ? "voix detectee" : "en attente", indicator.removeFromLeft (130.0f),
                juce::Justification::centredLeft, false);

    // --- Latence
    right.removeFromRight (30.0f);
    auto info = right.removeFromRight (150.0f);
    g.setColour (kTextDim);
    g.setFont (appFont (11.5f));
    const double latencyMs = 1000.0 * proc_.getLatencySamples()
                           / juce::jmax (1.0, proc_.getSampleRate());
    g.drawText (juce::String (latencyMs, 0) + " ms de latence", info,
                juce::Justification::centredRight, false);

    g.setColour (kPanelEdge);
    g.drawHorizontalLine (static_cast<int> (header.getBottom()), bounds.getX(), bounds.getRight());

   #if JUCE_STANDALONE_APPLICATION
    if (proc_.isStandaloneMode())
    {
        g.setFont (appFont (11.0f));
        g.setColour (kTextDim);
        g.drawText ("Application autonome : choix du micro et de la sortie MIDI via le bouton "
                    "\"Settings...\" (en bas a droite).",
                    bounds.removeFromBottom (16.0f), juce::Justification::centredLeft, true);
    }
   #endif
}

void VoiceToMidiEditor::resized()
{
    auto bounds = getLocalBounds().reduced (14.0f);

    bounds.removeFromTop (40.0f + 10.0f);   // en-tête

    if (micPanel_ != nullptr)
        micPanel_->setBounds (bounds.removeFromTop (54.0f).toNearestInt());

   #if JUCE_STANDALONE_APPLICATION
    if (proc_.isStandaloneMode())
        bounds.removeFromBottom (18);
   #endif

    const int gap = 10;
    const int rowHeight = juce::jlimit (96, 186, static_cast<int> (bounds.getHeight() * 0.27f));

    auto row2 = bounds.removeFromBottom (rowHeight);
    auto row1 = bounds.removeFromBottom (rowHeight + gap);

    display_->setBounds (bounds.reduced (0, gap));

    auto half = [](juce::Rectangle<int>& r)
    {
        auto left = r.removeFromLeft (r.getWidth() / 2 - 5);
        r.removeFromLeft (10);
        return std::make_pair (left, r);
    };

    const auto top = half (row1);
    const auto bottom = half (row2);

    panelVoix_->setBounds (top.first);
    panelNote_->setBounds (top.second);
    panelAccord_->setBounds (bottom.first);
    panelSortie_->setBounds (bottom.second);
}

void VoiceToMidiEditor::timerCallback()
{
    repaint();
}

} // namespace vm
