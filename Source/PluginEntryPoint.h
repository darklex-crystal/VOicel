#pragma once

#include <JuceHeader.h>

// Point d'entrée commun aux deux versions du plug-in. Le code de la version
// instrument est activé par la définition VM_INSTRUMENT_BUILD.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
