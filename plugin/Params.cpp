#include "Params.h"

namespace tsp
{

namespace
{
    using FloatParam  = juce::AudioParameterFloat;
    using ChoiceParam = juce::AudioParameterChoice;
    using BoolParam   = juce::AudioParameterBool;

    juce::NormalisableRange<float> logRange (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    juce::String dbText (float v, int)      { return juce::String (v, 1) + " dB"; }
    juce::String msText (float v, int)      { return v < 1.0f ? juce::String (v * 1000.0f, 0) + " us"
                                                              : juce::String (v, v < 10.0f ? 2 : 1) + " ms"; }
    juce::String ratioText (float v, int)   { return juce::String (v, 1) + ":1"; }
    juce::String pctText (float v, int)     { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String hpfText (float v, int)     { return v <= 20.5f ? juce::String ("Off")
                                                                : juce::String (juce::roundToInt (v)) + " Hz"; }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<ChoiceParam> (
        juce::ParameterID { id::mode, 1 }, "Mode",
        juce::StringArray { "Clean", "FET", "Opto", "Vari-Mu", "VCA", "Voice" }, 0));

    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { id::threshold, 1 }, "Threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f, 0.0f), -18.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));

    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { id::ratio, 1 }, "Ratio",
        logRange (1.0f, 20.0f, 4.0f), 4.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (ratioText)));

    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { id::attack, 1 }, "Attack",
        logRange (0.05f, 250.0f, 10.0f), 10.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (msText)));

    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { id::release, 1 }, "Release",
        logRange (5.0f, 2500.0f, 150.0f), 150.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (msText)));

    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { id::knee, 1 }, "Knee",
        juce::NormalisableRange<float> (0.0f, 24.0f, 0.0f), 6.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));

    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { id::drive, 1 }, "Drive",
        juce::NormalisableRange<float> (-12.0f, 24.0f, 0.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));

    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { id::makeup, 1 }, "Makeup",
        juce::NormalisableRange<float> (-12.0f, 24.0f, 0.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { id::automakeup, 1 }, "Auto Makeup", false));

    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { id::mix, 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));

    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { id::schpf, 1 }, "Detector HPF",
        logRange (20.0f, 500.0f, 100.0f), 20.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (hpfText)));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { id::hq, 1 }, "HQ (2x)", false));

    layout.add (std::make_unique<BoolParam> (
        juce::ParameterID { id::analogflaws, 1 }, "Analog Character", false));

    layout.add (std::make_unique<FloatParam> (
        juce::ParameterID { id::intimacy, 1 }, "Intimacy",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));

    layout.add (std::make_unique<ChoiceParam> (
        juce::ParameterID { id::mic, 1 }, "Mic Profile",
        juce::StringArray { "Dynamic (close)", "Condenser (close)", "Condenser (far)" }, 0));

    layout.add (std::make_unique<ChoiceParam> (
        juce::ParameterID { id::learn, 1 }, "Voice Profile",
        juce::StringArray { "Learn", "Hold" }, 0));

    return layout;
}

} // namespace tsp
