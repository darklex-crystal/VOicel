#pragma once

#include <cstdint>
#include <vector>

namespace vm
{

//==============================================================================
/** Résultat d'une trame d'analyse. */
struct PitchFrame
{
    float frequency = 0.0f;   // Hz, 0 si non périodique
    float clarity   = 0.0f;   // 0..1, confiance de la périodicité
    float rms       = 0.0f;   // niveau RMS sur la fenêtre
    bool  voiced    = false;  // true si une fondamentale plausible est trouvée
};

//==============================================================================
/** Détecteur de hauteur « YIN » (de Cheveigné & Kawahara, 2002).

    Fonction de différence + normalisation cumulée + seuil absolu + recherche du
    minimum local + interpolation parabolique pour la précision sub-échantillon.

    Le détecteur travaille en interne à 16 kHz : suffisant pour une voix (70 Hz
    à 1,2 kHz) et très peu coûteux, ce qui permet un taux de rafraîchissement
    de 8 ms.
*/
class YinPitchDetector
{
public:
    static constexpr int kInternalSampleRate = 16000;
    static constexpr int kHop = 128;            // 8 ms

    YinPitchDetector();

    void prepare (int windowSize);
    void reset();
    void setRange (float fMin, float fMax);
    void setThreshold (float t) { threshold_ = t; }
    void setGateDb (float db) { gateDb_ = db; }

    void pushSamples (const float* samples, int numSamples);

    /** Transfère les trames analysées (0 à plusieurs) et vide la file. */
    int popFrames (std::vector<PitchFrame>& dest);

    /** Latence d'analyse en échantillons internes (estimation centrée). */
    double getLatencyInInternalSamples() const { return windowSize_ * 0.5 + tauMax_ * 0.5; }
    int getWindowSize() const { return windowSize_; }

private:
    void analyse (std::vector<PitchFrame>& dest);

    int    windowSize_ = 1024;
    int    tauMin_     = 13;
    int    tauMax_     = 228;
    float  threshold_  = 0.15f;
    float  gateDb_     = -46.0f;

    std::vector<float>  pending_;
    std::vector<float>  diff_;
    std::vector<PitchFrame> queue_;
    int    lastTau_ = 0;   // longueur de période précédente (a priori de continuité)
};

} // namespace vm
