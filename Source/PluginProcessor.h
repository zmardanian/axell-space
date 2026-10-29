#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"
#include "Presets.h"

#include <atomic>

// Thin JUCE wrapper around axs::Engine (all DSP lives in the JUCE-free Engine.h). No latency: the dry signal passes
// straight through; the wet path's 8 ms ess look-ahead is part of its pre-delay.
class SpaceProcessor : public juce::AudioProcessor
{
public:
    SpaceProcessor();
    ~SpaceProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }   // the longest reverb (10 s) or delay feedback ring-out

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    axs::Params readParams() const;
    void writeParams (const axs::Params& p);
    int getActiveSlot() const { return activeSlot; }
    void selectSlot (int slot);
    void copyActiveToOther();
    // the name of the preset the settings came from (a factory preset, or a name a project or the Dojo gave them)
    juce::String getPresetName() const;
    void setPresetName (const juce::String& n);
    const axs::Engine& engine() const { return dsp; }
    double hostBpm() const { return bpm.load(); }

    // meters (any thread): peak level of the last block, linear
    std::atomic<float> inPeak[2] { 0.0f, 0.0f }, outPeak[2] { 0.0f, 0.0f };
    std::atomic<float> essNow { 0.0f }, duckNow { 0.0f };

private:
    void run (juce::AudioBuffer<float>& buffer, bool bypass);
    void restoreTree (const juce::ValueTree& tree);
    juce::ValueTree stateWithName();

    axs::Engine dsp;
    int preparedChannels = 0, maxChunk = 512;
    std::array<std::vector<float>, 2> dryCopy;
    float bypassAmt = 0.0f, bypassStep = 0.001f;
    int currentProgram = 0, activeSlot = 0;
    juce::ValueTree slots[2];
    std::atomic<double> bpm { 120.0 };
    juce::CriticalSection nameLock;
    juce::String presetName { "Init" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpaceProcessor)
};
