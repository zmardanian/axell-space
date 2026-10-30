#include "PluginEditor.h"

#include <axell/gui/BrandTitle.h>

#include <cmath>

using namespace ui;

namespace
{
const juce::Identifier kUiWidth ("uiWidth");
const juce::Colour kSpaceAccent (0xffb48cf0);   // Space's own colour: the emblem's wave and dot
namespace tip
{
const char* reverbOn = "Reverb on / off. Switched off, its tail rings out first (nothing is cut).";
const char* type = "BOOTH: a small, dense booth around the voice. ROOM: a natural room, early reflections first. "
                   "PLATE: the classic vocal plate, dense and bright from the start. HALL: big and long, for hooks.";
const char* size = "The size of the space: longer delays between reflections, a bigger room.";
const char* decay = "How long the reverb rings: the time to fall 60 dB (RT60), at mid frequencies.";
const char* preDelay = "A gap between the voice and its reverb: the words stay in front, the space comes behind. "
                       "The wet path always starts 8 ms after the voice (the ess look-ahead); this adds to it.";
const char* damping = "How much faster the highs die than the mids: 0 % = an even, bright decay; 100 % = a dark one "
                      "(at 8 kHz twice as fast as the mids, at every sample rate).";
const char* early = "The early reflections of BOOTH / ROOM / HALL against the tail (PLATE has none: it is dense from the start).";
const char* modulation = "Slow movement inside the reverb that keeps its tail smooth and free of metallic ringing; "
                         "50 % keeps any pitch wobble under about 7 cents.";
const char* revLow = "Removes lows before the reverb: no mud from the bass of the voice.";
const char* revHigh = "Removes highs before the reverb: a darker, smoother space.";
const char* width = "Stereo width of the reverb: 0 % mono, 100 % natural, 200 % extra wide.";
const char* bass = "How long the lows ring re DECAY (2.0 core): 50 % half as long (tight, clear), 200 % twice (big, warm).";
const char* freeze = "Hold the tail forever and let nothing new in - a pad under the next line; off: it decays from there (2.0 core).";
const char* preSync = "PRE-DELAY in note values of the song's tempo instead of milliseconds.";
const char* preNote = "The pre-delay's note value (with PRE SYNC).";
const char* engine = "The reverb core. 2.0: 32 lines, true stereo, three-band decay, smoother and warmer. 1.0: the original - sessions saved "
                     "before 2.0 keep it, so they sound exactly as they did.";
const char* revLevel = "Level of the reverb in the wet signal.";
const char* delayOn = "Delay on / off. Switched off, its repeats ring out first.";
const char* sync = "SYNC: the delay time follows the song's tempo (the note value). Off: TIME in milliseconds.";
const char* note = "The delay time as a note value at the host's tempo (T = triplet, . = dotted).";
const char* time = "The delay time in milliseconds (SYNC off).";
const char* feedback = "How many repeats: the share of each repeat fed back into the next.";
const char* pingPong = "The repeats bounce from left to right.";
const char* dlyLow = "Thins every repeat a little more: repeats get smaller and out of the way.";
const char* dlyHigh = "Darkens every repeat a little more, like tape or an analog delay.";
const char* drive = "Soft saturation of the repeats: warm, worn repeats like an old tape echo.";
const char* dlyLevel = "Level of the delay in the wet signal.";
const char* toReverb = "Sends the delay's repeats into the reverb: the echoes melt into the space.";
const char* dlyMode = "The repeats' character. CLEAN: exact copies. TAPE: recorded on tape - each pass a little darker (about 6 dB less "
                      "at 10 kHz), a warm head bump, soft saturation, the tape's wow and flutter. ANALOG: a bucket-brigade line - "
                      "dark (3.6 kHz), soft clipping, a hint of clock jitter. A change crossfades over 20 ms.";
const char* wow = "How much the tape moves (TAPE; ANALOG a fifth of it): 40 % = a healthy machine (about 1.6 cents of wow), "
                  "100 % = a worn one (4 cents).";
const char* diffuse = "Each repeat smears a little more than the last: from distinct echoes to a soft, reverb-like trail. The "
                      "repeats stay on the beat.";
const char* unmask = "The effects step back only where they would cover the voice - per third of an octave, while the voice "
                     "uses it - and stay full everywhere else (the air while you sing a vowel, the body while an ess hisses). "
                     "On the artist's vocal: as clear as VOICE DUCK 6 dB with 2 dB more of the space kept. Up to 12 dB.";
const char* essDuck = "Keeps the esses (S, SH, CH) out of the reverb and the delay: the suite's sibilance network marks them "
                      "and turns them down before they reach the effects - no splashy, hissing tails. The voice itself is untouched.";
const char* essDb = "How far an ess is turned down before the effects (measured on your vocal: 85 % of ess moments turned down, no vowel touched).";
const char* voiceDuck = "Turns the reverb and delay down while you rap and lets them bloom in the gaps - big spaces "
                        "without washing the words out. 0 dB = off.";
const char* release = "How fast the effects come back after the voice stops.";
const char* mix = "Dry / wet: up to 50 % the voice stays at full level and the effects come in; above 50 % the voice "
                  "goes down. 100 % = wet only (for a send / aux track).";
const char* output = "Output level.";
const char* view = "The space: the reverb's decay (level over time, to -60 dB) and the delay's repeats. "
                   "ESS lights while an ess is being kept out of the effects; DUCK shows how far the effects are turned down.";
} // namespace tip
} // namespace

