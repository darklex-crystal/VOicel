#include "VoiceInstrumentProcessor.h"

#include "VoiceToMidiEditor.h"
#include "DebugLog.h"

namespace vm
{

VoiceInstrumentProcessor::VoiceInstrumentProcessor()
    : VoicelProcessorBase (BusesProperties()
                               .withOutput ("Sortie", juce::AudioChannelSet::stereo(), true)),
      engine_ (state_)
{
    dbg ("VoiceInstrumentProcessor: constructeur");
}

VoiceInstrumentProcessor::~VoiceInstrumentProcessor()
{
    mic_.shutdown();
}

void VoiceInstrumentProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    dbg ("prepareToPlay: " + juce::String (sampleRate) + " Hz, bloc " + juce::String (maximumExpectedSamplesPerBlock));
    engine_.prepare (sampleRate, maximumExpectedSamplesPerBlock);
    mic_.prepare (sampleRate, maximumExpectedSamplesPerBlock);
    currentLatency_ = -1;
}

void VoiceInstrumentProcessor::releaseResources()
{
    // Le micro reste ouvert : les hôtes appellent releaseResources /
    // prepareToPlay à chaque arrêt de lecture, et rien ne le rouvrirait.
    // Il est fermé dans le destructeur.
    engine_.releaseResources();
}

void VoiceInstrumentProcessor::reset()
{
    engine_.reset();
}

void VoiceInstrumentProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    static bool firstBlock = true;
    if (firstBlock) { firstBlock = false; dbg ("processBlock: premier bloc"); }

    const int numSamples = buffer.getNumSamples();
    const int numOut     = getChannelCountOfBus (false, 0);

    midi.clear();

    if (numSamples <= 0)
        return;

    // --- Acquisition du micro ---------------------------------------------
    if (scratch_.getNumSamples() < numSamples || scratch_.getNumChannels() < 1)
        scratch_.setSize (1, numSamples, false, false, true);

    mic_.fill (scratch_.getWritePointer (0), numSamples);

    const float synthGain = juce::Decibels::decibelsToGain (engine_.paramFloat (pid::synthGain));

    // --- Analyse + sortie --------------------------------------------------
    if (buffer.getNumChannels() > 0 && buffer.getNumSamples() >= numSamples)
    {
        engine_.process (scratch_.getReadPointer (0), numSamples, midi, &buffer, 0.0f, synthGain);
    }
    else
    {
        engine_.process (scratch_.getReadPointer (0), numSamples, midi, nullptr, 0.0f, synthGain);
    }

    // Buffers de l'hôte plus grands que le bloc attendu : on efface le reste.
    for (int c = 0; c < buffer.getNumChannels(); ++c)
    {
        auto* ch = buffer.getWritePointer (c);
        std::fill (ch + numSamples, ch + buffer.getNumSamples(), 0.0f);
    }

    juce::ignoreUnused (numOut);

    if (engine_.getLatencySamples() != currentLatency_)
    {
        currentLatency_ = engine_.getLatencySamples();
        setLatencySamples (currentLatency_);
    }
}

bool VoiceInstrumentProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getChannelSet (false, 0);

    return mainOutput == juce::AudioChannelSet::stereo()
        || mainOutput == juce::AudioChannelSet::mono();
}

void VoiceInstrumentProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    if (auto xml = state_.copyState().createXml())
        copyXmlToBinary (*xml, destination);
}

void VoiceInstrumentProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.hasType (state_.state.getType()))
            state_.replaceState (tree);
    }
}

juce::AudioProcessorEditor* VoiceInstrumentProcessor::createEditor()
{
    dbg ("createEditor");
    return new vm::VoiceToMidiEditor (*this);
}

} // namespace vm
