#include "PluginEntryPoint.h"

#if defined(VM_INSTRUMENT_BUILD)
 #include "VoiceInstrumentProcessor.h"
  #define VM_PLUGIN_CLASS vm::VoiceInstrumentProcessor
#else
 #include "VoiceToMidiProcessor.h"
  #define VM_PLUGIN_CLASS vm::VoiceToMidiProcessor
#endif

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
   #if defined(VM_INSTRUMENT_BUILD)
    return new vm::VoiceInstrumentProcessor();
   #else
    return new vm::VoiceToMidiProcessor();
   #endif
}
