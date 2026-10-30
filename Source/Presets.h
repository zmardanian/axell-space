#pragma once
// Axell Space - factory presets: complete settings (every parameter), so a preset always sounds the same.
#include "Engine.h"

#include <vector>

namespace axs
{
struct Preset
{
    const char* name;
    const char* description;
    Params p;
};

namespace detail
{
inline Params reverb (int type, float decay, float preMs, float damping, float mix, float size = 50.0f, float early = 50.0f,
                      float lowCut = 150.0f, float highCut = 12000.0f)
{
    Params p;
    p.reverbOn = true;
    p.delayOn = false;
    p.type = type;
    p.decay = decay;
    p.preDelayMs = preMs;
    p.damping = damping;
    p.mix = mix;
    p.size = size;
    p.early = early;
    p.revLowCut = lowCut;
    p.revHighCut = highCut;
    p.modulation = 50.0f;
    return p;
}
inline Params delay (Params p, bool sync, int note, float timeMs, float feedback, bool ping, float lowCut, float highCut,
                     float drive = 0.0f, float toReverb = 0.0f)
{
    p.delayOn = true;
    p.sync = sync;
    p.note = note;
    p.timeMs = timeMs;
    p.feedback = feedback;
    p.pingPong = ping;
    p.dlyLowCut = lowCut;
    p.dlyHighCut = highCut;
    p.drive = drive;
    p.delayToReverb = toReverb;
    return p;
}
inline Params noReverb (Params p) { p.reverbOn = false; return p; }
inline Params duck (Params p, float voiceDb, float releaseMs = 300.0f) { p.voiceDuckDb = voiceDb; p.duckReleaseMs = releaseMs; return p; }
inline Params mix (Params p, float m) { p.mix = m; return p; }
// 3.0
inline Params character (Params p, int mode, float wow, float diffuse) { p.dlyMode = mode; p.wowFlutter = wow; p.diffuse = diffuse; return p; }
inline Params unmasked (Params p, float pct) { p.unmask = pct; return p; }
} // namespace detail

inline const std::vector<Preset>& factoryPresets()
{
    using namespace detail;
    static const std::vector<Preset> list {
        { "Init", "A medium plate at 20 %, esses kept out of it.", Params {} },
        { "Vocal Plate", "The classic rap-vocal plate: 1.4 s, 30 ms pre-delay so the words stay in front, esses kept out, "
          "stepping back where the voice sits (UNMASK).",
          unmasked (duck (reverb (kPlate, 1.4f, 30.0f, 40.0f, 20.0f), 3.0f), 40.0f) },
        { "Booth Air", "Just enough room to take the dryness off a close, dead vocal: a short booth, 15 %.",
          reverb (kBooth, 0.45f, 0.0f, 30.0f, 15.0f, 40.0f, 70.0f, 200.0f, 11000.0f) },
        { "Small Room", "A natural small room around the voice: early reflections first, a short tail.",
          reverb (kRoom, 0.8f, 8.0f, 45.0f, 18.0f, 45.0f, 65.0f) },
        { "Dark Plate", "A long, dark plate for slow, moody verses: the highs die fast, nothing sizzles.",
          reverb (kPlate, 2.2f, 40.0f, 80.0f, 20.0f, 55.0f, 50.0f, 180.0f, 6500.0f) },
        { "Hall Hook", "A big hall for hooks and choruses: 2.4 s, ducked under the voice, blooms in the gaps.",
          unmasked (duck (reverb (kHall, 2.4f, 40.0f, 50.0f, 22.0f, 60.0f, 45.0f), 6.0f, 400.0f), 40.0f) },
        { "Slap Back", "One short slap (110 ms) off a tape echo - darker, softer, a little wow - a classic for rap leads.",
          character (mix (noReverb (delay (Params {}, false, k1_8, 110.0f, 0.0f, false, 300.0f, 5000.0f, 15.0f)), 18.0f), 1, 30.0f, 0.0f) },
        { "1/4 Throw", "A quarter-note throw for the end of a line: ducked while you rap, it answers in the gap, "
          "with a little plate behind it.",
          unmasked (mix (duck (delay (reverb (kPlate, 1.2f, 20.0f, 50.0f, 20.0f), true, k1_4, 500.0f, 35.0f, false, 300.0f, 5500.0f, 10.0f, 30.0f), 10.0f, 250.0f), 25.0f), 40.0f) },
        { "1/8 Ping-Pong", "Eighth-note repeats bouncing left and right, ducked under the voice.",
          mix (duck (noReverb (delay (Params {}, true, k1_8, 250.0f, 30.0f, true, 350.0f, 6000.0f)), 8.0f), 18.0f) },
        { "Dotted 1/8 Space", "Dotted-eighth repeats into a hall: wide, rhythmic space for melodic hooks.",
          unmasked (mix (duck (delay (reverb (kHall, 2.0f, 30.0f, 50.0f, 20.0f), true, k1_8D, 375.0f, 40.0f, false, 250.0f, 6000.0f, 0.0f, 40.0f), 6.0f, 350.0f), 24.0f), 40.0f) },
        { "Ad-lib Tail", "Big tails for ad-libs: a long hall and quarter repeats, pushed back under the lead.",
          unmasked (mix (duck (delay (reverb (kHall, 3.0f, 60.0f, 55.0f, 30.0f, 70.0f), true, k1_4, 500.0f, 45.0f, true, 300.0f, 5000.0f, 20.0f, 35.0f), 4.0f), 35.0f), 50.0f) },
        { "Send: Vocal Plate (100 % wet)", "For a send / aux track: the Vocal Plate, wet only.",
          unmasked (mix (duck (reverb (kPlate, 1.4f, 30.0f, 40.0f, 100.0f), 3.0f), 100.0f), 40.0f) },
        { "Send: 1/4 Delay (100 % wet)", "For a send / aux track: ducked quarter-note repeats, wet only.",
          unmasked (mix (duck (noReverb (delay (Params {}, true, k1_4, 500.0f, 35.0f, false, 300.0f, 5500.0f, 10.0f)), 10.0f, 250.0f), 100.0f), 40.0f) },
        // 3.0
        { "Tape Echo", "Dotted-eighth repeats off a tape echo for melodic lines: each one darker and softer than the last, a little "
          "wow, stepping back from your words.",
          unmasked (character (mix (duck (noReverb (delay (Params {}, true, k1_8D, 375.0f, 40.0f, false, 250.0f, 6500.0f, 20.0f)), 4.0f), 22.0f), 1, 45.0f, 15.0f), 40.0f) },
        { "Analog Dub", "Dub-style quarter repeats from a bucket-brigade line: dark, soft, smearing and melting into a hall.",
          unmasked (character (mix (duck (delay (reverb (kHall, 2.2f, 20.0f, 55.0f, 18.0f), true, k1_4, 500.0f, 55.0f, false, 200.0f, 8000.0f, 10.0f, 30.0f), 6.0f, 300.0f), 28.0f), 2, 40.0f, 35.0f), 50.0f) },
        { "Diffuse Trail", "Eighth repeats that smear into a soft trail - between a delay and a reverb - over a small plate.",
          unmasked (character (mix (duck (delay (reverb (kPlate, 1.6f, 20.0f, 45.0f, 15.0f), true, k1_8, 250.0f, 50.0f, false, 250.0f, 7000.0f), 3.0f), 24.0f), 0, 40.0f, 75.0f), 40.0f) },
        { "Unmasked Hall", "A big hall that stays out of the words: it steps back where the voice sits, in the bands it uses "
          "(UNMASK 100 %), and ducks only lightly - the air and the body stay.",
          unmasked (duck (reverb (kHall, 2.8f, 30.0f, 50.0f, 30.0f, 65.0f, 45.0f), 4.0f, 500.0f), 100.0f) },
    };
    return list;
}
} // namespace axs
