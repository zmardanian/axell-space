// SpaceRender - the shipping engine on a file (research and measurement).
//   SpaceRender in.wav out.wav [key=value ...]
//   SpaceRender --impulse out.wav seconds fs [key=value ...]   (a unit impulse in: the impulse response, wet only by default)
// keys: reverb type size decay predelay damping early mod revlow revhigh width revdb
//       delay sync note time feedback pingpong dlylow dlyhigh drive dlydb dlytorev
//       essduck essdb voiceduck release mix output bpm block
#include "../Source/Engine.h"
#include "../Source/Presets.h"
#include "WavIO.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void setKey (axs::Params& p, const std::string& k, double v, int& block)
{
    if (k == "reverb") p.reverbOn = v != 0;
    else if (k == "type") p.type = (int) v;
    else if (k == "size") p.size = (float) v;
    else if (k == "decay") p.decay = (float) v;
    else if (k == "predelay") p.preDelayMs = (float) v;
    else if (k == "damping") p.damping = (float) v;
    else if (k == "early") p.early = (float) v;
    else if (k == "mod") p.modulation = (float) v;
    else if (k == "revlow") p.revLowCut = (float) v;
    else if (k == "revhigh") p.revHighCut = (float) v;
    else if (k == "width") p.width = (float) v;
    else if (k == "revdb") p.reverbDb = (float) v;
    else if (k == "delay") p.delayOn = v != 0;
    else if (k == "sync") p.sync = v != 0;
    else if (k == "note") p.note = (int) v;
    else if (k == "time") p.timeMs = (float) v;
    else if (k == "feedback") p.feedback = (float) v;
    else if (k == "pingpong") p.pingPong = v != 0;
    else if (k == "dlylow") p.dlyLowCut = (float) v;
    else if (k == "dlyhigh") p.dlyHighCut = (float) v;
    else if (k == "drive") p.drive = (float) v;
    else if (k == "dlydb") p.delayDb = (float) v;
    else if (k == "dlytorev") p.delayToReverb = (float) v;
    else if (k == "essduck") p.essDuck = v != 0;
    else if (k == "essdb") p.essDuckDb = (float) v;
    else if (k == "voiceduck") p.voiceDuckDb = (float) v;
    else if (k == "release") p.duckReleaseMs = (float) v;
    else if (k == "mix") p.mix = (float) v;
    else if (k == "output") p.outputDb = (float) v;
    else if (k == "bpm") p.bpm = v;
    else if (k == "engine") p.engine = (int) v;
    else if (k == "bass") p.bass = (float) v;
    else if (k == "freeze") p.freeze = v != 0;
    else if (k == "presync") p.preSync = v != 0;
    else if (k == "prenote") p.preNote = (int) v;
    else if (k == "dlymode") p.dlyMode = (int) v;
    else if (k == "wow") p.wowFlutter = (float) v;
    else if (k == "diffuse") p.diffuse = (float) v;
    else if (k == "unmask") p.unmask = (float) v;
    else if (k == "block") block = (int) v;
    else std::fprintf (stderr, "unknown key %s\n", k.c_str());
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: SpaceRender in.wav out.wav [key=value ...] | --impulse out.wav seconds fs [key=value ...]\n");
        return 1;
    }
    axs::Params p;
    int block = 512, first = 3;
    wav::Audio a;
    const bool impulse = std::strcmp (argv[1], "--impulse") == 0;
    if (impulse)
    {
        if (argc < 5)
            return 1;
        a.fs = std::atof (argv[4]);
        a.ch.assign (2, std::vector<float> ((size_t) (std::atof (argv[3]) * a.fs), 0.0f));
        a.ch[0][0] = a.ch[1][0] = 1.0f;
        p.mix = 100.0f;      // wet only: the impulse response
        p.essDuck = false;   // a click is not an ess (the network would see none anyway)
        first = 5;
    }
    else if (! wav::read (argv[1], a))
    {
        std::fprintf (stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    for (int i = first; i < argc; ++i)
    {
        const std::string s = argv[i];
        const auto eq = s.find ('=');
        if (eq == std::string::npos)
            continue;
        if (s.substr (0, eq) == "preset")   // a factory preset (by index) as the starting point; later keys change it
        {
            const int k = std::atoi (s.c_str() + eq + 1);
            if (k >= 0 && k < (int) axs::factoryPresets().size())
                p = axs::factoryPresets()[(size_t) k].p;
            continue;
        }
        setKey (p, s.substr (0, eq), std::atof (s.c_str() + eq + 1), block);
    }
    if (a.ch.size() == 1)
        a.ch.push_back (a.ch[0]);
    axs::Engine e;
    e.setParams (p);
    e.prepare (a.fs, block);
    e.setParams (p);
    const size_t n = a.frames();
    for (size_t pos = 0; pos < n; pos += (size_t) block)
    {
        const int m = (int) std::min<size_t> ((size_t) block, n - pos);
        float* io[2] = { a.ch[0].data() + pos, a.ch[1].data() + pos };
        e.process (io, 2, m);
    }
    return wav::write (impulse ? argv[2] : argv[2], a) ? 0 : 1;
}
