#include "VoiceToMidiProcessor.h"

#include "VoiceToMidiEditor.h"

namespace vm
{

VoiceToMidiProcessor::VoiceToMidiProcessor()
    : VoicelProcessorBase (BusesProperties()
                               .withInput ("Entree voix", juce::AudioChannelSet::mono(), true)
                               .withOutput ("Sortie", juce::AudioChannelSet::mono(), true)),
      engine_ (state_)
{
}

void VoiceToMidiProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    engine_.prepare (sampleRate, maximumExpectedSamplesPerBlock);
    currentLatency_ = -1;
}

void VoiceToMidiProcessor::releaseResources()
{
    engine_.releaseResources();
}

void VoiceToMidiProcessor::reset()
{
    engine_.reset();
}

void VoiceToMidiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    midi.clear();

    if (numSamples <= 0)
        return;

    // L'hote regroupe tous les bus dans un seul tampon : l'entrée occupe les
    // premiers canaux, la sortie les suivants.
    const int firstIn  = getChannelIndexInProcessBlockBuffer (true, 0, 0);
    const int firstOut = getChannelIndexInProcessBlockBuffer (false, 0, 0);
    const int numIn    = getChannelCountOfBus (true, 0);
    const int numOut   = getChannelCountOfBus (false, 0);

    if (numIn <= 0)
        return;

    if (scratch_.getNumSamples() < numSamples || scratch_.getNumChannels() < numIn)
        scratch_.setSize (juce::jmax (1, numIn), numSamples, false, false, true);

    auto* mono = scratch_.getWritePointer (0);

    if (numIn == 1)
    {
        auto* in = buffer.getReadPointer (juce::jlimit (0, buffer.getNumChannels() - 1, firstIn));
        for (int n = 0; n < numSamples; ++n)
            mono[n] = in[n];
    }
    else
    {
        const float invChannels = 1.0f / static_cast<float> (numIn);
        for (int n = 0; n < numSamples; ++n)
        {
            float s = 0.0f;
            for (int c = 0; c < numIn; ++c)
                s += buffer.getReadPointer (juce::jmin (firstIn + c, buffer.getNumChannels() - 1))[n];

            mono[n] = s * invChannels;
        }
    }

    const float monitorGain = juce::Decibels::decibelsToGain (engine_.paramFloat (pid::monitorGain));
    const float synthGain   = juce::Decibels::decibelsToGain (engine_.paramFloat (pid::synthGain));

    if (output_.getNumSamples() < numSamples)
        output_.setSize (juce::jmax (1, numOut), numSamples, false, false, true);
    else
        output_.clear();

    engine_.process (mono, numSamples, midi, numOut > 0 ? &output_ : nullptr,
                     monitorGain, synthGain);

    if (numOut > 0)
        for (int c = 0; c < numOut; ++c)
        {
            const int index = juce::jmin (firstOut + c, buffer.getNumChannels() - 1);
            buffer.copyFrom (index, 0, output_, juce::jmin (c, output_.getNumChannels() - 1), 0, numSamples);
        }

    if (engine_.getLatencySamples() != currentLatency_)
    {
        currentLatency_ = engine_.getLatencySamples();
        setLatencySamples (currentLatency_);
    }
}

bool VoiceToMidiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainInput  = layouts.getChannelSet (true, 0);
    const auto& mainOutput = layouts.getChannelSet (false, 0);

    if (mainInput != juce::AudioChannelSet::mono() && mainInput != juce::AudioChannelSet::stereo())
        return false;

    return mainOutput == juce::AudioChannelSet::mono();
}

void VoiceToMidiProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    if (auto xml = state_.copyState().createXml())
        copyXmlToBinary (*xml, destination);
}

void VoiceToMidiProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.hasType (state_.state.getType()))
            state_.replaceState (tree);
    }
}

juce::AudioProcessorEditor* VoiceToMidiProcessor::createEditor()
{
    return new vm::VoiceToMidiEditor (*this);
}

} // namespace vm
