#pragma once

#include <JuceHeader.h>

namespace vm
{

/** Journal de diagnostic : ecrit dans %TEMP%\voicel-log.txt, ligne par ligne,
    et referme le fichier a chaque ecriture (les lignes survivent a un gel).
    Sert a trouver a quelle etape le programme se fige. */
inline void dbg (const juce::String& text)
{
    static juce::CriticalSection lock;
    static bool first = true;

    const juce::ScopedLock sl (lock);

    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("voicel-log.txt");

    if (first)
    {
        first = false;
        file.deleteFile();
    }

    file.appendText (juce::String (juce::Time::getMillisecondCounter()) + " ms [fil "
                     + juce::String::toHexString ((juce::pointer_sized_int) juce::Thread::getCurrentThreadId())
                     + "] " + text + "\r\n");
}

} // namespace vm
