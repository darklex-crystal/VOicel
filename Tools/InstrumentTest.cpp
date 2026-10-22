/*
    Test hors-ligne de la version INSTRUMENT (celle qui sert dans Ableton Live).

    On simule un micro (le flux audio entre par MicCapture, comme le ferait un
    vrai périphérique), et on vérifie que le plug-in :
      - détecte les notes et les émet en MIDI,
      - joue effectivement l'instrument (énergie audio non nulle),
      - reste silencieux quand le micro ne reçoit rien.

    Usage : VoicelInstrumentTest
*/

#include "../Source/VoiceInstrumentProcessor.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlockSize  = 256;
    constexpr int    kChannels   = 2;     // sortie stéréo de l'instrument

    double midiToFreq (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }

    juce::String noteName (int midi)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                       "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
    }

    struct SyntheticVoice
    {
        double phase = 0.0;
        uint32_t rng  = 9876;

        float nextNoise()
        {
            rng = rng * 1664525u + 1013904223u;
            return (static_cast<float> (rng >> 8) / 8388608.0f) - 1.0f;
        }

        float render (double f0, float amplitude)
        {
            const double increment = f0 / kSampleRate;
            float value = 0.0f;

            for (int h = 1; h <= 12; ++h)
                value += static_cast<float> (std::sin (phase * 2.0 * 3.14159265358979 * h)) / h;

            phase += increment;
            if (phase >= 1.0)
                phase -= 1.0;

            return value * 0.25f * amplitude;
        }
    };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Contrôle : les minuteries JUCE doivent tourner sur le fil des messages
    struct Ticker : juce::Timer { int count = 0; void timerCallback() override { ++count; } };
    Ticker ticker;
    ticker.startTimerHz (50);
    std::thread dispatcher ([] { juce::MessageManager::getInstance()->runDispatchLoop(); });
    std::this_thread::sleep_for (std::chrono::milliseconds (1200));
    juce::MessageManager::getInstance()->stopDispatchLoop();
    dispatcher.join();
    ticker.stopTimer();
    std::printf ("\n  Minuteries JUCE en 1,2 s : %d appels\n", ticker.count);

    vm::VoiceInstrumentProcessor processor;
    processor.prepareToPlay (kSampleRate, kBlockSize);

    // Micro simulé
    auto& mic = processor.getMic();
    SyntheticVoice voice;

    std::vector<float> micBlock (static_cast<size_t> (kBlockSize), 0.0f);
    juce::AudioBuffer<float> out (kChannels, kBlockSize);
    juce::MidiBuffer midi;

    juce::StringArray notesPlayed;
    double silencePeak = 0.0;
    double voicedPeak  = 0.0;
    double time = 0.0;
    bool   ok = true;

    const int totalBlocks = static_cast<int> (5.0 * kSampleRate / kBlockSize);
    bool singingPrev = false;
    double settle = 0.5;     // on ignore le relâchement après chaque note

    for (int block = 0; block < totalBlocks; ++block)
    {
        const bool singing = (time >= 0.4 && time < 1.9)
                         || (time >= 2.4 && time < 3.4);
        const double f0   = (time < 1.9) ? midiToFreq (57.0) : midiToFreq (64.0);

        if (singing != singingPrev)
        {
            settle = 0.5;
            singingPrev = singing;
        }

        settle -= static_cast<double> (kBlockSize) / kSampleRate;

        for (int n = 0; n < kBlockSize; ++n)
            micBlock[static_cast<size_t> (n)] = singing ? voice.render (f0, 0.9f)
                                                        : voice.nextNoise() * 0.002f;

        mic.pushTestSamples (micBlock.data(), kBlockSize, kSampleRate);

        out.clear();
        midi.clear();
        processor.processBlock (out, midi);

        double peak = 0.0;
        for (int c = 0; c < kChannels; ++c)
            for (int n = 0; n < kBlockSize; ++n)
                peak = std::max (peak, static_cast<double> (std::abs (out.getSample (c, n))));

        if (settle > 0.0)
        {
            // transition : on ignore (relâchement du son, latence de détection)
        }
        else if (singing)
            voicedPeak = std::max (voicedPeak, peak);
        else
            silencePeak = std::max (silencePeak, peak);

        for (auto it = midi.begin(); it != midi.end(); ++it)
        {
            if ((*it).getMessage().isNoteOn())
                notesPlayed.add (noteName ((*it).getMessage().getNoteNumber()));
        }

        time += static_cast<double> (kBlockSize) / kSampleRate;
    }

    std::printf ("\n=== Test de l'instrument (chemin Ableton Live) ===\n");
    std::printf ("  Notes MIDI detectees : ");
    for (int i = 0; i < notesPlayed.size(); ++i)
        std::printf ("%s%s", i > 0 ? " " : "", notesPlayed[i].toUTF8().getAddress());
    std::printf ("\n");

    std::printf ("  Energie de l'instrument pendant le chant : %.5f\n", voicedPeak);
    std::printf ("  Energie residuelle pendant le silence    : %.5f\n", silencePeak);
    std::printf ("  Latence declaree                          : %.1f ms\n",
                 1000.0 * processor.getLatencySamples() / kSampleRate);

    if (notesPlayed.size() < 2)
    {
        std::printf ("  !! moins de 2 notes detectees\n");
        ok = false;
    }

    if (voicedPeak < 1e-4)
    {
        std::printf ("  !! l'instrument ne produit aucun son\n");
        ok = false;
    }

    if (silencePeak > voicedPeak * 0.1)
    {
        std::printf ("  !! du son est produit alors que le micro est silencieux\n");
        ok = false;
    }

    std::printf ("\n  %s\n\n", ok ? "RESULTAT : SUCCES" : "RESULTAT : ECHEC");

    return ok ? 0 : 1;
}
