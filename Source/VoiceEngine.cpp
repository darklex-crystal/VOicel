#include "VoiceEngine.h"

#include <algorithm>

namespace vm
{

//==============================================================================
const juce::StringArray& VoicelProcessorBase::scaleNames()
{
    static const juce::StringArray names { "Libre", "Chromatique", "Majeur", "Mineur",
                                          "Penta mineure", "Penta majeure", "Blues",
                                          "Dorien", "Lydien" };
    return names;
}

const juce::StringArray& VoicelProcessorBase::rootNames()
{
    static const juce::StringArray names { "C", "C#", "D", "D#", "E", "F",
                                          "F#", "G", "G#", "A", "A#", "B" };
    return names;
}

const juce::StringArray& VoicelProcessorBase::bendRangeNames()
{
    static const juce::StringArray names { "0 - fixe", "1 demi-ton",
                                          "2 demi-tons", "4 demi-tons", "12 demi-tons" };
    return names;
}

const juce::StringArray& VoicelProcessorBase::latencyNames()
{
    static const juce::StringArray names { "48 ms", "64 ms", "96 ms" };
    return names;
}

const juce::StringArray& VoicelProcessorBase::velocityNames()
{
    static const juce::StringArray names { "Fixe", "Dynamique" };
    return names;
}

const juce::StringArray& VoicelProcessorBase::expressionNames()
{
    static const juce::StringArray names { "Desactive", "CC 11 - Expression",
                                          "CC 7 - Volume", "CC 10 - Panorama",
                                          "CC 1 - Modulation" };
    return names;
}

int VoicelProcessorBase::windowSizeForLatencyIndex (int index)
{
    switch (index)
    {
        case 0:  return 768;    // ~48 ms a 16 kHz
        case 2:  return 1536;   // ~96 ms
        default: return 1024;   // ~64 ms
    }
}

int VoicelProcessorBase::bendRangeValue (int choiceIndex)
{
    static const int values[] = { 0, 1, 2, 4, 12 };
    return values[juce::jlimit (0, 4, choiceIndex)];
}

juce::AudioProcessorValueTreeState::ParameterLayout VoicelProcessorBase::makeParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&layout] (const char* id, const char* name, float minV, float maxV, float def)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (id, 1), juce::String (name),
            juce::NormalisableRange<float> (minV, maxV), def));
    };

    auto addChoice = [&layout] (const char* id, const char* name,
                                const juce::StringArray& choices, int defaultIndex)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (id, 2), juce::String (name), choices, defaultIndex));
    };

    // --- Voix
    addFloat (pid::inputGain,   "Gain d'entree (dB)",  -36.0f, 36.0f, 0.0f);
    addFloat (pid::monitorGain, "Echo voix (dB)",      -60.0f, 12.0f, -60.0f);
    addFloat (pid::synthGain,   "Synthese (dB)",       -60.0f,  6.0f,  -6.0f);
    addFloat (pid::gateDb,      "Seuil de voix (dB)",  -80.0f, -20.0f, -46.0f);
    addFloat (pid::fMin,        "Frequence min (Hz)",   55.0f, 300.0f, 70.0f);
    addFloat (pid::fMax,        "Frequence max (Hz)",  300.0f, 2500.0f, 1200.0f);
    addFloat (pid::sensitivity, "Sensibilite",          0.05f, 0.50f, 0.15f);
    addChoice (pid::latencyMode, "Latence d'analyse", latencyNames(), 1);
    addFloat (pid::holdMs,      "Maintien (ms)",           0.0f, 400.0f, 60.0f);

    // --- Note
    addFloat (pid::noteMin,     "Note basse",              0.0f, 127.0f, 36.0f);
    addFloat (pid::noteMax,     "Note haute",              0.0f, 127.0f, 84.0f);
    addChoice (pid::bendRange,  "Plage de bend", bendRangeNames(), 2);
    addFloat (pid::glideMs,     "Glissando (ms)",          0.0f, 400.0f, 45.0f);
    addFloat (pid::octaveShift, "Transposer (+/- octave)", -2.0f, 2.0f, 0.0f);

    // --- Accord
    addFloat (pid::tuneAmount,  "Correction (%)",        0.0f, 100.0f, 0.0f);
    addFloat (pid::retuneSpeed, "Vitesse correction (ms)", 0.0f, 300.0f, 20.0f);
    addFloat (pid::tuneLock,    "Verrouillage (cents)",    0.0f, 120.0f, 40.0f);
    addChoice (pid::scale,   "Gamme", scaleNames(), 0);
    addChoice (pid::root,    "Tonalite", rootNames(), 0);
    addChoice (pid::velocityMode, "Velocite", velocityNames(), 1);
    addFloat (pid::velocityMax, "Velocite max", 1.0f, 127.0f, 100.0f);

    // --- Sortie
    addChoice (pid::exprCC, "Expression (CC)", expressionNames(), 0);
    addChoice (pid::midiChannel, "Canal MIDI",
               [] {
                   juce::StringArray channels;
                   for (int i = 1; i <= 16; ++i)
                       channels.add (juce::String (i));
                   return channels;
               }(), 0);

    return layout;
}

