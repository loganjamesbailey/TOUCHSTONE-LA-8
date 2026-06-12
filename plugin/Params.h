#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace tsp
{
namespace id
{
    inline constexpr const char* mode        = "mode";
    inline constexpr const char* threshold   = "threshold";
    inline constexpr const char* ratio       = "ratio";
    inline constexpr const char* attack      = "attack";
    inline constexpr const char* release     = "release";
    inline constexpr const char* knee        = "knee";
    inline constexpr const char* drive       = "drive";
    inline constexpr const char* makeup      = "makeup";
    inline constexpr const char* automakeup  = "automakeup";
    inline constexpr const char* mix         = "mix";
    inline constexpr const char* schpf       = "schpf";
    inline constexpr const char* hq          = "hq";
    inline constexpr const char* analogflaws = "analogflaws";
    inline constexpr const char* intimacy    = "intimacy";
    inline constexpr const char* mic         = "mic";
    inline constexpr const char* learn       = "learn";
} // namespace id

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace tsp
