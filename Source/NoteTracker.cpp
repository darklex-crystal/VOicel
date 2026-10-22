#include "NoteTracker.h"

#include <algorithm>
#include <cmath>

namespace vm
{

namespace
{
    // Gammes : tons (0..11) relatifs à la tonique.
    const std::vector<std::vector<int>>& scales()
    {
        static const std::vector<std::vector<int>> s
        {
            {},                              // 0 : libre (pas de quantification)
            {0,1,2,3,4,5,6,7,8,9,10,11},     // 1 : chromatique
            {0,2,4,5,7,9,11},                // 2 : majeur
            {0,2,3,5,7,8,10},                // 3 : mineur naturel
            {0,3,5,7,10},                    // 4 : pentatonique mineure
            {0,2,4,7,9},                     // 5 : pentatonique majeure
            {0,3,5,6,7,10},                  // 6 : blues
            {0,2,3,5,7,9,10},                // 7 : dorien
            {0,2,4,6,7,9,11},                // 8 : lydien
        };
        return s;
    }

    float medianOf (std::deque<float>& d)
    {
        if (d.empty()) return 0.0f;
        std::vector<float> tmp (d.begin(), d.end());
        std::nth_element (tmp.begin(), tmp.begin() + static_cast<int> (tmp.size() / 2), tmp.end());
        return tmp[tmp.size() / 2];
    }
}

void NoteTracker::reset (int channel)
{
    settings_.channel = channel;
    activeNote_      = -1;
    bendSemitones_   = 0.0f;
    bendTarget_      = 0.0f;
    lastBendSent_    = -1;
    unvoicedTime_    = 1.0f;
    candidateNote_   = -1;
    candidateCount_  = 0;
    velocitySmooth_  = 80.0f;
    expression_      = 0.0f;
    exprCounter_     = 0;
    hasPitch_        = false;
    hasCorrected_    = false;
    f0History_.clear();
    // displayMidi_ volontairement conservé : l'affichage ne doit pas sauter
}

void NoteTracker::panic (juce::MidiBuffer& midi, int sampleOffset)
{
    if (activeNote_ >= 0)
        sendNoteOff (midi, sampleOffset);

    if (settings_.expressionCC >= 0)
        midi.addEvent (juce::MidiMessage::controllerEvent (settings_.channel,
                                                           settings_.expressionCC, 0),
                       sampleOffset);

    activeNote_    = -1;
    lastBendSent_  = -1;
    bendSemitones_ = 0.0f;
    bendTarget_    = 0.0f;
    hasPitch_      = false;
    f0History_.clear();
}

float NoteTracker::velocityTarget() const
{
    const float target = 1.0f + loudness_ * static_cast<float> (settings_.velocityMax - 1);
    return std::clamp (target, 1.0f, 127.0f);
}

float NoteTracker::loudnessToUnit (float rms) const
{
    const float db = 20.0f * std::log10 (std::max (rms, 1e-7f));
    const float top = -1.0f;   // -1 dBFS = niveau de chant comfortable
    return std::clamp ((db - settings_.gateDb) / std::max (1.0f, top - settings_.gateDb),
                       0.0f, 1.0f);
}

//------------------------------------------------------------------------------
/** Filtre médian + rattrapage d'octave.

    Une erreur d'octave classique de YIN se présente comme un saut d'exactement
    un octave. On ne la corrige que si la valeur reste isolée (la médiane
    historique ne suit pas), ce qui laisse passer les vrais sauts d'octave
    chants, qui sont détectés quelques trames plus tard.
*/
float NoteTracker::octaveCorrect (float f0)
{
    if (f0History_.size() < 3)
    {
        f0History_.push_back (f0);
        return f0;
    }

    // Historique borné dans TOUS les cas (il grossissait sans limite quand
    // des corrections d'octave s'enchaînaient).
    while (f0History_.size() > 7)
        f0History_.pop_front();

    const float med  = medianOf (f0History_);
    const float oct  = std::log2 (f0 / std::max (med, 1e-3f));
    const float round_ = std::round (oct);

    if (round_ != 0.0f && std::abs (round_) <= 1.0f && std::abs (oct - round_) < 0.25f)
    {
        const float corrected = f0 / std::pow (2.0f, round_);
        const float err = 12.0f * std::log2 (corrected / std::max (med, 1e-3f));

        if (std::abs (err) < 1.0f)
        {
            f0History_.push_back (corrected);
            return corrected;
        }
    }

    f0History_.push_back (f0);
    while (f0History_.size() > 7)
        f0History_.pop_front();

    return f0;
}

int NoteTracker::snapToScale (int note) const
{
    const auto& table = scales();

    if (settings_.scale <= 0 || static_cast<size_t> (settings_.scale) >= table.size())
        return note;

    const auto& s     = table[static_cast<size_t> (settings_.scale)];
    const int    pc   = ((note - settings_.root) % 12 + 12) % 12;

    int    best      = pc;
    int    bestDist  = 100;

    for (int d : s)
    {
        const int dist = std::min (std::abs (d - pc), 12 - std::abs (d - pc));
        if (dist < bestDist)
        {
            bestDist = dist;
            best     = d;
        }
    }

    return note + (best - pc);
}

void NoteTracker::sendNoteOff (juce::MidiBuffer& midi, int offset)
{
    if (activeNote_ < 0)
        return;

    midi.addEvent (juce::MidiMessage::noteOff (settings_.channel, activeNote_,
                                               static_cast<juce::uint8> (0)),
                   offset);
    activeNote_ = -1;
}

void NoteTracker::sendNoteOn (juce::MidiBuffer& midi, int offset, int velocity)
{
    velocity = juce::jlimit (1, 127, velocity);
    midi.addEvent (juce::MidiMessage::noteOn (settings_.channel, activeNote_,
                                              static_cast<juce::uint8> (velocity)),
                   offset);
}

void NoteTracker::sendBend (juce::MidiBuffer& midi, int offset)
{
    const float allowed = static_cast<float> (settings_.bendRangeSemitones) * 0.5f;
    if (allowed <= 0.0f)
        return;

    const float normalised = std::clamp (bendSemitones_ / allowed, -1.0f, 1.0f);
    const int   units      = std::lround (normalised * 8192.0f);

    if (units != lastBendSent_)
    {
        lastBendSent_ = units;
        // pitchWheel attend une position 14 bits (0..16383, centre = 8192)
        const int position = juce::jlimit (0, 16383,
                                           static_cast<int> (std::lround ((normalised * 0.5f + 0.5f) * 16383.0f)));
        midi.addEvent (juce::MidiMessage::pitchWheel (settings_.channel, position), offset);
    }
}

//------------------------------------------------------------------------------
void NoteTracker::processFrame (const PitchFrame& frame,
                                int sampleOffset,
                                juce::MidiBuffer& midi,
                                float hopSeconds)
{
    sampleOffset = std::max (0, sampleOffset);
    hopSeconds   = std::max (0.0005f, hopSeconds);

    // --- Lissage du bend (constant de temps = glide / 3) -------------------
    const float tau   = std::max (0.0005f, settings_.glideSeconds / 3.0f);
    const float alpha = 1.0f - std::exp (-hopSeconds / tau);

    bool inRange = false;
    float midiFloat = 0.0f;
    loudness_ = loudnessToUnit (frame.rms);

    if (frame.voiced && frame.frequency > 0.0f)
    {
        const float f0 = octaveCorrect (frame.frequency);
        midiFloat = 69.0f + 12.0f * std::log2 (f0 / 440.0f)
                        + 12.0f * static_cast<float> (settings_.octaveShift);

        // Hauteur de la trame précédente, utilisée pour le point de départ du
        // glissando lors d'un changement de note.
        const float previousPitch = prevMidiFloat_;

        // --- Correction de justesse (autotune) ----------------------------
        // A 0 %, la hauteur chantee est conservee telle quelle. A 100 %, elle
        // est ramenee exactement sur le degre de gamme le plus proche : le
        // chant sort alors toujours dans la gamme, meme en faussant.
        const float amount = std::clamp (settings_.tuneAmount, 0.0f, 1.0f);

        if (amount > 0.0f)
        {
            const int   target = snapToScale (static_cast<int> (std::floor (midiFloat + 0.5f)));
            const float wanted = static_cast<float> (target)
                               + (midiFloat - static_cast<float> (target)) * (1.0f - amount);

            // Lissage : c'est lui qui donne la vitesse de correction. Une
            // valeur tres courte colle instantanement sur la note, une valeur
            // longue laisse la voix glisser jusqu'a elle.
            const float retuneTau = std::max (0.0005f, settings_.retuneSeconds);
            const float beta      = 1.0f - std::exp (-hopSeconds / retuneTau);

            if (! hasCorrected_)
            {
                correctedPitch_ = wanted;
                hasCorrected_   = true;
            }
            else
            {
                correctedPitch_ += (wanted - correctedPitch_) * beta;
            }

            midiFloat = correctedPitch_;
        }
        else
        {
            hasCorrected_ = false;
        }

        // --- Choix de la note ---------------------------------------------
        const float allowed = static_cast<float> (settings_.bendRangeSemitones) * 0.5f;

        const int nearest = static_cast<int> (std::floor (midiFloat + 0.5f));
        int       note    = snapToScale (nearest);

        // On ramene l'ecart dans la plage de bend admissible, pour que la
        // totalite du vibrato reste exprimable. Attention : avec une plage
        // nulle, les deux boucles se marcheraient dessus indefiniment ; dans
        // ce cas la note est simplement le demi-ton le plus proche, ce qui
        // revient a un collage dur de la hauteur.
        if (allowed > 0.0f)
        {
            if (midiFloat - static_cast<float> (note) >  allowed) note = nearest + 1;
            if (midiFloat - static_cast<float> (note) < -allowed) note = nearest - 1;
        }

        note = std::clamp (note, 0, 127);

        // --- Verrouillage de note (anti-warble) ----------------------------
        // Tant que la hauteur corrigee reste a portee de la note jouee, on la
        // garde. Sans cela, une note chantee entre deux degres fait osciller
        // le note-on / note-off (le "warble" caracteristique d'un autotune mal
        // regle). Le verrouillage n'agit que lorsqu'une gamme est selectionnee
        // ou qu'une correction est active, pour ne pas gener un chant libre.
        if (activeNote_ >= 0 && note != activeNote_)
        {
            const bool  tuning = settings_.scale > 0 || amount > 0.0f;
            const float lock   = tuning ? std::max (0.0f, settings_.lockCents) * 0.01f : 0.0f;

            if (lock > 0.0f
                && std::abs (note - activeNote_) <= 2
                && std::abs (midiFloat - static_cast<float> (activeNote_)) <= lock)
            {
                note = activeNote_;
            }
        }

        const bool playable = (note >= settings_.noteMin && note <= settings_.noteMax);
        inRange = playable;

        if (playable)
        {
            unvoicedTime_ = 0.0f;
            hasPitch_     = true;
            displayMidi_  = midiFloat;

            // Une note nouvelle doit être confirmée sur deux trames
            // consécutives : cela supprime les brèves hallucinations de
            // hauteur en fin de note, sans pénaliser la latence de jeu.
            if (activeNote_ >= 0 && note != activeNote_)
            {
                if (candidateNote_ == note)
                    ++candidateCount_;
                else
                {
                    candidateNote_  = note;
                    candidateCount_ = 1;
                }

                if (candidateCount_ < 2)
                {
                    bendTarget_ = (allowed > 0.0f)
                                    ? std::clamp (midiFloat - static_cast<float> (activeNote_),
                                                  -allowed, allowed)
                                    : 0.0f;
                    inRange = true;
                    return;
                }

                candidateNote_  = -1;
                candidateCount_ = 0;
            }
            else
            {
                candidateNote_  = -1;
                candidateCount_ = 0;
            }

            if (activeNote_ >= 0 && note != activeNote_)
            {
                // Changement de note : le bend repart de la hauteur réellement
                // chantée à la trame précédente, puis glisse jusqu'à la note.
                const float startBend = (allowed > 0.0f)
                                            ? std::clamp (previousPitch - static_cast<float> (note),
                                                          -allowed, allowed)
                                            : 0.0f;

                sendNoteOff (midi, sampleOffset);
                activeNote_ = note;
                bendSemitones_ = startBend;
                lastBendSent_  = -1;

                int vel = settings_.velocityMax;
                if (settings_.dynamicVelocity)
                    vel = std::lround (velocitySmooth_);

                sendNoteOn (midi, sampleOffset, vel);
            }
            else if (activeNote_ < 0)
            {
                activeNote_ = note;
                bendSemitones_ = 0.0f;
                bendTarget_    = 0.0f;

                // À l'attaque, on part directement du niveau courant : la
                // note doit sonner à sa vraie force dès le début.
                velocitySmooth_ = juce::jmax (velocityTarget(),
                                              0.3f * static_cast<float> (settings_.velocityMax));

                int vel = settings_.velocityMax;
                if (settings_.dynamicVelocity)
                    vel = std::lround (velocitySmooth_);

                sendNoteOn (midi, sampleOffset, vel);
            }

            bendTarget_ = (allowed > 0.0f) ? (midiFloat - static_cast<float> (activeNote_)) : 0.0f;
        }
    }

    if (! inRange)
    {
        unvoicedTime_ += hopSeconds;
        bendTarget_    = 0.0f;

        if (activeNote_ >= 0 && unvoicedTime_ >= settings_.holdSeconds)
        {
            sendNoteOff (midi, sampleOffset);
            hasPitch_ = false;
        }
    }

    // --- Vélocité dynamique ------------------------------------------------
    if (settings_.dynamicVelocity)
    {
        velocitySmooth_ += (velocityTarget() - velocitySmooth_) * alpha;
        velocitySmooth_  = std::clamp (velocitySmooth_, 1.0f, 127.0f);
    }

    // --- Expression (CC) ---------------------------------------------------
    expression_ += (loudness_ - expression_) * (1.0f - std::exp (-hopSeconds / 0.03f));

    if (settings_.expressionCC >= 0 && ++exprCounter_ >= 3)
    {
        exprCounter_ = 0;
        const int value = std::lround (std::clamp (expression_, 0.0f, 1.0f) * 127.0f);
        midi.addEvent (juce::MidiMessage::controllerEvent (settings_.channel,
                                                           settings_.expressionCC, value),
                       sampleOffset);
    }

    // --- Envoi du pitch bend ----------------------------------------------
    bendSemitones_ += (bendTarget_ - bendSemitones_) * alpha;

    if (activeNote_ >= 0)
        sendBend (midi, sampleOffset);

    prevMidiFloat_ = inRange ? midiFloat : prevMidiFloat_;
}

} // namespace vm