//==============================================================================
void SpaceView::set (const axs::Params& p, int lookSamples, double fs, float essP, float duckDb, const float* unmaskDb)
{
    params = p;
    for (size_t b = 0; b < unmask.size(); ++b)
        unmask[b] = unmaskDb[b];
    look = lookSamples;
    rate = fs;
    ess = essP;
    duck = duckDb;
    repaint();
}

void SpaceView::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto r = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (r, 10.0f);
    auto plot = r.reduced (14.0f, 12.0f).withTrimmedLeft (26.0f).withTrimmedBottom (16.0f).withTrimmedRight (110.0f);
    // the time axis: long enough for the longer of the reverb or the first four repeats
    const double lookMs = 1000.0 * look / rate;
    const double rt = params.reverbOn ? params.decay : 0.0;
    const double dly = params.sync ? axs::noteBeats (params.note) * 60.0 / std::max (20.0, params.bpm) : params.timeMs * 0.001;
    const double span = std::clamp (std::max ({ 0.6, (params.preDelayMs + lookMs) / 1000.0 + rt * 1.05, params.delayOn ? dly * 4.2 : 0.0 }), 0.6, 12.0);
    auto x = [&] (double t) { return plot.getX() + (float) (t / span) * plot.getWidth(); };
    auto y = [&] (double db) { return plot.getY() + (float) (jlimit (0.0, 60.0, -db) / 60.0) * plot.getHeight(); };
    g.setFont (ui::font (10.0f));
    for (int k = 0; k <= 4; ++k)
    {
        const double db = -15.0 * k;
        g.setColour (col::grid);
        g.drawHorizontalLine ((int) y (db), plot.getX(), plot.getRight());
        g.setColour (col::faint);
        g.drawText (String ((int) db), Rectangle<float> (r.getX() + 2.0f, y (db) - 6.0f, 32.0f, 12.0f), Justification::right);
    }
    const double tick = span > 6.0 ? 1.0 : (span > 2.5 ? 0.5 : (span > 1.0 ? 0.25 : 0.1));
    for (double t = 0.0; t <= span + 1.0e-9; t += tick)
    {
        g.setColour (col::grid);
        g.drawVerticalLine ((int) x (t), plot.getY(), plot.getBottom());
        g.setColour (col::faint);
        g.drawText (t < 1.0 ? String (juce::roundToInt (t * 1000.0)) + " ms" : String (t, 2) + " s",
                    Rectangle<float> (x (t) - 30.0f, plot.getBottom() + 2.0f, 60.0f, 12.0f), Justification::centred);
    }
    // the reverb's envelope: from the pre-delay, falling 60 dB over DECAY (the early reflections at the start)
    if (params.reverbOn)
    {
        const double t0 = (params.preDelayMs + lookMs) / 1000.0;
        Path p;
        p.startNewSubPath (x (t0), plot.getBottom());
        p.lineTo (x (t0), y (params.type == axs::kPlate ? -2.0 : -1.0));
        if (params.type != axs::kPlate && params.early > 1.0f)
        {
            const double er = t0 + 0.012 + 0.06 * params.size / 100.0 * (params.type == axs::kHall ? 2.0 : (params.type == axs::kBooth ? 0.4 : 1.0));
            p.lineTo (x (er), y (-6.0 * (1.0 - params.early / 100.0) - 3.0));
        }
        const double t60 = t0 + params.decay;
        p.lineTo (x (std::min (span, t60)), y (-60.0 * std::min (1.0, (span - t0) / std::max (0.01, (double) params.decay))));
        p.lineTo (x (std::min (span, t60)), plot.getBottom());
        p.closeSubPath();
        g.setColour (col::teal.withAlpha (0.22f));
        g.fillPath (p);
        g.setColour (col::teal);
        g.strokePath (p, PathStrokeType (1.5f));
    }
    // the delay's repeats: a bar per repeat at its level
    if (params.delayOn)
    {
        const double fb = params.feedback / 100.0;
        for (int k = 0; k < 24; ++k)
        {
            const double t = dly * (k + 1);
            if (t > span)
                break;
            const double db = (k == 0 ? 0.0 : 20.0 * std::log10 (std::max (1.0e-6, std::pow (fb, k)))) + params.delayDb - 3.0;
            if (db < -60.0)
                break;
            const float bx = x (t);
            g.setColour ((params.pingPong && (k & 1) ? col::accent.withAlpha (0.55f) : col::accent));
            g.fillRoundedRectangle (bx - 2.5f, y (db), 5.0f, plot.getBottom() - y (db), 2.0f);
        }
    }
    // UNMASK: what it carves out of the effects right now, 200 Hz - 10 kHz (a bar hangs down per third of an octave)
    if (params.unmask > 0.0f)
    {
        auto strip = Rectangle<float> (plot.getRight() - 196.0f, plot.getY() + 4.0f, 190.0f, 46.0f);
        g.setColour (col::well.withAlpha (0.85f));
        g.fillRoundedRectangle (strip.expanded (4.0f, 2.0f), 5.0f);
        g.setFont (ui::caps (9.0f, 1));
        g.setColour (col::faint);
        g.drawText ("UNMASK", strip.removeFromTop (11.0f), Justification::centredLeft);
        const float bw = strip.getWidth() / (float) unmask.size();
        g.setColour (col::grid);
        g.drawHorizontalLine ((int) strip.getY(), strip.getX(), strip.getRight());
        for (size_t b = 0; b < unmask.size(); ++b)
        {
            const float d = jlimit (0.0f, 1.0f, -unmask[b] / 12.0f);
            g.setColour (col::global.withAlpha (0.35f + 0.65f * d));
            g.fillRoundedRectangle (strip.getX() + bw * (float) b + 1.0f, strip.getY(), bw - 2.0f, jmax (1.0f, d * strip.getHeight()), 1.5f);
        }
        g.setFont (ui::font (8.5f));
        g.setColour (col::faint);
        g.drawText ("200", Rectangle<float> (strip.getX(), strip.getBottom() - 10.0f, 30.0f, 10.0f), Justification::centredLeft);
        g.drawText ("10k", Rectangle<float> (strip.getRight() - 30.0f, strip.getBottom() - 10.0f, 30.0f, 10.0f), Justification::centredRight);
    }
    // ESS and DUCK at the right
    auto side = Rectangle<float> (r).removeFromRight (104.0f).reduced (8.0f, 12.0f);
    g.setFont (ui::caps (10.0f, 1));
    const bool essLit = params.essDuck && ess > 0.5f;
    g.setColour (essLit ? col::warn : col::track);
    g.fillEllipse (side.getX(), side.getY() + 3.0f, 10.0f, 10.0f);
    g.setColour (params.essDuck ? col::text : col::faint);
    g.drawText ("ESS", Rectangle<float> (side.getX() + 16.0f, side.getY(), 60.0f, 16.0f), Justification::centredLeft);
    g.setColour (col::dim);
    g.drawText ("DUCK", Rectangle<float> (side.getX(), side.getY() + 30.0f, 80.0f, 14.0f), Justification::centredLeft);
    auto bar = Rectangle<float> (side.getX(), side.getY() + 48.0f, 14.0f, side.getHeight() - 70.0f);
    g.setColour (col::track);
    g.fillRoundedRectangle (bar, 3.0f);
    const float frac = jlimit (0.0f, 1.0f, -duck / 24.0f);
    g.setColour (col::global);
    g.fillRoundedRectangle (bar.withHeight (bar.getHeight() * frac), 3.0f);
    g.setFont (ui::font (11.0f));
    g.setColour (col::text);
    g.drawText (String (duck, 1) + " dB", Rectangle<float> (side.getX() + 20.0f, bar.getY(), 70.0f, 14.0f), Justification::centredLeft);
}

