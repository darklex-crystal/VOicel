#include "MonoSynth.h"

#include <cmath>

namespace vm
{

namespace
{
    // M_PI n'est pas défini par tous les en-têtes <cmath> (absent de MinGW,
    // p. ex.) : on utilise une constante locale.
    constexpr double kPi = 3.14159265358979323846;
}

void MonoSynth::prepare (double sampleRate)
{
    sampleRate_ = std::max (1000.0, sampleRate);
    ampCoef_    = 1.0f - std::exp (-1.0f / (0.010f * static_cast<float> (sampleRate_)));
    filterCoef_ = 1.0f - std::exp (-1.0f / (0.060f * static_cast<float> (sampleRate_)));
    releaseCoef_= 1.0f - std::exp (-1.0f / (0.180f * static_cast<float> (sampleRate_)));
    reset();
}

void MonoSynth::reset()
{
    phase1_ = phase2_ = phaseSub_ = 0.0;
    gate_ = amp_ = filterEnv_ = 0.0f;
    gateTarget_ = ampTarget_ = filterTarget_ = 0.0f;
    low_ = band_ = 0.0f;
    activeNote_ = -1;
    velocity_ = 0.0f;
}

void MonoSynth::noteOn (int midiNote, float velocity)
{
    activeNote_ = midiNote;
    updateIncrement();
    velocity_   = std::clamp (velocity, 0.0f, 1.0f);
    gateTarget_ = 1.0f;
    ampTarget_  = 1.0f;
    filterTarget_ = 0.35f + 0.65f * velocity_;
}

void MonoSynth::noteOff (int /*midiNote*/)
{
    gateTarget_ = 0.0f;
    ampTarget_  = 0.0f;
    filterTarget_ = 0.0f;
    activeNote_ = -1;
}

void MonoSynth::setPitchBend (float semitones)
{
    const float clamped = std::clamp (semitones, -12.0f, 12.0f);
    if (std::abs (clamped - pitchOffset_) < 0.01f)
        return;

    pitchOffset_ = clamped;
    updateIncrement();
}

void MonoSynth::updateIncrement()
{
    const double note = static_cast<double> (activeNote_) + static_cast<double> (pitchOffset_);
    const double freq = 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
    increment_ = std::clamp (freq / sampleRate_, 0.0001, 0.45);
}

float MonoSynth::polyBlep (double phase, double increment)
{
    if (phase < increment)
    {
        const double t = phase / increment;
        return static_cast<float> (t + t - t * t - 1.0);
    }

    if (phase > 1.0 - increment)
    {
        const double t = (phase - 1.0) / increment;
        return static_cast<float> (t * t + t + t + 1.0);
    }

    return 0.0f;
}

void MonoSynth::render (juce::AudioBuffer<float>& out, int startSample, int numSamples, float gain)
{
    if (gain <= 0.0f || numSamples <= 0)
        return;

    const int numChannels = out.getNumChannels();
    const double detuneRatio = std::pow (2.0, detuneCents_ / 1200.0);

    for (int n = 0; n < numSamples; ++n)
    {
        // Enveloppes
        gate_ += (gateTarget_ - gate_) * ampCoef_;
        amp_  += (ampTarget_ - amp_) * (ampTarget_ > amp_ ? ampCoef_ : releaseCoef_);
        filterEnv_ += (filterTarget_ - filterEnv_) * filterCoef_;

        // Filtre passe-bas résonant (structure SVF), cutoff piloté par l'enveloppe
        const float cutoff = 200.0f + 7000.0f * filterEnv_ * filterEnv_;
        cutoff_ += (cutoff - cutoff_) * 0.0008f;
        const float f = std::clamp (2.0f * static_cast<float> (std::sin (3.14159265 *
                                 cutoff_ / static_cast<float> (sampleRate_))), 0.0f, 1.4f);
        const float q = 0.45f;

        const float in = (2.0f * static_cast<float> (phase1_) - 1.0f) * 0.5f
                       + (2.0f * static_cast<float> (phase2_) - 1.0f) * 0.35f
                       + std::sin (static_cast<float> (phaseSub_ * 2.0 * kPi)) * 0.25f;

        const float high = in - low_ - q * band_;
        band_ += f * high;
        low_  += f * band_;

        float sample = low_ * amp_ * (0.35f + 0.65f * velocity_) * gain * 0.5f;
        sample = std::tanh (sample * 1.4f) * 0.7f;

        for (int c = 0; c < numChannels; ++c)
            out.addSample (c, startSample + n, sample);

        phase1_ += increment_;
        phase2_ += increment_ * detuneRatio;
        phaseSub_ += increment_;

        if (phase1_ >= 1.0) phase1_ -= 1.0;
        if (phase2_ >= 1.0) phase2_ -= 1.0;
        if (phaseSub_ >= 1.0) phaseSub_ -= 1.0;
    }

    if (amp_ < 0.0001f && gateTarget_ <= 0.0f)
    {
        low_ = band_ = 0.0f;
        amp_ = 0.0f;
    }
}

} // namespace vm
