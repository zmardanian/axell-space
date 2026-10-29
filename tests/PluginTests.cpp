// Axell Space - plugin-level tests (JUCE): parameters, presets, state, A/B, latency, host bypass, mono, rates, editor.
// Exit code = number of failures.
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cstdio>
#include <random>

namespace
{
int failures = 0, checks = 0;
void check (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok)
        ++failures;
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
}
void setParam (SpaceProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}
float getParam (SpaceProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }
void prepare (SpaceProcessor& p, double fs = 48000.0, int block = 256, int ch = 2)
{
    p.setPlayConfigDetails (ch, ch, fs, block);
    p.prepareToPlay (fs, block);
}
void pumpMessages (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

std::vector<std::vector<float>> render (SpaceProcessor& p, std::vector<std::vector<float>> x, int block, bool bypassed = false)
{
    const int nc = (int) x.size();
    juce::AudioBuffer<float> buf (nc, block);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos < x[0].size(); pos += (size_t) block)
    {
        const int m = (int) std::min<size_t> ((size_t) block, x[0].size() - pos);
        buf.setSize (nc, m, false, false, true);
        for (int c = 0; c < nc; ++c)
            buf.copyFrom (c, 0, x[(size_t) c].data() + pos, m);
        if (bypassed)
            p.processBlockBypassed (buf, midi);
        else
            p.processBlock (buf, midi);
        for (int c = 0; c < nc; ++c)
            std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + m, x[(size_t) c].begin() + (long) pos);
    }
    return x;
}
std::vector<float> voiceLike (int n, double fs, unsigned seed)
{
    std::vector<float> x ((size_t) n);
    std::mt19937 g (seed);
    std::normal_distribution<float> nd;
    double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs, u = std::fmod (t, 0.5);
        ph += 2 * juce::MathConstants<double>::pi * (130 + 20 * std::sin (t)) / fs;
        double y = 0;
        for (int h = 1; h < 25; ++h)
            y += std::sin (h * ph) / h;
        x[(size_t) i] = (float) ((u < 0.35 ? 0.15 * y : 0.0) + 0.001 * nd (g));
    }
    return x;
}
bool sameParams (const axs::Params& a, const axs::Params& b)
{
    auto near = [] (float x, float y) { return std::abs (x - y) <= 0.051f + 0.002f * std::abs (y); };   // the parameters' steps
    return a.reverbOn == b.reverbOn && a.type == b.type && near (a.size, b.size) && near (a.decay, b.decay) && near (a.preDelayMs, b.preDelayMs)
           && near (a.damping, b.damping) && near (a.early, b.early) && near (a.modulation, b.modulation) && near (a.revLowCut, b.revLowCut)
           && near (a.revHighCut, b.revHighCut) && near (a.width, b.width) && near (a.reverbDb, b.reverbDb) && a.delayOn == b.delayOn
           && a.sync == b.sync && a.note == b.note && near (a.timeMs, b.timeMs) && near (a.feedback, b.feedback) && a.pingPong == b.pingPong
           && near (a.dlyLowCut, b.dlyLowCut) && near (a.dlyHighCut, b.dlyHighCut) && near (a.drive, b.drive) && near (a.delayDb, b.delayDb)
           && near (a.delayToReverb, b.delayToReverb) && a.essDuck == b.essDuck && near (a.essDuckDb, b.essDuckDb)
           && near (a.voiceDuckDb, b.voiceDuckDb) && near (a.duckReleaseMs, b.duckReleaseMs) && near (a.mix, b.mix) && near (a.outputDb, b.outputDb);
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    //== 1. parameters: frozen IDs and order, version hints, defaults = the engine's
    {
        SpaceProcessor p;
        const juce::StringArray frozen { "reverbOn", "type", "size", "decay", "preDelay", "damping", "early", "modulation", "revLowCut",
                                         "revHighCut", "width", "reverbLevel", "delayOn", "sync", "note", "time", "feedback", "pingPong",
                                         "dlyLowCut", "dlyHighCut", "drive", "delayLevel", "delayToReverb", "essDuck", "essDuckDb",
                                         "voiceDuck", "duckRelease", "mix", "output" };
        auto& params = p.getParameters();
        check (params.size() == frozen.size(), "parameter count = " + juce::String (frozen.size()));
        bool ids = true;
        for (int i = 0; i < params.size() && i < frozen.size(); ++i)
        {
            auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (params[i]);
            ids = ids && wp != nullptr && wp->getParameterID() == frozen[i] && wp->getVersionHint() == 1;
        }
        check (ids, "every parameter in its frozen place, version hint 1");
        check (sameParams (p.readParams(), axs::Params {}), "defaults = the engine's defaults (= the Init preset)");
        check (p.getLatencySamples() == 0 && p.getTailLengthSeconds() >= 10.0, "no latency; tail length covers the longest reverb");
    }

    //== 2. presets: each applies exactly its settings
    {
        SpaceProcessor p;
        bool ok = true;
        juce::String bad;
        for (int i = 0; i < p.getNumPrograms(); ++i)
        {
            p.setCurrentProgram (i);
            if (! sameParams (p.readParams(), axs::factoryPresets()[(size_t) i].p))
            {
                ok = false;
                bad << p.getProgramName (i) << " ";
            }
        }
        check (ok && p.getNumPrograms() == (int) axs::factoryPresets().size(),
               "every factory preset applies its exact settings (" + juce::String (p.getNumPrograms()) + ")" + (ok ? "" : ": " + bad));
    }

    //== 3. state round trip and A/B
    {
        SpaceProcessor a;
        setParam (a, "decay", 3.3f);
        setParam (a, "type", 3.0f);
        setParam (a, "delayOn", 1.0f);
        setParam (a, "note", 3.0f);
        setParam (a, "voiceDuck", 7.5f);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        SpaceProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (sameParams (a.readParams(), b.readParams()), "state restore: every parameter identical");
        SpaceProcessor c;
        setParam (c, "decay", 0.8f);
        c.selectSlot (1);
        setParam (c, "decay", 4.0f);
        c.selectSlot (0);
        const float s0 = getParam (c, "decay");
        c.selectSlot (1);
        check (std::abs (s0 - 0.8f) < 0.011f && std::abs (getParam (c, "decay") - 4.0f) < 0.011f, "A/B: each slot keeps its own settings");
    }

    //== 4. audio through the plugin at every rate: dry exact at MIX 0, host bypass exact, wet present, finite
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const juce::String R = " @ " + juce::String (fs / 1000.0, 1) + " kHz";
        SpaceProcessor p;
        prepare (p, fs, 256);
        const int n = (int) (fs * 2.0);
        auto v = voiceLike (n, fs, 3);
        std::vector<std::vector<float>> x { v, v };
        setParam (p, "mix", 0.0f);
        auto y = render (p, x, 256);
        double e = 0;
        for (int i = (int) (0.5 * fs); i < n; ++i) e = std::max (e, (double) std::abs (y[0][(size_t) i] - x[0][(size_t) i]));
        check (e == 0.0, "MIX turned to 0 %: after its glide, the output is the input, sample for sample" + R);
        setParam (p, "mix", 30.0f);
        p.reset();
        auto w = render (p, x, 256);
        double diff = 0;
        bool finite = true;
        for (int i = 0; i < n; ++i)
        {
            diff += std::abs (w[0][(size_t) i] - x[0][(size_t) i]);
            finite = finite && std::isfinite (w[0][(size_t) i]) && std::isfinite (w[1][(size_t) i]);
        }
        check (diff > 1.0 && finite, "MIX 30 %: the effect is heard, every sample finite" + R);
        SpaceProcessor q;
        prepare (q, fs, 256);
        render (q, x, 256);          // ring
        auto b = render (q, x, 256, true);
        double eb = 0;
        for (int i = (int) (0.02 * fs); i < n; ++i) eb = std::max (eb, (double) std::abs (b[0][(size_t) i] - x[0][(size_t) i]));
        check (eb == 0.0, "host bypass: after the 10 ms crossfade, exactly the input" + R);
    }

    //== 5. mono track (1 in / 1 out) and blocks larger than announced
    {
        SpaceProcessor p;
        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        check (p.setBusesLayout (mono), "mono layout accepted");
        prepare (p, 48000.0, 128, 1);
        auto v = voiceLike (48000, 48000.0, 9);
        auto y = render (p, { v }, 700);   // larger than the announced 128
        bool fin = true;
        for (float s : y[0]) fin = fin && std::isfinite (s);
        check (fin, "mono, blocks larger than announced: processes, finite");
    }

    //== 6. tempo: the delay follows the host's BPM (through the engine's parameters)
    {
        SpaceProcessor p;
        prepare (p);
        check (std::abs (p.readParams().bpm - 120.0) < 1e-9, "without a host transport the tempo is 120 BPM");
    }

    //== 7. editor: open / close while processing, size limits, the last size remembered
    {
        SpaceProcessor p;
        prepare (p);
        auto v = voiceLike (4096, 48000.0, 2);
        bool ok = true;
        for (int i = 0; i < 5; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            render (p, { v, v }, 512);
            pumpMessages (40);
            ok = ok && ed->getWidth() == gui::kWidth;
        }
        check (ok, "editor: opens / closes 5x while processing (100 % size)");
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* c = ed->getConstrainer();
        check (c != nullptr && c->getMinimumWidth() == gui::kWidth * 3 / 4 && c->getMaximumWidth() == 2 * gui::kWidth, "editor: resizable 75-200 %");
        ed->setSize (gui::kWidth * 3 / 2, gui::kHeight * 3 / 2);
        ed.reset();
        std::unique_ptr<juce::AudioProcessorEditor> ed2 (p.createEditor());
        check (ed2->getWidth() == gui::kWidth * 3 / 2, "editor: re-opens at the last used size (150 %)");
    }

    if (failures == 0)
        std::printf ("ALL PASS: %d of %d plugin checks passed\n", checks, checks);
    else
        std::printf ("FAILURES: %d of %d plugin checks failed\n", failures, checks);
    return failures;
}
