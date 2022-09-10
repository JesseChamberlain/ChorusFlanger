/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
ChorusFlangerAudioProcessor::ChorusFlangerAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
    addParameter(mDryWetParameter = new juce::AudioParameterFloat({ "drywet", 1 },
                                                                  "Dry Wet",
                                                                  0.0,
                                                                  1.0,
                                                                  0.5));
    
    addParameter(mDepthParameter = new juce::AudioParameterFloat({ "depth", 1 },
                                                                     "Depth",
                                                                     0.0,
                                                                     1.0,
                                                                     0.5));
    
    addParameter(mRateParameter = new juce::AudioParameterFloat({ "rate", 1 },
                                                                     "Rate",
                                                                     0.1f,
                                                                     20.0f,
                                                                     10.0f));
    
    addParameter(mPhaseOffsetParameter = new juce::AudioParameterFloat({ "phaseoffset", 1 },
                                                                     "Phase Offset",
                                                                     0.0f,
                                                                     1.0f,
                                                                     0.0f));
    
    addParameter(mFeedbackParameter = new juce::AudioParameterFloat({ "feedback", 1 },
                                                                    "Feedback",
                                                                    0.0,
                                                                    0.98,
                                                                    0.5));
    
    addParameter(mTypeParameter = new juce::AudioParameterInt({ "type", 1 },
                                                                     "Type",
                                                                     0,
                                                                     1,
                                                                     0));
    
    mCircularBufferLeft = nullptr;
    mCircularBufferRight = nullptr;
    mCircularBufferWriteHead = 0;
    mCircularBufferLength = 0;
    mDelayTimeInSamples = 0;
    mDelayReadHead = 0;
    mFeedbackLeft = 0;
    mFeedbackRight = 0;
    mDelayTimeSmoothed = 0;
}

ChorusFlangerAudioProcessor::~ChorusFlangerAudioProcessor()
{
    if (mCircularBufferLeft != nullptr) {
        delete [] mCircularBufferLeft;
        mCircularBufferLeft = nullptr;
    }
    
    if (mCircularBufferRight != nullptr) {
        delete [] mCircularBufferRight;
        mCircularBufferRight = nullptr;
    }
}

//==============================================================================
const juce::String ChorusFlangerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool ChorusFlangerAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool ChorusFlangerAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool ChorusFlangerAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double ChorusFlangerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int ChorusFlangerAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int ChorusFlangerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void ChorusFlangerAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String ChorusFlangerAudioProcessor::getProgramName (int index)
{
    return {};
}

void ChorusFlangerAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void ChorusFlangerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    mDelayTimeInSamples = 1;
    
    mCircularBufferLength = sampleRate * MAX_DELAY_TIME;
    
    if (mCircularBufferLeft == nullptr) {
        mCircularBufferLeft = new float[int(mCircularBufferLength)];
    }
    
    juce::zeromem(mCircularBufferLeft, mCircularBufferLength *sizeof(float));
    
    if (mCircularBufferRight == nullptr) {
        mCircularBufferRight = new float[int(mCircularBufferLength)];
    }
    
    juce::zeromem(mCircularBufferRight, mCircularBufferLength *sizeof(float));
    
    mCircularBufferWriteHead = 0;
}

void ChorusFlangerAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool ChorusFlangerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void ChorusFlangerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    mDelayTimeInSamples = getSampleRate() * 1;
    
    float* leftChannel = buffer.getWritePointer(0);
    float* rightChannel = buffer.getWritePointer(1);
    
    for (int i = 0; i < buffer.getNumSamples(); i++) {
        mDelayTimeSmoothed = mDelayTimeSmoothed - 0.0001 * (mDelayTimeSmoothed - 1);
        mDelayTimeInSamples = getSampleRate() * mDelayTimeSmoothed;
        
        mCircularBufferLeft[mCircularBufferWriteHead] = leftChannel[i] + mFeedbackLeft;
        mCircularBufferRight[mCircularBufferWriteHead] = rightChannel[i] + mFeedbackRight;
        
        mDelayReadHead = mCircularBufferWriteHead - mDelayTimeInSamples;
        
        if (mDelayReadHead < 0) {
            mDelayReadHead += mCircularBufferLength;
        }
        
        int readHead_x = (int)mDelayReadHead;
        int readHead_x1 = readHead_x + 1;
        
        float readHeadFloat = mDelayReadHead - readHead_x;
        
        if (readHead_x1 >= mCircularBufferLength) {
            readHead_x1 -= mCircularBufferLength;
        }
        
        float delaySampleLeft = lin_interp(mCircularBufferLeft[readHead_x], mCircularBufferLeft[readHead_x1], readHeadFloat);
        float delaySampleRight = lin_interp(mCircularBufferRight[readHead_x], mCircularBufferRight[readHead_x1], readHeadFloat);
        
        mFeedbackLeft = delaySampleLeft * *mFeedbackParameter;
        mFeedbackRight = delaySampleRight * *mFeedbackParameter;
        
        mCircularBufferWriteHead++;
        
        buffer.setSample(0, i, buffer.getSample(0, i) * (1 - *mDryWetParameter) + delaySampleLeft * *mDryWetParameter);
        buffer.setSample(1, i, buffer.getSample(1, i) * (1 - *mDryWetParameter) + delaySampleLeft * *mDryWetParameter);
        
        if (mCircularBufferWriteHead >= mCircularBufferLength) {
            mCircularBufferWriteHead = 0;
        }
    }
}

//==============================================================================
bool ChorusFlangerAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* ChorusFlangerAudioProcessor::createEditor()
{
    return new ChorusFlangerAudioProcessorEditor (*this);
}

//==============================================================================
void ChorusFlangerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void ChorusFlangerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChorusFlangerAudioProcessor();
}

float ChorusFlangerAudioProcessor::lin_interp(float inSampleX, float inSampleY, float inFloatPhase)
{
    return (1 - inFloatPhase) * inSampleX  + inFloatPhase * inSampleY;
}
