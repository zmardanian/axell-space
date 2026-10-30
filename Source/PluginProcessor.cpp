#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
const juce::Identifier kPresetNameId ("presetName");
juce::AudioParameterFloatAttributes fmt (std::function<juce::String (float)> toText)
{
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
        .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
}
juce::String dbText (float v) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
juce::String pctText (float v) { return juce::String (juce::roundToInt (v)) + " %"; }
juce::String msText (float v) { return v < 100.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; }
juce::String sText (float v) { return juce::String (v, v < 10.0f ? 2 : 1) + " s"; }
juce::String hzText (float v) { return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz" : juce::String (v / 1000.0f, 1) + " kHz"; }
juce::NormalisableRange<float> skewed (float lo, float hi, float step, float centre)
{
    juce::NormalisableRange<float> r (lo, hi, step);
    r.setSkewForCentre (centre);
    return r;
}
const juce::StringArray kTypes { "Booth", "Room", "Plate", "Hall" };
const juce::StringArray kNotes { "1/16", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2", "1/1" };
} // namespace

APVTS::ParameterLayout SpaceProcessor::createLayout()
{
    using namespace juce;
    const axs::Params d;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    // IDs and order are frozen from 1.0 on (saved projects, automation); new parameters are appended
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "reverbOn", 1 }, "Reverb", d.reverbOn));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "type", 1 }, "Reverb Type", kTypes, d.type));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "size", 1 }, "Size", NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.size, fmt (pctText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "decay", 1 }, "Decay", skewed (0.2f, 10.0f, 0.01f, 1.5f), d.decay, fmt (sText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "preDelay", 1 }, "Pre-Delay", skewed (0.0f, 250.0f, 0.1f, 40.0f), d.preDelayMs, fmt (msText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "damping", 1 }, "Damping", NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.damping, fmt (pctText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "early", 1 }, "Early Reflections", NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.early, fmt (pctText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "modulation", 1 }, "Modulation", NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.modulation, fmt (pctText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "revLowCut", 1 }, "Reverb Low Cut", skewed (20.0f, 1000.0f, 1.0f, 150.0f), d.revLowCut, fmt (hzText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "revHighCut", 1 }, "Reverb High Cut", skewed (1000.0f, 20000.0f, 10.0f, 6000.0f), d.revHighCut, fmt (hzText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "width", 1 }, "Width", NormalisableRange<float> (0.0f, 200.0f, 0.1f), d.width, fmt (pctText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "reverbLevel", 1 }, "Reverb Level", NormalisableRange<float> (-24.0f, 6.0f, 0.1f), d.reverbDb, fmt (dbText)));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "delayOn", 1 }, "Delay", d.delayOn));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "sync", 1 }, "Delay Sync", d.sync));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "note", 1 }, "Delay Note", kNotes, d.note));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "time", 1 }, "Delay Time", skewed (1.0f, 2000.0f, 0.1f, 250.0f), d.timeMs, fmt (msText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "feedback", 1 }, "Feedback", NormalisableRange<float> (0.0f, 95.0f, 0.1f), d.feedback, fmt (pctText)));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "pingPong", 1 }, "Ping-Pong", d.pingPong));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "dlyLowCut", 1 }, "Delay Low Cut", skewed (20.0f, 2000.0f, 1.0f, 250.0f), d.dlyLowCut, fmt (hzText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "dlyHighCut", 1 }, "Delay High Cut", skewed (1000.0f, 20000.0f, 10.0f, 6000.0f), d.dlyHighCut, fmt (hzText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "drive", 1 }, "Drive", NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.drive, fmt (pctText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "delayLevel", 1 }, "Delay Level", NormalisableRange<float> (-24.0f, 6.0f, 0.1f), d.delayDb, fmt (dbText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "delayToReverb", 1 }, "Delay into Reverb", NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.delayToReverb, fmt (pctText)));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "essDuck", 1 }, "Ess Duck", d.essDuck));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "essDuckDb", 1 }, "Ess Duck Amount", NormalisableRange<float> (0.0f, 24.0f, 0.1f), d.essDuckDb, fmt (dbText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "voiceDuck", 1 }, "Voice Duck", NormalisableRange<float> (0.0f, 24.0f, 0.1f), d.voiceDuckDb, fmt (dbText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "duckRelease", 1 }, "Duck Release", skewed (50.0f, 2000.0f, 1.0f, 300.0f), d.duckReleaseMs, fmt (msText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.mix, fmt (pctText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), d.outputDb, fmt (dbText)));
    // 2.0 (appended): the reverb core, BASS, FREEZE, PRE-DELAY in note values
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "engine", 2 }, "Reverb Engine", StringArray { "1.0 (classic)", "2.0" }, d.engine - 1));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "bass", 2 }, "Bass Decay", NormalisableRange<float> (50.0f, 200.0f, 0.1f), d.bass, fmt (pctText)));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "freeze", 2 }, "Freeze", d.freeze));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "preSync", 2 }, "Pre-Delay Sync", d.preSync));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "preNote", 2 }, "Pre-Delay Note", kNotes, d.preNote));
    // 3.0 (appended; their defaults are the 2.0 sound, so older sessions are unchanged): the delay's character, UNMASK
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "dlyMode", 3 }, "Delay Mode", StringArray { "Clean", "Tape", "Analog" }, d.dlyMode));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "wow", 3 }, "Wow & Flutter", NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.wowFlutter, fmt (pctText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "diffuse", 3 }, "Delay Diffuse", NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.diffuse, fmt (pctText)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "unmask", 3 }, "Unmask", NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.unmask, fmt (pctText)));
    return { p.begin(), p.end() };
}

