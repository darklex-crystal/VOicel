#include "PluginEntryPoint.h"
#include "DebugLog.h"

#if defined(VM_INSTRUMENT_BUILD)
 #include "VoiceInstrumentProcessor.h"
  #define VM_PLUGIN_CLASS vm::VoiceInstrumentProcessor
#else
 #include "VoiceToMidiProcessor.h"
  #define VM_PLUGIN_CLASS vm::VoiceToMidiProcessor
#endif

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    vm::dbg ("createPluginFilter");
   #if defined(VM_INSTRUMENT_BUILD)
    return new vm::VoiceInstrumentProcessor();
   #else
    return new vm::VoiceToMidiProcessor();
   #endif
}
