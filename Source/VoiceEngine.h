#pragma once

#include <JuceHeader.h>

#include "MonoSynth.h"
#include "NoteTracker.h"
#include "ParamIds.h"
#include "SincResampler.h"
#include "YinPitchDetector.h"

namespace vm
{

//==============================================================================
/** Une trame de la trace d'affichage. */
struct DisplayFrame
{
    float midi    = 0.0f;    // hauteur chantée en degrés MIDI (0 = silence)
    float clarity = 0.0f;
    float level   = 0.0f;    // 0..1
    int   note    = -1;      // note jouée
    bool  voiced  = false;
};

//==============================================================================
/** Tampon circulaire d'affichage partagé entre le fil audio et l'interface. */
class DisplayState
{
public:
    static constexpr int capacity = 640;

    void push (const DisplayFrame& f)
    {
        const juce::SpinLock::ScopedLockType lock (mutex_);
        ring_[static_cast<size_t> (write_)] = f;
        write_ = (write_ + 1) % capacity;
        latest_ = f;
    }

    void setNote (int note, float clarity, float level, bool voiced)
    {
        const juce::SpinLock::ScopedLockType lock (mutex_);
        currentNote_    = note;
        currentClarity_ = clarity;
        currentLevel_   = level;
        currentVoiced_  = voiced;
    }

    /** Copie la trace, de la plus ancienne à la plus récente. */
    void read (std::vector<DisplayFrame>& dest) const
    {
        const juce::SpinLock::ScopedLockType lock (mutex_);
        dest.clear();
        dest.reserve (capacity);

        for (int i = 0; i < capacity; ++i)
            dest.push_back (ring_[static_cast<size_t> ((write_ + i) % capacity)]);
    }

    struct Current
    {
        int   note    = -1;
        float clarity = 0.0f;
        float level   = 0.0f;
        bool  voiced  = false;
    };

    Current getCurrent() const
    {
        const juce::SpinLock::ScopedLockType lock (mutex_);
        return { currentNote_, currentClarity_, currentLevel_, currentVoiced_ };
    }

    void clear()
    {
        const juce::SpinLock::ScopedLockType lock (mutex_);
        for (auto& f : ring_)
            f = {};
        write_ = 0;
        latest_ = {};
        currentNote_ = -1;
        currentClarity_ = currentLevel_ = 0.0f;
        currentVoiced_ = false;
    }

private:
    mutable juce::SpinLock mutex_;
    DisplayFrame ring_[capacity] {};
    DisplayFrame latest_ {};
    int write_ = 0;
    int currentNote_ = -1;
    float currentClarity_ = 0.0f;
    float currentLevel_ = 0.0f;
    bool currentVoiced_ = false;
};

//==============================================================================
/** Filtre passe-haut biquad (forme directe II), utilisé pour supprimer les
    graves parasites (cliquetis, rumble) avant l'analyse. */
class BiquadHighPass
{
public:
    void set (double cutoffHz, double q, double sampleRate)
    {
        const double w0    = 2.0 * 3.14159265358979323846 * cutoffHz / sampleRate;
        const double cw    = std::cos (w0);
        const double sw    = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        const double a0    = 1.0 + alpha;

        b0_ =  ((1.0 + cw) * 0.5) / a0;
        b1_ = (-(1.0 + cw))      / a0;
        b2_ =  ((1.0 + cw) * 0.5) / a0;
        a1_ = (-2.0 * cw)        / a0;
        a2_ = (1.0 - alpha)      / a0;
    }

    void reset() noexcept { x1_ = x2_ = y1_ = y2_ = 0.0f; }

    float process (float x) noexcept
    {
        const float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_; x1_ = x;
        y2_ = y1_; y1_ = y;
        return y;
    }

private:
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float x1_ = 0.0f, x2_ = 0.0f, y1_ = 0.0f, y2_ = 0.0f;
};

//==============================================================================
/** Cœur du traitement : toute la chaîne voix -> MIDI, indépendante du format
    (effet MIDI ou instrument). Regroupée ici pour que les deux plug-ins
    partagent exactement le même comportement.
*/
class VoiceEngine
{
public:
    VoiceEngine (juce::AudioProcessorValueTreeState& parameters);