SpaceProcessor::SpaceProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

axs::Params SpaceProcessor::readParams() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    axs::Params p;
    p.reverbOn = v ("reverbOn") > 0.5f;
    p.type = juce::roundToInt (v ("type"));
    p.size = v ("size");
    p.decay = v ("decay");
    p.preDelayMs = v ("preDelay");
    p.damping = v ("damping");
    p.early = v ("early");
    p.modulation = v ("modulation");
    p.revLowCut = v ("revLowCut");
    p.revHighCut = v ("revHighCut");
    p.width = v ("width");
    p.reverbDb = v ("reverbLevel");
    p.delayOn = v ("delayOn") > 0.5f;
    p.sync = v ("sync") > 0.5f;
    p.note = juce::roundToInt (v ("note"));
    p.timeMs = v ("time");
    p.feedback = v ("feedback");
    p.pingPong = v ("pingPong") > 0.5f;
    p.dlyLowCut = v ("dlyLowCut");
    p.dlyHighCut = v ("dlyHighCut");
    p.drive = v ("drive");
    p.delayDb = v ("delayLevel");
    p.delayToReverb = v ("delayToReverb");
    p.essDuck = v ("essDuck") > 0.5f;
    p.essDuckDb = v ("essDuckDb");
    p.voiceDuckDb = v ("voiceDuck");
    p.duckReleaseMs = v ("duckRelease");
    p.mix = v ("mix");
    p.outputDb = v ("output");
    p.engine = juce::roundToInt (v ("engine")) + 1;
    p.bass = v ("bass");
    p.freeze = v ("freeze") > 0.5f;
    p.preSync = v ("preSync") > 0.5f;
    p.preNote = juce::roundToInt (v ("preNote"));
    p.dlyMode = juce::roundToInt (v ("dlyMode"));
    p.wowFlutter = v ("wow");
    p.diffuse = v ("diffuse");
    p.unmask = v ("unmask");
    p.bpm = bpm.load();
    return p;
}

void SpaceProcessor::writeParams (const axs::Params& p)
{
    auto set = [this] (const char* id, float v) {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    };
    set ("reverbOn", p.reverbOn ? 1.0f : 0.0f);
    set ("type", (float) p.type);
    set ("size", p.size);
    set ("decay", p.decay);
    set ("preDelay", p.preDelayMs);
    set ("damping", p.damping);
    set ("early", p.early);
    set ("modulation", p.modulation);
    set ("revLowCut", p.revLowCut);
    set ("revHighCut", p.revHighCut);
    set ("width", p.width);
    set ("reverbLevel", p.reverbDb);
    set ("delayOn", p.delayOn ? 1.0f : 0.0f);
    set ("sync", p.sync ? 1.0f : 0.0f);
    set ("note", (float) p.note);
    set ("time", p.timeMs);
    set ("feedback", p.feedback);
    set ("pingPong", p.pingPong ? 1.0f : 0.0f);
    set ("dlyLowCut", p.dlyLowCut);
    set ("dlyHighCut", p.dlyHighCut);
    set ("drive", p.drive);
    set ("delayLevel", p.delayDb);
    set ("delayToReverb", p.delayToReverb);
    set ("essDuck", p.essDuck ? 1.0f : 0.0f);
    set ("essDuckDb", p.essDuckDb);
    set ("voiceDuck", p.voiceDuckDb);
    set ("duckRelease", p.duckReleaseMs);
    set ("mix", p.mix);
    set ("output", p.outputDb);
    set ("engine", (float) (p.engine - 1));
    set ("bass", p.bass);
    set ("preSync", p.preSync ? 1.0f : 0.0f);
    set ("preNote", (float) p.preNote);
    set ("dlyMode", (float) p.dlyMode);
    set ("wow", p.wowFlutter);
    set ("diffuse", p.diffuse);
    set ("unmask", p.unmask);
}

bool SpaceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && out == layouts.getMainInputChannelSet();
}

void SpaceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedChannels = juce::jlimit (1, 2, getMainBusNumOutputChannels());
    maxChunk = juce::jmax (samplesPerBlock, 32);
    for (auto& b : dryCopy)
        b.assign ((size_t) maxChunk, 0.0f);
    dsp.setParams (readParams());
    dsp.prepare (sampleRate, samplesPerBlock);
    bypassStep = (float) (1.0 / juce::jmax (1.0, 0.01 * sampleRate));
    setLatencySamples (0);
}

void SpaceProcessor::reset()
{
    if (preparedChannels > 0)
        dsp.reset();
}

