#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <refcomp/Engine.h>
#include "Params.h"

class TouchstoneProcessor : public juce::AudioProcessor,
                            private juce::AudioProcessorValueTreeState::Listener,
                            private juce::AsyncUpdater
{
public:
    TouchstoneProcessor();
    ~TouchstoneProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override            { return "Touchstone"; }
    bool acceptsMidi() const override                      { return false; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 0.0; }

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override       { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    int currentLatencySamples() const;

    // Shipping engine: float audio path, double control path, polynomial
    // transcendentals — null-tested against the double scalar reference.
    refcomp::Engine<float, refcomp::FastMath> engine;

    // Cached raw parameter atomics (owned by the APVTS).
    std::atomic<float>* pMode {};
    std::atomic<float>* pThreshold {};
    std::atomic<float>* pRatio {};
    std::atomic<float>* pAttack {};
    std::atomic<float>* pRelease {};
    std::atomic<float>* pKnee {};
    std::atomic<float>* pDrive {};
    std::atomic<float>* pMakeup {};
    std::atomic<float>* pAutoMakeup {};
    std::atomic<float>* pMix {};
    std::atomic<float>* pSchpf {};
    std::atomic<float>* pHq {};
    std::atomic<float>* pFlaws {};
    std::atomic<float>* pIntimacy {};
    std::atomic<float>* pMic {};
    std::atomic<float>* pLearn {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TouchstoneProcessor)
};
