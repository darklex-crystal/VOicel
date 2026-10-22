/*
    Test hors-ligne de la chaîne DSP de Voicel MIDI.

    On synthétise une voix (harmoniques + vibrato + gliding), on la fait passer
    dans le processeur, et on vérifie que les notes MIDI émises correspondent.

    Usage : VoicelDspTest
*/

#include "../Source/VoiceToMidiProcessor.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlockSize  = 256;
    constexpr int    kChannels   = 2;     // 1 entrée + 1 sortie, comme dans un hôte

    struct NoteEvent
    {
        double time;
        bool    on;
        int     note;
        int     velocity;
    };

    double midiToFreq (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }

    juce::String noteName (int midi)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                       "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
    }

    /** Voix synthétique : empilement d'harmoniques avec vibrato et glisse. */
    struct SyntheticVoice
    {
        double phase = 0.0;
        uint32_t rng  = 12345;

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

    vm::VoiceToMidiProcessor processor;
    processor.prepareToPlay (kSampleRate, kBlockSize);

    std::vector<NoteEvent> events;
    std::vector<double>     steadyPitch;
    std::vector<double>     steadyTarget;

    juce::AudioBuffer<float> buffer (kChannels, kBlockSize);
    juce::MidiBuffer midi;
    SyntheticVoice voice;

    const double totalDuration = 4.5;
    const int    totalSamples  = static_cast<int> (totalDuration * kSampleRate);
    int          position      = 0;
    double       nextSampleAt  = 0.0;

    while (position < totalSamples)
    {
        buffer.clear();

        float* input = buffer.getWritePointer (0);
        auto* output = buffer.getWritePointer (1);

        for (int n = 0; n < kBlockSize; ++n)
        {
            const double t = (position + n) / kSampleRate;

            double f0 = 0.0;
            float  amplitude = 0.0f;

            if (t >= 0.5 && t < 1.5)                       // A3 tenu avec vibrato
            {
                f0 = midiToFreq (57.0 + 0.25 * std::sin (2.0 * 3.14159265 * 5.0 * t));
                amplitude = 0.9f;
            }
            else if (t >= 1.5 && t < 2.5)                  // A3 -> C4 -> E4
            {
                const int step = static_cast<int> ((t - 1.5) / (1.0 / 3.0));
                static const double melody[] = { 57.0, 60.0, 64.0 };
                f0 = midiToFreq (melody[juce::jlimit (0, 2, step)]);
                amplitude = 0.9f;
            }
            else if (t >= 2.5 && t < 3.0)                  // silence
            {
                f0 = 220.0; amplitude = 0.0f;
            }
            else if (t >= 3.0 && t < 3.6)                  // bruit de fond
            {
                f0 = 220.0; amplitude = 0.0f;
            }
            else if (t >= 3.6)                             // D2 grave
            {
                f0 = midiToFreq (38.0);
                amplitude = 0.9f;
            }

            float sample = amplitude > 0.0f ? voice.render (f0, amplitude) : 0.0f;

            if (t >= 3.0 && t < 3.6)
                sample = voice.nextNoise() * 0.002f;       // -54 dBFS

            input[n] = sample;
            output[n] = 0.0f;
        }

        midi.clear();
        processor.processBlock (buffer, midi);

        // --- Collecte des événements -----------------------------------------
        for (auto it = midi.begin(); it != midi.end(); ++it)
        {
            const auto message = (*it).getMessage();

            if (message.isNoteOn() || message.isNoteOff())
                events.push_back ({ (position + (*it).samplePosition) / kSampleRate,
                                    message.isNoteOn(),
                                    message.getNoteNumber(),
                                    message.getVelocity() });
        }

        // --- Échantillonnage de la hauteur détectée (segment A3 tenu) -------
        if (position / kSampleRate >= nextSampleAt)
        {
            nextSampleAt = position / kSampleRate + 0.02;

            std::vector<vm::DisplayFrame> trace;
            processor.getDisplay().read (trace);

            if (! trace.empty())
            {
                const double t = position / kSampleRate;
                if (t > 0.8 && t < 1.45 && trace.back().midi > 0.0f)
                {
                    steadyPitch.push_back (trace.back().midi);
                    steadyTarget.push_back (57.0 + 0.25 * std::sin (2.0 * 3.14159265 * 5.0 * t));
                }
            }
        }

        position += kBlockSize;
    }

    // --- Rapport -------------------------------------------------------------
    std::printf ("\n=== Événements MIDI détectés ===\n");

    for (const auto& e : events)
        std::printf ("  %6.3f s  %-8s %-5s (note %3d, vél. %3d)\n",
                     e.time, e.on ? "noteOn" : "noteOff", noteName (e.note).toRawUTF8(),
                     e.note, e.velocity);

    bool silenceClean = true;
    for (const auto& e : events)
        if (e.on && (e.time < 0.45f || (e.time >= 2.45f && e.time < 3.55f)))
            silenceClean = false;

    double  meanError = 0.0;
    double  maxError  = 0.0;

    for (size_t i = 0; i < steadyPitch.size(); ++i)
    {
        const double error = 100.0 * (steadyPitch[i] - steadyTarget[i]);
        meanError += error;
        maxError = std::max (maxError, std::abs (error));
    }

    if (! steadyPitch.empty())
        meanError /= static_cast<double> (steadyPitch.size());

    int distinctNotes = 0;
    {
        std::vector<int> played;
        for (const auto& e : events)
            if (e.on && std::find (played.begin(), played.end(), e.note) == played.end())
                played.push_back (e.note);

        distinctNotes = static_cast<int> (played.size());
        std::printf ("\nNotes distinctes jouées :");
        for (int n : played)
            std::printf (" %s", noteName (n).toRawUTF8());
        std::printf ("\n");
    }

    // ---- Phase 2 : correction de justesse (autotune) ----------------------
    // On regle la correction a 100 % et la gamme sur do majeur, puis on chante
    // un faussaire : le resultat doit rester dans la gamme (A3), et surtout le
    // processeur ne doit pas se bloquer (mode "correction dure").
    std::vector<int> tunedNotes;
    bool  tuneOk = true;
    {
        auto& state = processor.getState();

        // Un AudioParameterChoice se regle par une valeur normalisee :
        // index / nombre de choix.
        auto setChoice = [&state] (const char* id, int index)
        {
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
                p->setValueNotifyingHost (static_cast<float> (index)
                                          / static_cast<float> (p->choices.size()));
        };

        auto setFloat = [&state] (const char* id, float value)
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (state.getParameter (id)))
                p->setValueNotifyingHost (value);
        };

        setChoice ("bend_range", 0);      // 0 demi-ton : correction dure
        setChoice ("scale", 1);           // majeur
        setChoice ("root", 0);            // do
        setFloat  ("tune_amount", 100.0f);

        juce::MidiBuffer tuneMidi;
        SyntheticVoice tuneVoice;
        double t = 0.0;

        auto* tuneInput  = buffer.getWritePointer (0);
        auto* tuneOutput = buffer.getWritePointer (1);

        // Deux attaques séparées par un silence : la premiere laisse le
        // detecteur se stabiliser (filtre d'octave, confirmation de note), la
        // seconde fournit un note-on tardif, donc analysable.
        for (int block = 0; block < static_cast<int> (2.2 * kSampleRate / kBlockSize); ++block)
        {
            // A3 (57) volontairement fausse de +35 cents
            const double f0 = midiToFreq (57.0 + 0.35 / 100.0);
            const bool singing = (t > 0.15 && t < 0.9) || (t > 1.1 && t < 2.0);

            for (int n = 0; n < kBlockSize; ++n)
                tuneInput[n] = singing ? tuneVoice.render (f0, 0.9f) : 0.0f;

            for (int n = 0; n < kBlockSize; ++n)
                tuneOutput[n] = 0.0f;

            tuneMidi.clear();
            processor.processBlock (buffer, tuneMidi);

            for (auto it = tuneMidi.begin(); it != tuneMidi.end(); ++it)
                if ((*it).getMessage().isNoteOn() && t > 1.05)  // transitoires d'attaque ignores
                {
                    const int n = (*it).getMessage().getNoteNumber();
                    if (std::find (tunedNotes.begin(), tunedNotes.end(), n) == tunedNotes.end())
                        tunedNotes.push_back (n);
                }

            t += static_cast<double> (kBlockSize) / kSampleRate;
        }

        // do majeur : A (9) et C# (1) sont hors gamme -> seul A3 est acceptable
        for (int n : tunedNotes)
            if (n != 57)
                tuneOk = false;

        if (tunedNotes.empty())
            tuneOk = false;

        std::printf ("\nNotes émises avec correction dure (A3chanté +35 cents) :");
        for (int n : tunedNotes)
            std::printf (" %s", noteName (n).toRawUTF8());
        std::printf ("\n");
    }

    const bool steadyOk = ! steadyPitch.empty() && std::abs (meanError) < 20.0 && maxError < 60.0;
    const bool melodyOk  = distinctNotes >= 4;   // A3, C4, E4 + D2 grave

    std::printf ("\n=== Résultats ===\n");
    std::printf ("  Trames analysées sur la note tenue : %d\n", static_cast<int> (steadyPitch.size()));
    std::printf ("  Écart moyen de hauteur   : %+.1f cents (attendu |x| < 20)\n", meanError);
    std::printf ("  Écart maximal           :  %.1f cents (attendu < 60)\n", maxError);
    std::printf ("  Aucune note sur le silence/le bruit : %s\n", silenceClean ? "oui" : "NON");
    std::printf ("  Notes distinctes        : %d (attendu >= 4)\n", distinctNotes);
    std::printf ("  Correction dure (do majeur, note faussée) : %s\n",
                 tuneOk ? "conforme" : "NON");
    std::printf ("  Latence déclarée        : %.1f ms\n",
                 1000.0 * processor.getLatencySamples() / kSampleRate);

    const bool allOk = steadyOk && melodyOk && silenceClean && tuneOk;
    std::printf ("\n  RÉSULTAT : %s\n\n", allOk ? "SUCCÈS" : "ÉCHEC");

    return allOk ? 0 : 1;
}
