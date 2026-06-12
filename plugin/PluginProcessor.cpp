#include "PluginProcessor.h"

TouchstoneProcessor::TouchstoneProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", tsp::createParameterLayout())
{
    pMode       = apvts.getRawParameterValue (tsp::id::mode);
    pThreshold  = apvts.getRawParameterValue (tsp::id::threshold);
    pRatio      = apvts.getRawParameterValue (tsp::id::ratio);
    pAttack     = apvts.getRawParameterValue (tsp::id::attack);
    pRelease    = apvts.getRawParameterValue (tsp::id::release);
    pKnee       = apvts.getRawParameterValue (tsp::id::knee);
    pDrive      = apvts.getRawParameterValue (tsp::id::drive);
    pMakeup     = apvts.getRawParameterValue (tsp::id::makeup);
    pAutoMakeup = apvts.getRawParameterValue (tsp::id::automakeup);
    pMix        = apvts.getRawParameterValue (tsp::id::mix);
    pSchpf      = apvts.getRawParameterValue (tsp::id::schpf);
    pHq         = apvts.getRawParameterValue (tsp::id::hq);
    pFlaws      = apvts.getRawParameterValue (tsp::id::analogflaws);
    pIntimacy   = apvts.getRawParameterValue (tsp::id::intimacy);
    pMic        = apvts.getRawParameterValue (tsp::id::mic);
    pLearn      = apvts.getRawParameterValue (tsp::id::learn);

    apvts.addParameterListener (tsp::id::hq, this);
}

TouchstoneProcessor::~TouchstoneProcessor()
{
    apvts.removeParameterListener (tsp::id::hq, this);
    cancelPendingUpdate();
}

bool TouchstoneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

int TouchstoneProcessor::currentLatencySamples() const
{
    return engine.latencySamples (pHq != nullptr && pHq->load() > 0.5f);
}

void TouchstoneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, std::max (1, samplesPerBlock), 2);
    setLatencySamples (currentLatencySamples());
}

void TouchstoneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const int numCh = std::min (2, getTotalNumInputChannels());
    const int n     = buffer.getNumSamples();
    if (numCh <= 0 || n <= 0)
        return;

    refcomp::Parameters p;
    p.mode        = static_cast<refcomp::Mode> (int (pMode->load() + 0.5f));
    p.thresholdDb = pThreshold->load();
    p.ratio       = pRatio->load();
    p.attackMs    = pAttack->load();
    p.releaseMs   = pRelease->load();
    p.kneeDb      = pKnee->load();
    p.driveDb     = pDrive->load();
    p.makeupDb    = pMakeup->load();
    p.autoMakeup  = pAutoMakeup->load() > 0.5f;
    p.mix         = pMix->load() * 0.01f;
    p.schpfHz     = pSchpf->load();
    p.hq          = pHq->load() > 0.5f;
    p.flaws       = pFlaws->load() > 0.5f;
    p.intimacy    = pIntimacy->load() * 0.01f;
    p.mic         = int (pMic->load() + 0.5f);
    p.learnHold   = pLearn->load() > 0.5f;

    engine.setParameters (p);
    engine.process (buffer.getArrayOfWritePointers(), numCh, n);
}

juce::AudioProcessorEditor* TouchstoneProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void TouchstoneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
    {
        // The learned Voice profile travels with the session so the mode
        // behaves identically on every playback pass and every reopen.
        const auto vp = engine.getVoiceProfile();
        if (vp.valid)
        {
            xml->setAttribute ("voiceERef", vp.eRef);
            xml->setAttribute ("voicePRef", vp.pRef);
        }
        copyXmlToBinary (*xml, destData);
    }
}

void TouchstoneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            if (xml->hasAttribute ("voiceERef"))
            {
                refcomp::Engine<float, refcomp::FastMath>::VoiceProfile vp;
                vp.eRef  = xml->getDoubleAttribute ("voiceERef");
                vp.pRef  = xml->getDoubleAttribute ("voicePRef");
                vp.valid = true;
                engine.setVoiceProfile (vp);
            }
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
        }
}

void TouchstoneProcessor::parameterChanged (const juce::String& parameterID, float)
{
    if (parameterID == tsp::id::hq)
        triggerAsyncUpdate(); // setLatencySamples must come from the message thread
}

void TouchstoneProcessor::handleAsyncUpdate()
{
    setLatencySamples (currentLatencySamples());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TouchstoneProcessor();
}
