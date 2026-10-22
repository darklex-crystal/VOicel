#pragma once

#include <JuceHeader.h>

#include "VoiceEngine.h"

namespace vm
{

//==============================================================================
/** Anneau sans verrou entre le fil du périphérique audio (écriture) et le fil
    de rendu de l'hôte (lecture).

    IMPORTANT : la mémoire est allouée UNE SEULE FOIS (constructeur) et ne
    change plus jamais. Les anciennes versions réallouaient le tampon quand le
    micro démarrait, pendant que le fil audio de l'hôte le lisait : c'est un
    accès mémoire invalide (plantage aléatoire à l'ouverture). */
class FloatRing
{
public:
    static constexpr int kSize = 1 << 17;   // 131072 échantillons (~0,68 s à 192 kHz)

    FloatRing() : data_ (static_cast<size_t> (kSize), 0.0f) {}

    void write (const float* src, int numSamples) noexcept
    {
        int64_t w = write_.load (std::memory_order_relaxed);

        for (int n = 0; n < numSamples; ++n)
        {
            data_[static_cast<size_t> (w & (kSize - 1))] = src[n];
            ++w;
        }

        write_.store (w, std::memory_order_release);
    }

    int64_t getNumAvailable() const noexcept
    {
        return write_.load (std::memory_order_acquire) - read_.load (std::memory_order_relaxed);
    }

    /** Échantillon situé à read_ + offset (0 si non encore disponible). */
    float sampleAt (int64_t offset) const noexcept
    {
        if (offset < 0 || offset >= getNumAvailable())
            return 0.0f;

        return data_[static_cast<size_t> ((read_.load (std::memory_order_relaxed) + offset) & (kSize - 1))];
    }

    void advance (int64_t numSamples) noexcept
    {
        int64_t r = read_.load (std::memory_order_relaxed) + numSamples;
        const int64_t w = write_.load (std::memory_order_acquire);

        // Si le lecteur a pris trop de retard, on se resynchronise.
        if (r + 16384 < w)
            r = w - 4096;

        read_.store (r, std::memory_order_relaxed);
    }

    /** Vide l'anneau sans toucher à la mémoire (peut être appelé de n'importe
        quel fil). */
    void clear() noexcept
    {
        read_.store (write_.load (std::memory_order_acquire), std::memory_order_relaxed);
    }

private:
    std::vector<float> data_;
    std::atomic<int64_t> write_ { 0 };
    std::atomic<int64_t> read_  { 0 };
};

//==============================================================================
/** Ouvre un périphérique d'entrée audio pour le plug-in, indépendamment du
    gestionnaire de l'hôte. Indispensable dans Ableton Live, où un instrument
    n'a pas de bus d'entrée audio : c'est le plug-in qui doit ouvrir le micro.

    L'échantillonnage du micro peut différer de celui de l'hôte : la conversion
    est faite par interpolation linéaire au moment de la lecture.
*/
class MicCapture : private juce::AudioIODeviceCallback
{
public:
    MicCapture() { mixBuffer_.reserve (8192); }
    ~MicCapture() override { shutdown(); }

    void prepare (double hostSampleRate, int maximumExpectedSamplesPerBlock);
    void shutdown();

    /** Liste « Type - Nom » de toutes les entrées disponibles. */
    juce::StringArray getInputNames();

    bool openInput (int index);
    /** Ouvre la première entrée disponible. */
    bool openDefault() { return openInput (0); }
    void closeInput();
    bool isOpen() const { return manager_.getCurrentAudioDevice() != nullptr; }

    juce::String getCurrentInputName() const;
    const juce::String& getError() const { return error_; }
    float getLevel() const { return level_.load (std::memory_order_relaxed); }

    /** Remplit numSamples échantillons à la fréquence de l'hôte. */
    void fill (float* dest, int numSamples);

#if defined(VM_TESTING)
    /** Test hors-ligne uniquement : simule un micro. */
    void pushTestSamples (const float* src, int numSamples, double rate)
    {
        micRate_.store (rate);
        ring_.write (src, numSamples);
    }
#endif

private:
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext (const float* const* inputData, int numInputChannels,
                                           float* const* outputData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

    juce::AudioDeviceManager manager_;
    FloatRing ring_;
    std::vector<float> mixBuffer_;

    std::atomic<double> hostRate_ { 48000.0 };
    std::atomic<double> micRate_  { 0.0 };
    std::atomic<bool>   resync_   { true };
    std::atomic<float>  level_    { 0.0f };
    double phase_ = 0.0;                    // fil audio de l'hôte uniquement
    juce::String error_;
    bool   opened_ = false;
};

//==============================================================================
/** Petit panneau de sélection du micro (version instrument uniquement). */
class MicPanel : public juce::Component,
                private juce::Timer
{
public:
    MicPanel (MicCapture& capture, VoiceEngine& engine);
    ~MicPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    void refreshDevices();
    void updateStatus();
    void setStatus (const juce::String& text);

    MicCapture& capture_;
    VoiceEngine& engine_;

    juce::ComboBox  deviceCombo_;
    juce::TextButton refreshButton_ { "Actualiser" };
    juce::Label      statusLabel_;
    juce::StringArray devices_;
    int  meter_ = 0;
    int  currentId_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicPanel)
};

} // namespace vm
