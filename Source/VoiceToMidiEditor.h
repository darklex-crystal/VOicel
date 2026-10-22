#pragma once

#include <JuceHeader.h>

#include "VoiceEngine.h"
#include "MicCapture.h"

namespace vm
{

//==============================================================================
/** Panneau de contrôles : titre + grille de boutons rotatifs / listes. */
class GroupPanel : public juce::Component
{
public:
    explicit GroupPanel (juce::String title);
    ~GroupPanel() override;

    void addControl (juce::Component& control, const juce::String& caption);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title_;
    std::vector<juce::Component*> controls_;
    std::vector<juce::String> captions_;
    std::vector<juce::Rectangle<int>> captionAreas_;
};

//==============================================================================
/** Affichage défilant de la hauteur détectée, des notes jouées et du niveau. */
class PitchDisplay : public juce::Component,
                        private juce::Timer
{
public:
    explicit PitchDisplay (VoicelProcessorBase& processor);
    ~PitchDisplay() override;

    void paint (juce::Graphics&) override;
    void timerCallback() override;

private:
    void paintGrid (juce::Graphics&, const juce::Rectangle<float>& area, int lo, int hi);
    void paintNotes (juce::Graphics&, const juce::Rectangle<float>& area,
                     const std::vector<DisplayFrame>& frames, int lo, int hi);
    void paintTrace (juce::Graphics&, const juce::Rectangle<float>& area,
                     const std::vector<DisplayFrame>& frames, int lo, int hi);

    VoicelProcessorBase& proc_;
    std::vector<DisplayFrame> snapshot_;
    double lastTimer_ = 0.0;
};

//==============================================================================
class VoiceToMidiEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit VoiceToMidiEditor (VoicelProcessorBase& processor);
    ~VoiceToMidiEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    class Look;

    void  buildInterface();
    juce::Slider* addKnob  (const char* paramId, const juce::String& caption, GroupPanel& panel);
    juce::ComboBox* addCombo (const char* paramId, GroupPanel& panel, const juce::String& caption);

    VoicelProcessorBase& proc_;
    juce::AudioProcessorValueTreeState& state_;

    std::unique_ptr<Look>  look_;
    std::unique_ptr<PitchDisplay> display_;

    // Propriété : les composants sont détenus ici (avant, ils étaient créés
    // avec `new` sans jamais être libérés). Ordre de déclaration = inverse de
    // l'ordre de destruction : panneaux, puis contrôles, puis attachements
    // (détruits en premier, avant les contrôles qu'ils référencent).
    std::unique_ptr<GroupPanel> panelVoix_, panelNote_, panelAccord_, panelSortie_;
    juce::OwnedArray<juce::Component> controls_;

    juce::ComboBox* latencyCombo_  = nullptr;
    juce::ComboBox* bendCombo_     = nullptr;
    juce::ComboBox* scaleCombo_    = nullptr;
    juce::ComboBox* rootCombo_     = nullptr;
    juce::ComboBox* velocityCombo_ = nullptr;
    juce::ComboBox* expressionCombo_ = nullptr;
    juce::ComboBox* channelCombo_  = nullptr;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>  sliderAttachments_;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments_;

    std::unique_ptr<MicPanel> micPanel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceToMidiEditor)
};

} // namespace vm
