#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace vm
{

//==============================================================================
/** Rééchantillonneur rationnel à noyau sinc windowé (Blackman), table de
    256 phases précalculée. Utilisé pour ramener le signal micro (44,1 / 48 /
    96 kHz) vers 16 kHz, où travaille le détecteur de hauteur.

    Le gain DC de chaque phase est normalisé à 1,0 : pas de dérive de niveau
    quel que soit le taux de conversion.
*/
class SincResampler
{
public:
    SincResampler();

    void prepare (double sourceSampleRate, double targetSampleRate);
    void reset();

    /** Convertit `numInput` échantillons, appends le résultat dans `dest`. */
    void process (const float* src, int numInput, std::vector<float>& dest);

    /** Latence du filtre, exprimée en échantillons source. */
    double getLatencyInSourceSamples() const noexcept { return halfTaps_; }
    bool isPrepared() const noexcept { return prepared_; }

private:
    static constexpr int kTaps       = 48;    // longueur totale du noyau
    static constexpr int kSubsamples = 256;   // phases par échantillon
    static constexpr int kHistSize   = 4096;  // puissance de 2

    void buildKernel();

    float sampleAt (int64_t index) const noexcept
    {
        return hist_[static_cast<size_t> ((index + kHistSize) & (kHistSize - 1))];
    }

    std::vector<float> kernel_;
    std::vector<float> hist_;

    int     histPos_   = 0;
    int64_t inCount_   = 0;
    double  pos_       = 0.0;   // position en échantillons source
    double  step_      = 3.0;   // échantillons source par échantillon de sortie
    double  srcRate_   = 48000.0;
    double  dstRate_   = 16000.0;
    int     halfTaps_  = kTaps / 2;
    bool    prepared_  = false;
};

} // namespace vm
