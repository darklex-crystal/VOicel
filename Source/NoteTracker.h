#pragma once

#include <deque>
#include <juce_audio_processors/juce_audio_processors.h>

#include "YinPitchDetector.h"

namespace vm
{

//==============================================================================
/** Convertit un flux continu de hauteur vocale en événements MIDI (monophonique).

    Chaîne de traitement par trame :
      1. correction d'octave (filtre médian + accrochage d'octave) ;
      2. conversion Hz -> MIDI flottant, puis transcription dans la portée ;
      3. éventuelle quantification à une gamme musicale ;
      4. correction de justesse (autotune) vers le degré de gamme le plus proche ;
      5. sélection de la note + déviation (pitch bend) allowable ;
      6. note-on / note-off avec portamento, verrouillage de note et maintien
         anti-vibration.
*/
class NoteTracker
{
public:
    struct Settings
    {
        int   noteMin            = 36;
        int   noteMax            = 84;
        int   bendRangeSemitones = 2;
        float glideSeconds       = 0.045f;
        float holdSeconds        = 0.060f;
        int   octaveShift        = 0;
        float tuneAmount         = 0.0f;   // 0 = brut, 1 = hauteur plaquee
        float retuneSeconds      = 0.020f; // vitesse de correction
        float lockCents          = 40.0f;  // hysteresis de changement de note
        int   scale              = 0;      // index dans ScaleNames
        int   root               = 0;
        int   channel            = 1;
        bool  dynamicVelocity    = true;
        int   velocityMax        = 100;
        int   expressionCC       = -1;     // -1 = désactivé
        float gateDb             = -46.0f;
    };

    void setSettings (const Settings& s) { settings_ = s; }
    void reset (int channel);
    void panic (juce::MidiBuffer& midi, int sampleOffset = 0);

    void processFrame (const PitchFrame& frame,
                       int sampleOffset,
                       juce::MidiBuffer& midi,
                       float hopSeconds);

    // ---- Accès pour l'interface -------------------------------------------
    int   getActiveNote()   const noexcept { return activeNote_; }
    float getBendSemitones()const noexcept { return bendSemitones_; }
    float getDisplayMidi()  const noexcept { return displayMidi_; }
    float getExpression()   const noexcept { return expression_; }
    bool  hasRecentPitch()  const noexcept { return hasPitch_; }

private:
    float octaveCorrect (float f0);
    int   snapToScale (int note) const;
    void  sendNoteOff (juce::MidiBuffer& midi, int offset);
    void  sendNoteOn  (juce::MidiBuffer& midi, int offset, int velocity);
    void  sendBend    (juce::MidiBuffer& midi, int offset);
    float loudnessToUnit (float rms) const;
    float velocityTarget() const;

    Settings settings_;

    int   activeNote_     = -1;
    float bendSemitones_  = 0.0f;
    float bendTarget_     = 0.0f;
    int   lastBendSent_   = -1;
    int   candidateNote_  = -1;
    int   candidateCount_ = 0;
    float displayMidi_    = 60.0f;
    float correctedPitch_ = 60.0f;
    bool  hasCorrected_   = false;
    float prevMidiFloat_  = 60.0f;
    float unvoicedTime_   = 1.0f;
    float velocitySmooth_ = 80.0f;
    float expression_     = 0.0f;
    float loudness_       = 0.0f;
    int   exprCounter_    = 0;
    bool  hasPitch_       = false;

    std::deque<float> f0History_;
};

} // namespace vm
