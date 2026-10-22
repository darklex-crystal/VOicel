#pragma once

#include "VoiceEngine.h"

namespace vm
{

//==============================================================================
/** Effet MIDI : reçoit l'audio du micro sur son bus d'entrée et produit du
    MIDI. Fonctionne dans Cubase, Logic, Reaper, Studio One, Bitwig, FL…

    (Ableton Live ne prend pas en charge cette catégorie de plug-in : utiliser
    la version instrument "Voicel Voice".)
*/
class VoiceToMidiProcessor : public VoicelProcessorBase
{
public:
    VoiceToMidiProcessor();

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void reset() override;

    bool isBusesLayoutSupported (const juce::AudioProcessor::BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override
    {
       #if defined(JucePlugin_Name)
        return JucePlugin_Name;
       #else
        return "Voicel MIDI";
       #endif
    }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }

    using juce::AudioProcessor::processBlock;

    void getStateInformation (juce::MemoryBlock& destination) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- VoicelProcessorBase -----------------------------------------------
    juce::AudioProcessorValueTreeState& getState() override { return state_; }
    DisplayState& getDisplay() override { return engine_.getDisplay(); }
    VoiceEngine&  getEngine() override { return engine_; }
    bool isSynthPlugin() const override { return false; }

private:
    VoiceEngine engine_;
    juce::AudioBuffer<float> scratch_;
    juce::AudioBuffer<float> output_;
    int currentLatency_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceToMidiProcessor)
};

} // namespace vm
