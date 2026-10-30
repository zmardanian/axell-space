// research: the 2.0 reverb core alone - an impulse response and a held-tone response (wet only)
//   V2Ir out_ir.wav out_tone.wav type decay [bass damping mod size early]
#include "../Source/ReverbV2.h"
#include "WavIO.h"
#include <cstdlib>
#include <cmath>
int main (int argc, char** argv)
{
    if (argc < 5) return 1;
    const double fs = 48000.0;
    const int type = std::atoi (argv[3]);
    const float decay = (float) std::atof (argv[4]);
    const float bass = argc > 5 ? (float) std::atof (argv[5]) : 1.0f, damp = argc > 6 ? (float) std::atof (argv[6]) : 40.0f;
    const float mod = argc > 7 ? (float) std::atof (argv[7]) : 50.0f, size = argc > 8 ? (float) std::atof (argv[8]) : 50.0f;
    const float early = argc > 9 ? (float) std::atof (argv[9]) : 50.0f;
    for (int which = 0; which < 2; ++which)
    {
        axs::v2::Reverb r;
        r.prepare (fs);
        r.configure (type, size);
        r.setDecay (decay, bass, damp, mod, false);
        r.setEarly (early);
        r.setPreDelay (0);
        wav::Audio a;
        a.fs = fs;
        const size_t n = (size_t) ((which ? 4.0 : 6.0) * fs);
        a.ch.assign (2, std::vector<float> (n, 0.0f));
        for (size_t i = 0; i < n; ++i)
        {
            float in = 0.0f;
            if (which == 0) in = i == (size_t) (0.1 * fs) ? 0.5f : 0.0f;
            else in = i < (size_t) (2.0 * fs) ? (float) (0.1 * std::sin (2.0 * 3.14159265358979 * 1000.0 * (double) i / fs)) : 0.0f;
            float l, rr;
            r.process (in, in, l, rr);
            a.ch[0][i] = l;
            a.ch[1][i] = rr;
        }
        wav::write (argv[1 + which], a);
    }
    return 0;
}
