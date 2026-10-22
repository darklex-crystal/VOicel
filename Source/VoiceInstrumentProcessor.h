#pragma once

#include "VoiceEngine.h"
#include "MicCapture.h"

namespace vm
{

//==============================================================================
/** Version INSTRUMENT du convertisseur voix > MIDI.

    Aucun bus d'entrée audio : c'est le plug-in qui ouvre lui-même le
    microphone, l'analyse, et joue la note détectée avec le synthétiseur
    interne (on peut couper l'écho de la synthèse pour n'utiliser que le
    MIDI). C'est la version à charger dans Ableton Live.
*/
class VoiceInstrumentProcessor : public VoicelProcessorBase
{
public:
    VoiceInstrumentProcessor();

    ~VoiceInstrumentProcessor() override;

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
        return "Voicel Voice";
       #endif
    }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    using juce::AudioProcessor::processBlock;

    void getStateInformation (juce::MemoryBlock& destination) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- VoicelProcessorBase -----------------------------------------------
    juce::AudioProcessorValueTreeState& getState() override { return state_; }
    DisplayState& getDisplay() override { return engine_.getDisplay(); }
    VoiceEngine&  getEngine() override { return engine_; }
    bool isSynthPlugin() const override { return true; }

    MicCapture& getMic() { return mic_; }

    // ---- VoicelProcessorBase
    MicCapture* getMicCapture() override { return &mic_; }

private:
    VoiceEngine engine_;
    MicCapture mic_;
    juce::AudioBuffer<float> scratch_;
    int currentLatency_ = -1;
    bool  micFailed_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceInstrumentProcessor)
};

} // namespace vm