//==============================================================================
SpaceEditor::SpaceEditor (SpaceProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    const int savedWidth = (int) proc.apvts.state.getProperty (kUiWidth, gui::kWidth);
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    auto& s = proc.apvts;
    reverbOn.bind (s, "reverbOn", tip::reverbOn);
    if (auto* tp = s.getParameter ("type"))
        type.bind (*tp, tip::type);
    size.bind (s, "size", tip::size);
    decay.bind (s, "decay", tip::decay);
    preDelay.bind (s, "preDelay", tip::preDelay);
    damping.bind (s, "damping", tip::damping);
    early.bind (s, "early", tip::early);
    modulation.bind (s, "modulation", tip::modulation);
    revLow.bind (s, "revLowCut", tip::revLow);
    revHigh.bind (s, "revHighCut", tip::revHigh);
    width.bind (s, "width", tip::width);
    bass.bind (s, "bass", tip::bass);
    freeze.bind (s, "freeze", tip::freeze);
    preSync.bind (s, "preSync", tip::preSync);
    preNote.bind (s, "preNote", tip::preNote);
    engine.bind (s, "engine", tip::engine);
    for (auto* c : std::initializer_list<juce::Component*> { &bass, &freeze, &preSync, &preNote, &engine })
        content.addAndMakeVisible (c);
    revLevel.bind (s, "reverbLevel", tip::revLevel);
    delayOn.bind (s, "delayOn", tip::delayOn);
    sync.bind (s, "sync", tip::sync);
    note.bind (s, "note", tip::note);
    time.bind (s, "time", tip::time);
    feedback.bind (s, "feedback", tip::feedback);
    pingPong.bind (s, "pingPong", tip::pingPong);
    dlyLow.bind (s, "dlyLowCut", tip::dlyLow);
    dlyHigh.bind (s, "dlyHighCut", tip::dlyHigh);
    drive.bind (s, "drive", tip::drive);
    dlyLevel.bind (s, "delayLevel", tip::dlyLevel);
    toReverb.bind (s, "delayToReverb", tip::toReverb);
    if (auto* mp = s.getParameter ("dlyMode"))
        dlyMode.bind (*mp, tip::dlyMode);
    wow.bind (s, "wow", tip::wow);
    diffuse.bind (s, "diffuse", tip::diffuse);
    unmask.bind (s, "unmask", tip::unmask);
    for (auto* c : std::initializer_list<juce::Component*> { &dlyMode, &wow, &diffuse, &unmask })
        content.addAndMakeVisible (c);
    essDuck.bind (s, "essDuck", tip::essDuck);
    essDb.bind (s, "essDuckDb", tip::essDb);
    voiceDuck.bind (s, "voiceDuck", tip::voiceDuck);
    release.bind (s, "duckRelease", tip::release);
    mix.bind (s, "mix", tip::mix);
    output.bind (s, "output", tip::output);
    view.setTooltip (tip::view);
    for (auto* c : std::initializer_list<juce::Component*> {
             &presetPrev, &presetNext, &presetName, &slotA, &slotB, &slotCopy, &view,
             &reverbOn, &type, &size, &decay, &preDelay, &damping, &early, &modulation, &revLow, &revHigh, &width, &revLevel,
             &delayOn, &sync, &note, &time, &feedback, &pingPong, &dlyLow, &dlyHigh, &drive, &dlyLevel, &toReverb,
             &essDuck, &essDb, &voiceDuck, &release, &mix, &output, &inMeter, &outMeter })
        content.addAndMakeVisible (c);
    for (auto* b : { &slotA, &slotB })
        b->setColour (juce::TextButton::buttonOnColourId, col::global);
    presetPrev.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + proc.getNumPrograms() - 1) % proc.getNumPrograms()); };
    presetNext.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    presetName.onClick = [this] { showPresetMenu(); };
    slotA.onClick = [this] { proc.selectSlot (0); };
    slotB.onClick = [this] { proc.selectSlot (1); };
    slotCopy.onClick = [this] { proc.copyActiveToOther(); };
    presetName.setTooltip ("Presets: vocal spaces, slaps, throws and sends.");
    slotA.setTooltip ("A/B: two complete settings to compare.");
    slotB.setTooltip ("A/B: two complete settings to compare.");
    slotCopy.setTooltip ("Copy the current slot to the other one.");
    setResizable (true, true);
    setResizeLimits (gui::kWidth * 3 / 4, gui::kHeight * 3 / 4, gui::kWidth * 2, gui::kHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) gui::kWidth / gui::kHeight);
    const int w = juce::jlimit (gui::kWidth * 3 / 4, gui::kWidth * 2, savedWidth);
    setSize (w, juce::roundToInt (w * (double) gui::kHeight / gui::kWidth));
    constructed = true;
    timerCallback();
    startTimerHz (30);
}