    void prepare (double sampleRate, int maximumExpectedSamplesPerBlock);
    void releaseResources();
    void reset();

    /** Traite un signal mono et produit les événements MIDI.

        @param input            signal vocal déjà réduit en mono
        @param numSamples       nombre d'échantillons
        @param midi             événements MIDI en sortie
        @param output           sortie audio (peut être nul)
        @param monitorGain      gain de l'écoute directe du micro
        @param synthGain        gain du synthé interne (0 = silencieux)
    */
    void process (const float* input, int numSamples, juce::MidiBuffer& midi,
                  juce::AudioBuffer<float>* output,
                  float monitorGain, float synthGain);

    DisplayState&       getDisplay() noexcept { return display_; }
    const DisplayState& getDisplay() const noexcept { return display_; }
    NoteTracker&        getTracker() noexcept { return tracker_; }
    MonoSynth&          getSynth() noexcept { return synth_; }

    double getReportedLatency() const;
    int    getLatencySamples() const { return latencySamples_; }

    /** Index de latence de la fenêtre d'analyse (0/1/2). */
    void setLatencyIndex (int index);

    /** Lecture directe des paramètres (utilisée par les wrappers). */
    float paramFloat  (const char* id) const;
    int   paramInt    (const char* id) const;
    int   paramChoice (const char* id) const;

private:
    void readParametersInto (NoteTracker::Settings& settings);
    void updateDetectorSettings();
    void renderSynth (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                      int numSamples, float synthGain);

    juce::AudioProcessorValueTreeState& parameters_;

    SincResampler     resampler_;
    YinPitchDetector  detector_;
    NoteTracker       tracker_;
    MonoSynth         synth_;
    DisplayState      display_;
    BiquadHighPass    hpFilter_;

    std::vector<float>   resampled_;
    std::vector<PitchFrame> frames_;
    std::vector<float>   mono_;

    double sampleRate_ = 48000.0;
    double framePos_ = 0.0;
    float  levelSmooth_ = 0.0f;
    int    latencySamples_ = -1;
    int    windowSize_ = 1024;
    int    lastLatencyIndex_ = -1;
};

//==============================================================================
/** Interface commune aux deux plug-ins, pour que l'interface graphique soit
    partagée. */
class VoicelProcessorBase : public juce::AudioProcessor
{
public:
    virtual juce::AudioProcessorValueTreeState& getState() = 0;
    virtual DisplayState& getDisplay() = 0;
    virtual VoiceEngine& getEngine() = 0;

    /** Vrai pour la version instrument (le micro est ouvert par le plug-in). */
    virtual bool isSynthPlugin() const = 0;

    /** Panneau supplémentaire (sélecteur de micro) pour la version instrument. */
    virtual class MicCapture* getMicCapture() { return nullptr; }
    virtual bool isStandaloneMode() const { return false; }

    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();
    static const juce::StringArray& scaleNames();
    static const juce::StringArray& rootNames();
    static const juce::StringArray& bendRangeNames();
    static const juce::StringArray& latencyNames();
    static const juce::StringArray& velocityNames();
    static const juce::StringArray& expressionNames();
    static int windowSizeForLatencyIndex (int index);
    static int bendRangeValue (int choiceIndex);

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Voicel"; }
    void changeProgramName (int, const juce::String&) override {}
    double getTailLengthSeconds() const override { return 0.25; }

protected:
    explicit VoicelProcessorBase (const juce::AudioProcessor::BusesProperties& buses)
        : juce::AudioProcessor (buses),
          state_ (*this, nullptr, juce::Identifier ("VOICEL"), makeParameterLayout())
    {
    }

    juce::AudioProcessorValueTreeState state_;
};

} // namespace vm