//==============================================================================
VoiceEngine::VoiceEngine (juce::AudioProcessorValueTreeState& parameters)
    : parameters_ (parameters)
{
    hpFilter_.set (55.0, 0.707, 48000.0);
}

void VoiceEngine::prepare (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRate_ = sampleRate;

    mono_.resize (static_cast<size_t> (juce::jmax (16, maximumExpectedSamplesPerBlock)));
    resampled_.reserve (static_cast<size_t> (maximumExpectedSamplesPerBlock) + 64);
    frames_.reserve (64);

    resampler_.prepare (sampleRate, static_cast<double> (YinPitchDetector::kInternalSampleRate));

    lastLatencyIndex_ = paramChoice (pid::latencyMode);
    windowSize_ = VoicelProcessorBase::windowSizeForLatencyIndex (lastLatencyIndex_);
    detector_.prepare (windowSize_);

    hpFilter_.set (55.0, 0.707, sampleRate);
    hpFilter_.reset();

    synth_.prepare (sampleRate);
    latencySamples_ = -1;
    reset();
}

void VoiceEngine::releaseResources()
{
    mono_.clear();
    resampled_.clear();
    frames_.clear();
}

void VoiceEngine::reset()
{
    resampler_.reset();
    detector_.reset();
    synth_.reset();
    display_.clear();
    hpFilter_.reset();
    framePos_ = 0.0;
    levelSmooth_ = 0.0f;

    NoteTracker::Settings s;
    readParametersInto (s);
    tracker_.setSettings (s);
    tracker_.reset (s.channel);
}

void VoiceEngine::setLatencyIndex (int index)
{
    if (index == lastLatencyIndex_)
        return;

    lastLatencyIndex_ = index;
    windowSize_ = VoicelProcessorBase::windowSizeForLatencyIndex (index);
    detector_.prepare (windowSize_);
}

//==============================================================================
float VoiceEngine::paramFloat (const char* id) const
{
    if (auto* p = parameters_.getParameter (id))
        if (auto* v = dynamic_cast<juce::AudioParameterFloat*> (p))
            return v->get();

    return 0.0f;
}

int VoiceEngine::paramInt (const char* id) const
{
    return static_cast<int> (std::lround (paramFloat (id)));
}

int VoiceEngine::paramChoice (const char* id) const
{
    if (auto* p = parameters_.getParameter (id))
        if (auto* v = dynamic_cast<juce::AudioParameterChoice*> (p))
            return v->getIndex();

    return 0;
}

void VoiceEngine::readParametersInto (NoteTracker::Settings& s)
{
    s.noteMin            = juce::jlimit (0, 127, paramInt (pid::noteMin));
    s.noteMax            = juce::jlimit (0, 127, paramInt (pid::noteMax));

    if (s.noteMax < s.noteMin)
        std::swap (s.noteMin, s.noteMax);

    s.bendRangeSemitones = VoicelProcessorBase::bendRangeValue (paramChoice (pid::bendRange));
    s.glideSeconds       = paramFloat (pid::glideMs) * 0.001f;
    s.holdSeconds        = paramFloat (pid::holdMs) * 0.001f;
    s.octaveShift        = juce::jlimit (-2, 2, paramInt (pid::octaveShift));
    s.tuneAmount         = juce::jlimit (0.0f, 1.0f, paramFloat (pid::tuneAmount) * 0.01f);
    s.retuneSeconds      = juce::jlimit (0.0f, 0.3f, paramFloat (pid::retuneSpeed) * 0.001f);
    s.lockCents          = juce::jlimit (0.0f, 120.0f, paramFloat (pid::tuneLock));
    s.scale              = paramChoice (pid::scale);
    s.root               = paramChoice (pid::root);
    s.channel            = juce::jlimit (1, 16, paramChoice (pid::midiChannel) + 1);
    s.dynamicVelocity    = paramChoice (pid::velocityMode) == 1;
    s.velocityMax        = juce::jlimit (1, 127, paramInt (pid::velocityMax));
    s.gateDb             = paramFloat (pid::gateDb);

    switch (paramChoice (pid::exprCC))
    {
        case 1:  s.expressionCC = 11; break;
        case 2:  s.expressionCC = 7;  break;
        case 3:  s.expressionCC = 10; break;
        case 4:  s.expressionCC = 1;  break;
        default: s.expressionCC = -1; break;
    }
}

void VoiceEngine::updateDetectorSettings()
{
    detector_.setRange (paramFloat (pid::fMin), paramFloat (pid::fMax));
    detector_.setThreshold (paramFloat (pid::sensitivity));
    detector_.setGateDb (paramFloat (pid::gateDb));
}

double VoiceEngine::getReportedLatency() const
{
    const double internal = detector_.getLatencyInInternalSamples();

    return resampler_.getLatencyInSourceSamples()
         + internal * (sampleRate_ / static_cast<double> (YinPitchDetector::kInternalSampleRate));
}