SpaceEditor::~SpaceEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SpaceEditor::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void SpaceEditor::resized()
{
    content.setBounds (0, 0, gui::kWidth, gui::kHeight);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) gui::kWidth));
    if (constructed)
        proc.apvts.state.setProperty (kUiWidth, getWidth(), nullptr);
}

void SpaceEditor::Content::resized() { owner.layout(); }

void SpaceEditor::layout()
{
    using juce::Rectangle;
    const int m = 16;
    headerArea = { 0, 0, gui::kWidth, 58 };
    {
        auto pb = Rectangle<int> (360, 14, 360, 30);
        presetPrev.setBounds (pb.removeFromLeft (30));
        pb.removeFromLeft (4);
        presetNext.setBounds (pb.removeFromRight (30));
        pb.removeFromRight (4);
        presetName.setBounds (pb);
        auto ab = Rectangle<int> (gui::kWidth - m - 160, 14, 160, 30);
        slotA.setBounds (ab.removeFromLeft (34));
        ab.removeFromLeft (4);
        slotB.setBounds (ab.removeFromLeft (34));
        ab.removeFromLeft (8);
        slotCopy.setBounds (ab);
    }
    viewArea = { m, 68, gui::kWidth - 2 * m, 150 };
    view.setBounds (viewArea);
    const int top = 228, h = 288;
    reverbArea = { m, top, 590, h };
    delayArea = { m + 600, top, gui::kWidth - 2 * m - 600, h };
    bottomArea = { m, top + h + 10, gui::kWidth - 2 * m, gui::kHeight - (top + h + 10) - m };
    // REVERB: power + type on the title row, two rows of five knobs
    {
        auto a = reverbArea.reduced (12, 10);
        auto titleRow = a.removeFromTop (30);
        reverbOn.setBounds (titleRow.removeFromLeft (130));
        titleRow.removeFromLeft (10);
        type.setBounds (titleRow.removeFromLeft (360).withSizeKeepingCentre (360, 28));
        engine.setBounds (titleRow.withSizeKeepingCentre (juce::jmin (titleRow.getWidth(), 92), 26));
        a.removeFromTop (6);
        auto mini = a.removeFromBottom (30);
        const int kw = a.getWidth() / 6;
        auto row1 = a.removeFromTop (a.getHeight() / 2);
        for (auto* k : { &size, &decay, &bass, &preDelay, &damping, &early })
            k->setBounds (row1.removeFromLeft (kw));
        for (auto* k : { &modulation, &revLow, &revHigh, &width, &revLevel })
            k->setBounds (a.removeFromLeft (kw));
        // 2.0: PRE SYNC + its note, FREEZE
        preSync.setBounds (mini.removeFromLeft (110).withSizeKeepingCentre (104, 24));
        preNote.setBounds (mini.removeFromLeft (110).withSizeKeepingCentre (100, 24));
        freeze.setBounds (mini.removeFromRight (120).withSizeKeepingCentre (104, 24));
    }
    // DELAY: power + sync + note / time on the title row
    {
        auto a = delayArea.reduced (12, 10);
        auto titleRow = a.removeFromTop (30);
        delayOn.setBounds (titleRow.removeFromLeft (110));
        titleRow.removeFromLeft (8);
        sync.setBounds (titleRow.removeFromLeft (76).withSizeKeepingCentre (76, 26));
        titleRow.removeFromLeft (8);
        noteArea = titleRow.removeFromLeft (96).withSizeKeepingCentre (96, 26);
        note.setBounds (noteArea);
        titleRow.removeFromLeft (8);
        pingPong.setBounds (titleRow.withSizeKeepingCentre (juce::jmin (titleRow.getWidth(), 112), 26));
        a.removeFromTop (6);
        // 3.0: the character of the repeats on the bottom row (as the reverb's PRE SYNC / FREEZE row)
        auto mini = a.removeFromBottom (30);
        dlyMode.setBounds (mini.withSizeKeepingCentre (juce::jmin (mini.getWidth(), 300), 26));
        const int kw = a.getWidth() / 5;
        auto row1 = a.removeFromTop (a.getHeight() / 2);
        for (auto* k : { &time, &feedback, &drive, &wow, &dlyLevel })
            k->setBounds (row1.removeFromLeft (kw));
        for (auto* k : { &dlyLow, &dlyHigh, &diffuse, &toReverb })
            k->setBounds (a.removeFromLeft (kw));
    }
    // DUCKING + OUTPUT + meters
    {
        auto a = bottomArea.reduced (12, 8);
        auto meters = a.removeFromRight (100);
        inMeter.setBounds (meters.removeFromLeft (46));
        meters.removeFromLeft (8);
        outMeter.setBounds (meters.removeFromLeft (46));
        a.removeFromRight (10);
        auto duckZone = a.removeFromLeft (520);
        auto t = duckZone.removeFromLeft (120);
        essDuck.setBounds (t.withSizeKeepingCentre (112, 28));
        const int kw = duckZone.getWidth() / 4;
        for (auto* k : { &essDb, &voiceDuck, &release, &unmask })
            k->setBounds (duckZone.removeFromLeft (kw));
        a.removeFromLeft (20);
        const int ow = a.getWidth() / 2;
        mix.setBounds (a.removeFromLeft (ow));
        output.setBounds (a);
    }
    updateVisibility();
}