void SpaceProcessor::run (juce::AudioBuffer<float>& buffer, bool bypass)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = getMainBusNumInputChannels(); c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);
    const int nch = juce::jmin (buffer.getNumChannels(), preparedChannels);
    if (preparedChannels == 0 || n == 0)
        return;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                if (*b > 1.0)
                    bpm = *b;
    for (int c = 0; c < nch; ++c)
        inPeak[c] = buffer.getMagnitude (c, 0, n);
    dsp.setParams (readParams());
    const float target = bypass ? 1.0f : 0.0f;
    // in chunks no larger than prepared (a host may exceed its announced block size): no allocation here
    for (int off = 0; off < n; off += maxChunk)
    {
        const int m = juce::jmin (maxChunk, n - off);
        float* io[2] {};
        for (int c = 0; c < nch; ++c)
        {
            io[c] = buffer.getWritePointer (c) + off;
            std::copy (io[c], io[c] + m, dryCopy[(size_t) c].begin());
        }
        dsp.process (io, nch, m);
        // host bypass: the input, crossfaded in over 10 ms (no latency to compensate)
        for (int i = 0; i < m; ++i)
        {
            bypassAmt += juce::jlimit (-bypassStep, bypassStep, target - bypassAmt);
            if (bypassAmt <= 0.0f)
                continue;
            for (int c = 0; c < nch; ++c)
                io[c][i] = bypassAmt >= 1.0f ? dryCopy[(size_t) c][(size_t) i]   // fully bypassed: the input, bit for bit
                                             : io[c][i] + (dryCopy[(size_t) c][(size_t) i] - io[c][i]) * bypassAmt;
        }
    }
    for (int c = 0; c < nch; ++c)
        outPeak[c] = buffer.getMagnitude (c, 0, n);
    essNow = dsp.essProbability();
    duckNow = dsp.wetDuckDb();
    for (int b = 0; b < axs::Engine::kUnmaskBands; ++b)
        unmaskNow[(size_t) b].store (dsp.unmaskDb (b), std::memory_order_relaxed);
}

void SpaceProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) { run (b, false); }
void SpaceProcessor::processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer&) { run (b, true); }

//== presets, A/B ==================================================================================================
int SpaceProcessor::getNumPrograms() { return (int) axs::factoryPresets().size(); }

void SpaceProcessor::setCurrentProgram (int index)
{
    index = juce::jlimit (0, getNumPrograms() - 1, index);
    currentProgram = index;
    writeParams (axs::factoryPresets()[(size_t) index].p);
    setPresetName (axs::factoryPresets()[(size_t) index].name);
}

const juce::String SpaceProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? juce::String (axs::factoryPresets()[(size_t) index].name) : juce::String();
}

void SpaceProcessor::selectSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == activeSlot)
        return;
    slots[activeSlot] = stateWithName();
    if (slots[slot].isValid())
    {
        restoreTree (slots[slot].createCopy());
        setPresetName (slots[slot].getProperty (kPresetNameId, getPresetName()).toString());
    }
    else
        slots[slot] = stateWithName();
    activeSlot = slot;
}

void SpaceProcessor::copyActiveToOther() { slots[1 - activeSlot] = stateWithName(); }

juce::String SpaceProcessor::getPresetName() const
{
    const juce::ScopedLock sl (nameLock);
    return presetName;
}

void SpaceProcessor::setPresetName (const juce::String& n)
{
    const juce::ScopedLock sl (nameLock);
    presetName = n;
}

juce::ValueTree SpaceProcessor::stateWithName()
{
    auto state = apvts.copyState();
    state.setProperty (kPresetNameId, getPresetName(), nullptr);
    return state;
}

// APVTS::replaceState skips a parameter whose snapped value already matches; re-apply explicitly (pluginval scenario)
void SpaceProcessor::restoreTree (const juce::ValueTree& source)
{
    // a state saved before 2.0 has no "engine": it keeps the 1.0 reverb, so the session sounds exactly as it did
    auto tree = source.createCopy();
    if (! tree.getChildWithProperty ("id", "engine").isValid())
    {
        juce::ValueTree e ("PARAM");
        e.setProperty ("id", "engine", nullptr);
        e.setProperty ("value", 0.0f, nullptr);
        tree.appendChild (e, nullptr);
    }
    apvts.replaceState (tree);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto child = apvts.state.getChildWithProperty ("id", rp->paramID);
            if (child.isValid() && child.hasProperty ("value"))
            {
                const float norm = rp->convertTo0to1 ((float) child.getProperty ("value"));
                if (std::abs (rp->getValue() - norm) > 1.0e-6f)
                    rp->setValueNotifyingHost (norm);
            }
        }
}

void SpaceProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // the parameters and (1.0.1) the preset's name, so a project or a Dojo chain reopens showing its preset
    auto state = stateWithName();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SpaceProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        setPresetName (tree.getProperty (kPresetNameId, getPresetName()).toString());
        tree.removeProperty (kPresetNameId, nullptr);
        restoreTree (tree);
    }
}

juce::AudioProcessorEditor* SpaceProcessor::createEditor() { return new SpaceEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpaceProcessor(); }
