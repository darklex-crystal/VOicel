#include "MicCapture.h"

#include <cstdio>

namespace vm
{

namespace
{
    struct InputEntry { juce::AudioIODeviceType* type; juce::String name; };

    /** Liste toutes les ENTREES (getDeviceNames (true)) de tous les types de
        périphérique, sauf ASIO.

        - L'ancienne version appelait getDeviceNames (false), qui renvoie les
          SORTIES : le « micro » ouvert était en réalité un périphérique de
          sortie.
        - ASIO est exclu : sonder les pilotes ASIO depuis l'intérieur d'un
          plug-in charge/décharge les DLL des pilotes et peut bloquer ou faire
          planter l'hôte (Ableton, par exemple, tient déjà le pilote). */
    std::vector<InputEntry> listInputs (juce::AudioDeviceManager& manager)
    {
        std::vector<InputEntry> entries;

        for (auto* type : manager.getAvailableDeviceTypes())
        {
            if (type->getTypeName().containsIgnoreCase ("ASIO"))
                continue;

            type->scanForDevices();

            for (const auto& name : type->getDeviceNames (true))
                entries.push_back ({ type, name });
        }

        return entries;
    }
}

//==============================================================================
void MicCapture::prepare (double hostSampleRate, int /*maximumExpectedSamplesPerBlock*/)
{
    // Aucune allocation ici : l'anneau est de taille fixe et le micro peut
    // être en train d'écrire dedans.
    hostRate_.store (hostSampleRate > 0.0 ? hostSampleRate : 48000.0);
    resync_.store (true);
}

void MicCapture::shutdown()
{
    closeInput();
    manager_.closeAudioDevice();
}

juce::StringArray MicCapture::getInputNames()
{
    juce::StringArray names;

    for (const auto& entry : listInputs (manager_))
    {
        const auto full = entry.type->getTypeName() + " - " + entry.name;

        if (! names.contains (full))
            names.add (full);
    }

    return names;
}

bool MicCapture::openInput (int index)
{
    closeInput();
    error_ = {};

    const auto entries = listInputs (manager_);

    if (index < 0 || index >= static_cast<int> (entries.size()))
    {
        error_ = "Peripherique introuvable.";
        return false;
    }

    const auto chosenType   = entries[static_cast<size_t> (index)].type->getTypeName();
    const auto chosenDevice = entries[static_cast<size_t> (index)].name;

    manager_.setCurrentAudioDeviceType (chosenType, true);

    auto setup = manager_.getAudioDeviceSetup();
    setup.inputDeviceName          = chosenDevice;
    setup.outputDeviceName         = {};
    setup.useDefaultInputChannels  = true;
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.clear();
    setup.sampleRate  = hostRate_.load();
    setup.bufferSize  = 256;

    error_ = manager_.setAudioDeviceSetup (setup, true);

    if (! error_.isEmpty())
    {
        manager_.closeAudioDevice();
        return false;
    }

    if (manager_.getCurrentAudioDevice() == nullptr)
    {
        error_ = "Impossible d'ouvrir le peripherique.";
        return false;
    }

    manager_.addAudioCallback (this);
    opened_ = true;
    return true;
}

void MicCapture::closeInput()
{
    if (opened_)
    {
        manager_.removeAudioCallback (this);
        manager_.closeAudioDevice();
    }

    opened_ = false;
    micRate_.store (0.0);
    resync_.store (true);
    ring_.clear();
}

juce::String MicCapture::getCurrentInputName() const
{
    if (auto* device = manager_.getCurrentAudioDevice())
        return device->getName();

    return "Aucun";
}

void MicCapture::fill (float* dest, int numSamples)
{
    if (numSamples <= 0)
        return;

    // micRate_ est mis à jour par le micro lui-même : tant qu'aucun
    // périphérique n'a démarré, on reste silencieux.
    const double micRate = micRate_.load();

    if (micRate <= 0.0)
    {
        std::fill (dest, dest + numSamples, 0.0f);
        return;
    }

    // Démarrage / changement de périphérique : phase_ n'est touchée que par
    // ce fil, d'où le drapeau plutôt qu'une remise à zéro depuis l'extérieur.
    if (resync_.exchange (false))
    {
        phase_ = 0.0;
        ring_.clear();
    }

    const double ratio = micRate / hostRate_.load();

    float peak = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        const int64_t index  = static_cast<int64_t> (phase_);
        const float   frac   = static_cast<float> (phase_ - static_cast<double> (index));

        const float a = ring_.sampleAt (index);
        const float b = ring_.sampleAt (index + 1);
        const float s = a + (b - a) * frac;

        dest[n] = s;
        peak = std::max (peak, std::abs (s));
        phase_ += ratio;
    }

    ring_.advance (static_cast<int64_t> (phase_));
    phase_ -= static_cast<double> (static_cast<int64_t> (phase_));

    // Niveau lissé pour la jauge
    level_.store (std::max (peak, level_.load (std::memory_order_relaxed) * 0.85f),
                  std::memory_order_relaxed);
}

void MicCapture::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // Pas de réallocation : voir FloatRing.
    ring_.clear();
    resync_.store (true);
    micRate_.store (device->getCurrentSampleRate());
}

void MicCapture::audioDeviceStopped()
{
    micRate_.store (0.0);
    ring_.clear();
}

