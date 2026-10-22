#include "SincResampler.h"

#include <algorithm>

namespace vm
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;
}

SincResampler::SincResampler()
{
    hist_.assign (kHistSize, 0.0f);
}

void SincResampler::prepare (double sourceSampleRate, double targetSampleRate)
{
    srcRate_ = std::max (8000.0, sourceSampleRate);
    dstRate_ = std::max (4000.0, targetSampleRate);
    step_    = srcRate_ / dstRate_;
    buildKernel();
    reset();
    prepared_ = true;
}

void SincResampler::reset()
{
    std::fill (hist_.begin(), hist_.end(), 0.0f);
    histPos_ = 0;
    inCount_ = 0;
    pos_     = 0.0;
}

void SincResampler::buildKernel()
{
    kernel_.assign (static_cast<size_t> (kSubsamples) * kTaps, 0.0f);

    // Fréquence de coupure : on reste sous la Nyquist la plus basse des deux
    // extrémités, ce qui garantit l'anti-repli de bande.
    const double ratio = 1.0 / step_;              // sortie / entrée
    const double fc    = 0.5 * std::min (1.0, ratio);
    const int    half  = halfTaps_;

    for (int p = 0; p < kSubsamples; ++p)
    {
        const double frac = static_cast<double> (p) / kSubsamples;
        double sum = 0.0;

        for (int i = 0; i < kTaps; ++i)
        {
            const double u = (i - half) - frac;   // distance au point d'interpolation

            double w = 0.0;
            if (std::abs (u) < static_cast<double> (half))
            {
                const double t = u / static_cast<double> (half);
                w = 0.42 + 0.5 * std::cos (kPi * t) + 0.08 * std::cos (2.0 * kPi * t);
            }

            const double s = (std::abs (u) < 1e-9)
                               ? fc
                               : fc * std::sin (kPi * fc * u) / (kPi * fc * u);

            const double h = s * w;
            kernel_[static_cast<size_t> (p) * kTaps + i] = static_cast<float> (h);
            sum += h;
        }

        // Gain DC exactement unité pour chaque phase
        if (std::abs (sum) > 1e-9)
            for (int i = 0; i < kTaps; ++i)
                kernel_[static_cast<size_t> (p) * kTaps + i] =
                    static_cast<float> (kernel_[static_cast<size_t> (p) * kTaps + i] / sum);
    }
}

void SincResampler::process (const float* src, int numInput, std::vector<float>& dest)
{
    dest.clear();

    if (! prepared_ || numInput <= 0 || src == nullptr)
        return;

    const int maxOut = static_cast<int> (1.0 / step_) + 2;

    for (int i = 0; i < numInput; ++i)
    {
        hist_[static_cast<size_t> (histPos_)] = src[i];
        histPos_ = (histPos_ + 1) & (kHistSize - 1);
        ++inCount_;

        int produced = 0;

        while (pos_ + halfTaps_ < static_cast<double> (inCount_) && produced < maxOut)
        {
            const int64_t n0    = static_cast<int64_t> (std::floor (pos_));
            const double  frac  = pos_ - static_cast<double> (n0);

            int phase = static_cast<int> (frac * kSubsamples + 0.5);
            if (phase >= kSubsamples) phase -= kSubsamples;

            const float* k = kernel_.data() + static_cast<size_t> (phase) * kTaps;

            float sum = 0.0f;
            for (int t = 0; t < kTaps; ++t)
                sum += k[t] * sampleAt (n0 - halfTaps_ + t);

            dest.push_back (sum);
            pos_ += step_;
            ++produced;
        }
    }
}

} // namespace vm
