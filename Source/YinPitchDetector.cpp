#include "YinPitchDetector.h"

#include <algorithm>
#include <cmath>

namespace vm
{

namespace
{
    constexpr int   kMaxTau = 640;   // 16 kHz / 640 = 25 Hz, très large
    constexpr int   kMaxWindow = 2048;
}

YinPitchDetector::YinPitchDetector()
{
    diff_.assign (kMaxTau + 2, 0.0f);
    pending_.reserve (kMaxWindow + kMaxTau);
    queue_.reserve (64);
}

void YinPitchDetector::prepare (int windowSize)
{
    windowSize_ = std::clamp (windowSize, 256, kMaxWindow);
    pending_.clear();
    queue_.clear();
    lastTau_ = 0;
    std::fill (diff_.begin(), diff_.end(), 0.0f);
}

void YinPitchDetector::reset()
{
    pending_.clear();
    queue_.clear();
    lastTau_ = 0;
}

void YinPitchDetector::setRange (float fMin, float fMax)
{
    fMin = std::max (20.0f, fMin);
    fMax = std::max (fMin * 1.5f, fMax);

    tauMin_ = std::clamp (static_cast<int> (kInternalSampleRate / fMax), 2, kMaxTau - 4);
    tauMax_ = std::clamp (static_cast<int> (kInternalSampleRate / fMin), tauMin_ + 2,
                          std::min (kMaxTau, windowSize_ / 2));
}

void YinPitchDetector::pushSamples (const float* samples, int numSamples)
{
    if (samples == nullptr || numSamples <= 0)
        return;

    pending_.insert (pending_.end(), samples, samples + numSamples);

    // Tant qu'on ne peut pas remplir une fenêtre complète + le plus long décalage
    while (static_cast<int> (pending_.size()) >= windowSize_ + tauMax_)
        analyse (queue_);
}

int YinPitchDetector::popFrames (std::vector<PitchFrame>& dest)
{
    const int n = static_cast<int> (queue_.size());
    dest.insert (dest.end(), queue_.begin(), queue_.end());
    queue_.clear();
    return n;
}

void YinPitchDetector::analyse (std::vector<PitchFrame>& dest)
{
    PitchFrame frame;

    const float* x = pending_.data();

    // --- Niveau RMS et filtre de porte ------------------------------------
    double sumSquares = 0.0;
    for (int j = 0; j < windowSize_; ++j)
        sumSquares += static_cast<double> (x[j]) * x[j];

    frame.rms = static_cast<float> (std::sqrt (sumSquares / windowSize_));
    const float db = 20.0f * std::log10 (std::max (frame.rms, 1e-7f));

    // --- Fonction de différence brute --------------------------------------
    diff_[0] = 0.0f;
    for (int tau = 1; tau <= tauMax_; ++tau)
    {
        double d = 0.0;
        for (int j = 0; j < windowSize_; ++j)
        {
            const float diff = x[j] - x[j + tau];
            d += static_cast<double> (diff) * diff;
        }
        diff_[tau] = static_cast<float> (d);
    }

    // --- Normalisation cumulée (CMND) --------------------------------------
    float running = 0.0f;
    diff_[0] = 1.0f;

    for (int tau = 1; tau <= tauMax_; ++tau)
    {
        running += diff_[tau];
        diff_[tau] = (running > 0.0f) ? diff_[tau] * static_cast<float> (tau) / running
                                      : 1.0f;
    }

    // --- Seuil absolu puis minimum local -----------------------------------
    int bestTau = -1;
    for (int tau = tauMin_; tau <= tauMax_; ++tau)
    {
        if (diff_[tau] < threshold_)
        {
            while (tau + 1 <= tauMax_ && diff_[tau + 1] < diff_[tau])
                ++tau;

            bestTau = tau;
            frame.clarity = 1.0f - diff_[tau];
            break;
        }
    }

    // --- Continuité : on privilégie la période voisine de la précédente -----
    // Sans cet a priori, la fin d'une note (fenêtre presque silencieuse) fait
    // parfois sauter la détection sur une harmonique ou une fréquence parasite.
    if (lastTau_ >= tauMin_ && lastTau_ <= tauMax_)
    {
        const int radius = std::max (2, lastTau_ / 8);
        const int lo = std::max (tauMin_, lastTau_ - radius);
        const int hi = std::min (tauMax_, lastTau_ + radius);

        int localBest = lo;
        for (int tau = lo + 1; tau <= hi; ++tau)
            if (diff_[tau] < diff_[localBest])
                localBest = tau;

        if (diff_[localBest] < 0.45f)
        {
            if (bestTau < 0 || diff_[localBest] <= diff_[bestTau] + 0.05f)
                bestTau = localBest;
        }
    }

    if (bestTau < 0)
    {
        // Pas de minimum franchissant le seuil : on tente le minimum global,
        // avec une confiance volontairement pessimiste.
        int argMin = tauMin_;
        for (int tau = tauMin_ + 1; tau <= tauMax_; ++tau)
            if (diff_[tau] < diff_[argMin])
                argMin = tau;

        if (diff_[argMin] < 0.5f)
        {
            bestTau  = argMin;
            frame.clarity = std::max (0.0f, (1.0f - diff_[argMin]) * 0.5f);
        }
    }

    // --- Interpolation parabolique ----------------------------------------
    if (bestTau > tauMin_ && bestTau < tauMax_)
    {
        const float s0 = diff_[bestTau - 1];
        const float s1 = diff_[bestTau];
        const float s2 = diff_[bestTau + 1];
        const float denom = 2.0f * (2.0f * s1 - s2 - s0);

        if (std::abs (denom) > 1e-9f)
        {
            const float shift = (s2 - s0) / denom;
            if (std::abs (shift) < 1.0f)
            {
                const float refined = static_cast<float> (bestTau) + shift;
                const float f = static_cast<float> (kInternalSampleRate) / refined;
                if (f > 0.0f)
                    frame.frequency = f;
            }
        }
    }

    if (frame.frequency <= 0.0f && bestTau > 0)
        frame.frequency = static_cast<float> (kInternalSampleRate) / static_cast<float> (bestTau);

    // --- Validation --------------------------------------------------------
    frame.voiced = bestTau > 0
                && frame.frequency > 0.0f
                && db > gateDb_
                && frame.clarity > 0.05f;

    if (! frame.voiced)
    {
        frame.frequency = 0.0f;
        frame.clarity   = 0.0f;
    }

    if (frame.voiced)
        lastTau_ = bestTau;
    else
        lastTau_ = 0;

    dest.push_back (frame);

    // Consommation d'une trame
    pending_.erase (pending_.begin(),
                    pending_.begin() + std::min (kHop, static_cast<int> (pending_.size())));
}

} // namespace vm