std::array<float, axs::Engine::kUnmaskBands> SpaceEditor::unmaskSnapshot() const
{
    std::array<float, axs::Engine::kUnmaskBands> a {};
    for (size_t b = 0; b < a.size(); ++b)
        a[b] = proc.unmaskNow[b].load (std::memory_order_relaxed);
    return a;
}

void SpaceEditor::updateVisibility()
{
    const bool s = proc.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    note.setVisible (s);
    time.setEnabled (! s);
    time.setAlpha (s ? 0.4f : 1.0f);
    // WOW moves the tape / the bucket brigade: nothing to move in CLEAN
    const bool clean = proc.apvts.getRawParameterValue ("dlyMode")->load() < 0.5f;
    wow.setEnabled (! clean);
    wow.setAlpha (clean ? 0.4f : 1.0f);
}

void SpaceEditor::Content::paint (juce::Graphics& g)
{
    using namespace juce;
    auto& e = owner;
    g.fillAll (col::bg);
    g.setColour (col::header);
    g.fillRect (e.headerArea);
    axell::brand::drawHeaderRule (g, (float) e.headerArea.getBottom() - 1.0f, 0.0f, (float) gui::kWidth, col::grid);
    // Axell's Dojo: the family emblem, its wave and dot in this plugin's colour
    axell::brand::drawEmblem (g, { 12.0f, 6.0f, 46.0f, 46.0f }, kSpaceAccent);
    axell::brand::drawVersion (g, axell::brand::drawTitle (g, 68.0f, 28.0f, "Space",
                                                           juce::String (juce::CharPointer_UTF8 ("\xe9\x9f\xbf"))), // 響
                               28.0f, AXELL_VERSION_STRING);
    g.setFont (ui::caps (10.0f, 1));
    g.setColour (col::dim);
    g.drawText ("REVERB  +  DELAY  FOR VOCALS", 67, 37, 300, 14, Justification::centredLeft);
    auto panel = [&g] (Rectangle<int> a, Colour accent, bool active)
    {
        const auto r = a.toFloat();
        g.setGradientFill (ColourGradient (col::panelTop, 0.0f, r.getY(), col::panel, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (accent.withAlpha (active ? 0.55f : 0.18f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.2f);
    };
    panel (e.reverbArea, col::teal, e.proc.apvts.getRawParameterValue ("reverbOn")->load() > 0.5f);
    panel (e.delayArea, col::accent, e.proc.apvts.getRawParameterValue ("delayOn")->load() > 0.5f);
    panel (e.bottomArea, col::global, true);
    g.setFont (ui::caps (9.5f, 1));
    g.setColour (col::faint);
    g.drawText ("DUCKING", e.bottomArea.getX() + 14, e.bottomArea.getY() + 4, 120, 12, Justification::centredLeft);
    g.drawText ("OUTPUT", e.bottomArea.getX() + 560, e.bottomArea.getY() + 4, 120, 12, Justification::centredLeft);
}

void SpaceEditor::timerCallback()
{
    view.set (proc.readParams(), proc.engine().lookAheadSamples(), proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0,
              proc.essNow.load(), proc.duckNow.load(), unmaskSnapshot().data());
    inMeter.push (proc.inPeak[0].load(), proc.inPeak[1].load(), 2);
    outMeter.push (proc.outPeak[0].load(), proc.outPeak[1].load(), 2);
    presetName.setButtonText (proc.getPresetName());
    slotA.setToggleState (proc.getActiveSlot() == 0, juce::dontSendNotification);
    slotB.setToggleState (proc.getActiveSlot() == 1, juce::dontSendNotification);
    slotCopy.setButtonText (proc.getActiveSlot() == 0 ? "COPY A>B" : "COPY B>A");
    updateVisibility();
    // the panels' outlines show REVERB / DELAY on or off: repaint them only when that changes
    const int onState = (proc.apvts.getRawParameterValue ("reverbOn")->load() > 0.5f ? 1 : 0)
                        | (proc.apvts.getRawParameterValue ("delayOn")->load() > 0.5f ? 2 : 0);
    if (onState != lastOnState)
    {
        lastOnState = onState;
        content.repaint();
    }
}

void SpaceEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    const auto& list = axs::factoryPresets();
    for (int i = 0; i < (int) list.size(); ++i)
        menu.addItem (1 + i, list[(size_t) i].name);
    juce::Component::SafePointer<SpaceEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetName), [safe] (int id) {
        if (safe != nullptr && id > 0)
            safe->proc.setCurrentProgram (id - 1);
    });
}
