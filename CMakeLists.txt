cmake_minimum_required(VERSION 3.22)

project(VoicelMIDI VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# MSVC : runtime C++ lie statiquement, pour que le plug-in se charge sur un
# Windows sans "Visual C++ Redistributable". Doit preceder add_subdirectory.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_subdirectory(JUCE)

# ---------------------------------------------------------------------------
#  Sources communes aux deux versions
# ---------------------------------------------------------------------------
set(VM_COMMON_SOURCES
    Source/ParamIds.h
    Source/SincResampler.h          Source/SincResampler.cpp
    Source/YinPitchDetector.h       Source/YinPitchDetector.cpp
    Source/NoteTracker.h            Source/NoteTracker.cpp
    Source/MonoSynth.h              Source/MonoSynth.cpp
    Source/VoiceEngine.h            Source/VoiceEngine.cpp
    Source/MicCapture.h             Source/MicCapture.cpp
    Source/VoiceToMidiEditor.h      Source/VoiceToMidiEditor.cpp
    Source/PluginEntryPoint.cpp
)

set(VM_COMMON_DEFINES
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

# Options propres au compilateur (les options GCC n'existent pas sous MSVC).
if(MSVC)
    set(VM_COMPILE_OPTIONS /utf-8)
else()
    set(VM_COMPILE_OPTIONS
        -finput-charset=UTF-8
        -Wno-sign-conversion -Wno-float-equal -Wno-overloaded-virtual)
endif()

set(VM_COMMON_LIBS
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_audio_devices
)

# En compilation croisée, l'outil de manifeste VST3 de JUCE ne peut pas être
# exécuté (il tourne sur la machine hôte) : on désactive cette étape. VST3
# charge le plug-in en ouvrant directement la DLL, le manifeste n'est pas requis.
if(CMAKE_CROSSCOMPILING)
    set(VM_VST3_AUTO_MANIFEST FALSE)
else()
    set(VM_VST3_AUTO_MANIFEST TRUE)
endif()

# ---------------------------------------------------------------------------
#  1) "Voicel MIDI" - effet MIDI (micro du routage audio -> MIDI)
#     A charger dans les DAW qui acceptent les effets MIDI
#     (Cubase, Logic, Reaper, Studio One, Bitwig, FL Studio, Renoise…).
#     Ableton Live n'a pas de emplacement pour cette catégorie.
# ---------------------------------------------------------------------------
juce_add_plugin(VoicelMIDI
    COMPANY_NAME                "Voicel"
    PLUGIN_MANUFACTURER_CODE    VCML
    PLUGIN_CODE                 VMID
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "Voicel MIDI"
    DESCRIPTION                 "Convertit la voix en notes MIDI en temps reel"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_AUTO_MANIFEST          ${VM_VST3_AUTO_MANIFEST}
    VST3_CATEGORIES             "MIDI" "Instrument" "Tools"
)

target_sources(VoicelMIDI PRIVATE
    ${VM_COMMON_SOURCES}
    Source/VoiceToMidiProcessor.h
    Source/VoiceToMidiProcessor.cpp
)

target_compile_options(VoicelMIDI PRIVATE ${VM_COMPILE_OPTIONS})

target_compile_definitions(VoicelMIDI PUBLIC ${VM_COMMON_DEFINES})

juce_generate_juce_header(VoicelMIDI)

target_link_libraries(VoicelMIDI
    PRIVATE ${VM_COMMON_LIBS}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
#  2) "Voicel Voice" - instrument VST3 (ouvre le micro lui-même)
#     C'est la version à utiliser dans Ableton Live.
# ---------------------------------------------------------------------------
juce_add_plugin(VoicelVoice
    COMPANY_NAME                "Voicel"
    PLUGIN_MANUFACTURER_CODE    VCML
    PLUGIN_CODE                 VMVC
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "Voicel Voice"
    DESCRIPTION                 "Instrument : la voix pilote les notes en temps reel"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_AUTO_MANIFEST          ${VM_VST3_AUTO_MANIFEST}
    VST3_CATEGORIES             "Instrument" "MIDI" "Tools"
)

target_sources(VoicelVoice PRIVATE
    ${VM_COMMON_SOURCES}
    Source/VoiceInstrumentProcessor.h
    Source/VoiceInstrumentProcessor.cpp
)

target_compile_definitions(VoicelVoice PRIVATE VM_INSTRUMENT_BUILD=1)

target_compile_options(VoicelVoice PRIVATE ${VM_COMPILE_OPTIONS})

target_compile_definitions(VoicelVoice PUBLIC ${VM_COMMON_DEFINES})

juce_generate_juce_header(VoicelVoice)

target_link_libraries(VoicelVoice
    PRIVATE ${VM_COMMON_LIBS}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
#  Windows : le runtime MinGW (libstdc++, libgcc, winpthread) n'existe pas sur
#  un poste Windows vierge. On le lie statiquement, sinon VST3 refuse de
#  charger le plug-in faute de DLL.
# ---------------------------------------------------------------------------
if(MINGW)
    foreach(vm_target VoicelMIDI VoicelVoice)
        # -static lie statiquement libstdc++, libgcc et winpthread.
        target_link_options(${vm_target} INTERFACE -static)
    endforeach()
endif()

# ---------------------------------------------------------------------------
#  Test hors-ligne du DSP (console)
# ---------------------------------------------------------------------------
option(BUILD_DSP_TEST "Compile le test hors-ligne" OFF)

if(BUILD_DSP_TEST)
    juce_add_console_app(VoicelDspTest PRODUCT_NAME "VoicelDspTest")

    target_sources(VoicelDspTest PRIVATE
        Tools/DspTest.cpp
        Source/ParamIds.h
        Source/SincResampler.h            Source/SincResampler.cpp
        Source/YinPitchDetector.h         Source/YinPitchDetector.cpp
        Source/NoteTracker.h              Source/NoteTracker.cpp
        Source/MonoSynth.h                Source/MonoSynth.cpp
        Source/VoiceEngine.h              Source/VoiceEngine.cpp
        Source/MicCapture.h               Source/MicCapture.cpp
        Source/VoiceToMidiProcessor.h     Source/VoiceToMidiProcessor.cpp
        Source/VoiceToMidiEditor.h        Source/VoiceToMidiEditor.cpp
    )

    target_compile_definitions(VoicelDspTest PUBLIC ${VM_COMMON_DEFINES})
    target_compile_options(VoicelDspTest PRIVATE ${VM_COMPILE_OPTIONS})

    juce_generate_juce_header(VoicelDspTest)

    target_link_libraries(VoicelDspTest
        PRIVATE ${VM_COMMON_LIBS}
        PUBLIC
            juce::juce_recommended_config_flags
    )

    # ---- Test hors-ligne de la version instrument (avec micro simulé)
    juce_add_console_app(VoicelInstrumentTest PRODUCT_NAME "VoicelInstrumentTest")

    target_sources(VoicelInstrumentTest PRIVATE
        Tools/InstrumentTest.cpp
        Source/ParamIds.h
        Source/SincResampler.h            Source/SincResampler.cpp
        Source/YinPitchDetector.h         Source/YinPitchDetector.cpp
        Source/NoteTracker.h              Source/NoteTracker.cpp
        Source/MonoSynth.h                Source/MonoSynth.cpp
        Source/VoiceEngine.h              Source/VoiceEngine.cpp
        Source/MicCapture.h               Source/MicCapture.cpp
        Source/VoiceToMidiEditor.h        Source/VoiceToMidiEditor.cpp
        Source/VoiceInstrumentProcessor.h Source/VoiceInstrumentProcessor.cpp
    )

    target_compile_definitions(VoicelInstrumentTest PUBLIC ${VM_COMMON_DEFINES} VM_TESTING=1)
    target_compile_options(VoicelInstrumentTest PRIVATE ${VM_COMPILE_OPTIONS})

    juce_generate_juce_header(VoicelInstrumentTest)

    target_link_libraries(VoicelInstrumentTest
        PRIVATE ${VM_COMMON_LIBS}
        PUBLIC
            juce::juce_recommended_config_flags
    )
endif()