void MicCapture::audioDeviceIOCallbackWithContext (const float* const* inputData,
                                                   int numInputChannels,
                                                   float* const* outputData,
                                                   int numOutputChannels,
                                                   int numSamples,
                                                   const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused (outputData, numOutputChannels, context);

    if (numInputChannels <= 0 || inputData == nullptr || inputData[0] == nullptr)
        return;

    if (numInputChannels == 1)
    {
        ring_.write (inputData[0], numSamples);
        return;
    }

    if (mixBuffer_.size() < static_cast<size_t> (numSamples))
        mixBuffer_.resize (static_cast<size_t> (numSamples));

    for (int n = 0; n < numSamples; ++n)
    {
        float s = 0.0f;
        int used = 0;

        for (int c = 0; c < numInputChannels; ++c)
            if (inputData[c] != nullptr)
            {
                s += inputData[c][n];
                ++used;
            }

        mixBuffer_[static_cast<size_t> (n)] = used > 0 ? s / static_cast<float> (used) : 0.0f;
    }

    ring_.write (mixBuffer_.data(), numSamples);
}

//==============================================================================
MicPanel::MicPanel (MicCapture& capture, VoiceEngine& engine)
    : capture_ (capture), engine_ (engine)
{
    deviceCombo_.onChange = [this]
    {
        const int id = deviceCombo_.getSelectedId();
        currentId_ = id;
        capture_.openInput (id - 1);
        updateStatus();
    };

    refreshButton_.onClick = [this] { refreshDevices(); };

    statusLabel_.setJustificationType (juce::Justification::centredLeft);
    statusLabel_.setText ("Recherche du micro...", juce::dontSendNotification);

    addAndMakeVisible (deviceCombo_);
    addAndMakeVisible (refreshButton_);
    addAndMakeVisible (statusLabel_);

    startTimerHz (12);

    // L'énumération des périphériques peut être lente (elle sonde ALSA, JACK,
    // PulseAudio...) : on ne la fait jamais pendant la création de l'éditeur,
    // sinon la fenêtre resterait figée au démarrage.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MicPanel> (this)]
    {
        if (safe != nullptr)
            safe->refreshDevices();
    });
}

MicPanel::~MicPanel()
{
    stopTimer();
}

void MicPanel::refreshDevices()
{
    devices_ = capture_.getInputNames();
    deviceCombo_.clear (juce::dontSendNotification);

    if (devices_.isEmpty())
    {
        deviceCombo_.addItem ("Aucun peripherique d'entree", 1);
        deviceCombo_.setSelectedId (1, juce::dontSendNotification);
        setStatus ("Aucun peripherique d'entree detecte.");
        return;
    }

    for (int i = 0; i < devices_.size(); ++i)
        deviceCombo_.addItem (devices_[i], i + 1);

    deviceCombo_.setSelectedId (1, juce::dontSendNotification);
    currentId_ = 1;

    // Le processeur ouvre déjà l'entrée par défaut : on ne la ferme pas pour
    // la rouvrir (coupure audio inutile).
    if (! capture_.isOpen())
        capture_.openInput (0);

    updateStatus();
}

void MicPanel::setStatus (const juce::String& text)
{
    statusLabel_.setText (text, juce::dontSendNotification);
}

void MicPanel::updateStatus()
{
    const auto error = capture_.getError();

    if (! error.isEmpty())
        setStatus (error);
    else if (! capture_.isOpen())
        setStatus ("Micro ferme - selectionnez un peripherique.");
    else
        setStatus (capture_.getCurrentInputName());
}

void MicPanel::timerCallback()
{
    updateStatus();

    meter_ = juce::jlimit (0, 20, static_cast<int> (capture_.getLevel() * 22.0f));
    repaint();
}

void MicPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff151a21));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (juce::Colour (0xff242c36));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    // Jauge de niveau du micro
    const float barWidth = 6.0f;
    const float gap = 3.0f;
    const int   bars = 20;
    const float totalWidth = bars * (barWidth + gap) - gap;
    float x = bounds.getRight() - 12.0f - totalWidth;
    const float y = bounds.getCentreY() - 4.0f;

    for (int i = 0; i < bars; ++i)
    {
        g.setColour (i < meter_ ? juce::Colour (0xff35d6c0) : juce::Colour (0xff232a33));
        g.fillRoundedRectangle (x, y, barWidth, 8.0f, 2.0f);
        x += barWidth + gap;
    }

    g.setColour (juce::Colour (0xff7b8794));
    g.setFont (juce::Font (10.0f, juce::Font::bold));
    g.drawText ("ENTREE MICRO",
                getLocalBounds().reduced (12, 0).withHeight (16),
                juce::Justification::centredLeft, false);
}

void MicPanel::resized()
{
    auto bounds = getLocalBounds().reduced (12, 6).withTrimmedTop (22);

    const int row = juce::jmin (22, bounds.getHeight());
    bounds.setHeight (row);

    refreshButton_.setBounds (bounds.removeFromRight (88));
    bounds.removeFromRight (8);

    const int statusWidth = juce::jlimit (120, 320, bounds.getWidth() / 3);
    statusLabel_.setBounds (bounds.removeFromRight (statusWidth));
    bounds.removeFromRight (8);

    deviceCombo_.setBounds (bounds);
}

} // namespace vm
