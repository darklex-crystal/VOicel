#pragma once

//==============================================================================
// Identifiants de paramètres partagés entre le processeur et l'interface.
//==============================================================================
namespace vm
{
namespace pid
{
    inline constexpr const char* inputGain    = "input_gain";
    inline constexpr const char* monitorGain  = "monitor_gain";
    inline constexpr const char* synthGain    = "synth_gain";
    inline constexpr const char* gateDb       = "gate_db";
    inline constexpr const char* fMin         = "f_min";
    inline constexpr const char* fMax         = "f_max";
    inline constexpr const char* sensitivity  = "sensitivity";
    inline constexpr const char* latencyMode  = "latency_mode";
    inline constexpr const char* holdMs       = "hold_ms";
    inline constexpr const char* noteMin      = "note_min";
    inline constexpr const char* noteMax      = "note_max";
    inline constexpr const char* bendRange    = "bend_range";
    inline constexpr const char* glideMs      = "glide_ms";
    inline constexpr const char* octaveShift  = "octave_shift";
    inline constexpr const char* scale        = "scale";
    inline constexpr const char* root         = "root";
    inline constexpr const char* tuneAmount   = "tune_amount";
    inline constexpr const char* retuneSpeed  = "retune_speed";
    inline constexpr const char* tuneLock     = "tune_lock";
    inline constexpr const char* velocityMode = "velocity_mode";
    inline constexpr const char* velocityMax  = "velocity_max";
    inline constexpr const char* exprCC       = "expr_cc";
    inline constexpr const char* midiChannel  = "midi_channel";
}
}
