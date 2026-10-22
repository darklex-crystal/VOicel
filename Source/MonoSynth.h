#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace vm
{

//==============================================================================
/** Synthé monophonique minimaliste utilisé par l'application autonome pour
    entendre les notes détectées sans matériel MIDI. Deux dents de scie
    désaccordées + sous-sine, filtre passe-bas résonant à enveloppe, ADSR.

    Volontairement simple : c'est un outil de contrôle, pas un instrument.
*/
class MonoSynth
{
public:
    void prepare (double sampleRate);
    void reset();

    void noteOn  (int midiNote, float velocity);
    void noteOff (int midiNote);
    void setPitchBend (float semitones);

    bool isNoteOn() const noexcept { return gate_ > 0.001f; }
    int  getActiveNote() const noexcept { return activeNote_; }

    void render (juce::AudioBuffer<float>& out, int startSample, int numSamples, float gain);

private:
    static float polyBlep (double phase, double increment);
    void  updateIncrement();

    double sampleRate_ = 48000.0;

    double phase1_ = 0.0, phase2_ = 0.0, phaseSub_ = 0.0;
    double increment_ = 0.1;
    double detuneCents_ = 8.0;
    float  pitchOffset_ = 0.0f;

    float gate_        = 0.0f;
    float gateTarget_  = 0.0f;
    float amp_         = 0.0f;
    float ampTarget_   = 0.0f;
    float filterEnv_   = 0.0f;
    float filterTarget_= 0.0f;
    float cutoff_      = 1200.0f;

    float low_ = 0.0f, band_ = 0.0f;
    float velocity_ = 0.0f;
    int   activeNote_ = -1;

    float ampCoef_ = 0.001f, filterCoef_ = 0.001f, releaseCoef_ = 0.001f;
};

} // namespace vm