namespace
{
    float gainToUnit (float gain)
    {
        const float db = 20.0f * std::log10 (std::max (gain, 1e-6f));
        return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    }
}

//==============================================================================
void VoiceEngine::process (const float* input, int numSamples, juce::MidiBuffer& midi,
                           juce::AudioBuffer<float>* output,
                           float monitorGain, float synthGain)
{
    if (numSamples <= 0)
        return;

    setLatencyIndex (paramChoice (pid::latencyMode));
    updateDetectorSettings();

    NoteTracker::Settings settings;
    readParametersInto (settings);
    tracker_.setSettings (settings);

    // --- Pré-traitement : gain, passe-haut, niveau --------------------------
    const float inputGain = juce::Decibels::decibelsToGain (paramFloat (pid::inputGain));
    // Certains hôtes envoient des blocs plus grands que ce qu'ils ont annoncé
    // à prepareToPlay : sans cette garde, on écrivait hors du tampon.
    if (mono_.size() < static_cast<size_t> (numSamples))
        mono_.resize (static_cast<size_t> (numSamples));

    float* mono = mono_.data();
    float peak = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        mono[n] = hpFilter_.process (juce::jlimit (-8.0f, 8.0f, input != nullptr ? input[n] : 0.0f))
                                * inputGain;
        peak = std::max (peak, std::abs (mono[n]));
    }

    const float decay = std::exp (-static_cast<float> (numSamples / sampleRate_) * 6.0f);
    levelSmooth_ = std::max (gainToUnit (peak), levelSmooth_ * decay);

    // --- Analyse ------------------------------------------------------------
    resampler_.process (mono, numSamples, resampled_);

    if (! resampled_.empty())
        detector_.pushSamples (resampled_.data(), static_cast<int> (resampled_.size()));

    frames_.clear();
    detector_.popFrames (frames_);

    const double hopSeconds     = static_cast<double> (YinPitchDetector::kHop)
                                / static_cast<double> (YinPitchDetector::kInternalSampleRate);
    const double hopHostSamples = hopSeconds * sampleRate_;

    // Les trames d'un même bloc sont espacées d'un « hop » : on répartit leurs
    // événements MIDI dans le bloc au lieu de les empiler au même endroit.
    double framePos = 0.0;

    for (const auto& frame : frames_)
    {
        const int offset = juce::jlimit (0, numSamples - 1, static_cast<int> (framePos));
        framePos += hopHostSamples;

        tracker_.processFrame (frame, offset, midi, static_cast<float> (hopSeconds));

        DisplayFrame df;
        df.midi    = frame.voiced ? tracker_.getDisplayMidi() : 0.0f;
        df.clarity = frame.clarity;
        df.level   = levelSmooth_;
        df.note    = tracker_.getActiveNote();
        df.voiced  = frame.voiced;
        display_.push (df);
    }

    display_.setNote (tracker_.getActiveNote(),
                      frames_.empty() ? 0.0f : frames_.back().clarity,
                      levelSmooth_,
                      ! frames_.empty() && frames_.back().voiced);

    // --- Sortie audio --------------------------------------------------------
    if (output != nullptr && output->getNumSamples() >= numSamples)
    {
        for (int c = 0; c < output->getNumChannels(); ++c)
        {
            auto* out = output->getWritePointer (c);
            for (int n = 0; n < numSamples; ++n)
                out[n] = mono[n] * monitorGain;
        }
    }

    if (output != nullptr && synthGain > 0.0001f)
        renderSynth (*output, midi, numSamples, synthGain);
    else if (synth_.isNoteOn())
        synth_.noteOff (0);

    latencySamples_ = juce::jmax (0, static_cast<int> (std::ceil (getReportedLatency())));
}

void VoiceEngine::renderSynth (juce::AudioBuffer<float>& buffer,
                               juce::MidiBuffer& midi,
                               int numSamples, float synthGain)
{
    // Le suiveur de notes envoie +/-1 pour +/-(plage / 2) demi-tons.
    const float bendSemitones = 0.5f * static_cast<float> (
        VoicelProcessorBase::bendRangeValue (paramChoice (pid::bendRange)));

    auto it = midi.begin();
    const auto end = midi.end();

    int n = 0;
    while (n < numSamples)
    {
        while (it != end && (*it).samplePosition <= n)
        {
            const auto msg = (*it).getMessage();

            if (msg.isNoteOn())
                synth_.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
            else if (msg.isNoteOff() || msg.isAllNotesOff() || msg.isAllSoundOff())
                synth_.noteOff (msg.getNoteNumber());
            else if (msg.isPitchWheel())
                synth_.setPitchBend (static_cast<float> (msg.getPitchWheelValue() - 8192)
                                     / 8192.0f * bendSemitones);

            ++it;
        }

        int next = numSamples;
        if (it != end)
            next = juce::jmin (numSamples,
                               juce::jmax (n + 1, static_cast<int> ((*it).samplePosition)));

        synth_.render (buffer, n, next - n, synthGain);
        n = next;
    }
}

} // namespace vm
