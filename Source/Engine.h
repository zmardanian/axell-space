#pragma once
// Axell Space - the engine (framework independent, no JUCE): reverb + tempo-synced delay for vocals.
//
//   dry ─────────────────────────────────────────────────────────────────────────────┐
//   in ─ D (the ess look-ahead, 8 ms) ─ ESS DUCK ─┬─ PRE-DELAY ─ early reflections ─┐  │
//                                                 │             └ diffusion ─ FDN ──┼─ VOICE DUCK ─ MIX ─ OUTPUT
//                                                 └─ DELAY (sync, feedback, filters, ping-pong) ─(into reverb)─┘
//
// REVERB: a 16-line feedback delay network (Jot / Chaigne): mutually prime line lengths spread over the type's span,
// a fast Walsh-Hadamard mixing matrix (lossless, every line feeds every other), one absorbing one-pole per line that
// sets the mid decay (RT60) and a shorter high decay (DAMPING), slow modulation of every line (each at its own rate)
// against metallic ringing, input diffusion (Schroeder allpasses), L / R taken with orthogonal sign patterns (decorrelated), WIDTH by
// mid / side. Early reflections for BOOTH / ROOM / HALL from an image-source shoebox model (computed in prepare()),
// none for PLATE (dense from the start).
// DELAY: stereo delay lines, the time synced to the host tempo or free; feedback through a band-pass (LOW / HIGH CUT)
// and a soft saturation (DRIVE); PING-PONG alternates the sides; part of it can feed the reverb.
// ESS DUCK: the suite's sibilance network (axell-core SibilanceDetector, causal, the same as Axell Velvet's LIVE and
// Duo's ESS HOLD) marks esses; the wet path runs D = the detector's delay behind the input, so an ess is turned down
// before it reaches the reverb or the delay - the dry signal is untouched and has no latency.
// VOICE DUCK: the wet output is turned down while the voice is loud (re its own running peak) and comes back up in
// the gaps.
// Real-time safe after prepare(): no allocation, locks or I/O in process().
#include <axell/dsp/SibilanceDetector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#if defined(__SSE__) || defined(_M_X64)
 #include <xmmintrin.h>
 #define AXS_FTZ 1
#endif

namespace axs
{
constexpr double kPi = 3.14159265358979323846;

enum RevType : int { kBooth = 0, kRoom = 1, kPlate = 2, kHall = 3 };
enum Note : int { k1_16 = 0, k1_8T, k1_8, k1_8D, k1_4T, k1_4, k1_4D, k1_2, k1_1, kNumNotes };
inline double noteBeats (int n) noexcept
{
    static constexpr double b[kNumNotes] = { 0.25, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 };
    return b[std::clamp (n, 0, kNumNotes - 1)];
}

struct Params
{
    // REVERB
    bool reverbOn = true;
    int type = kPlate;
    float size = 50.0f;          // % (scales the room / the line lengths)
    float decay = 1.2f;          // s, RT60 at mid frequencies (0.2 .. 10)
    float preDelayMs = 20.0f;    // ms after the 8 ms look-ahead (0 .. 250)
    float damping = 40.0f;       // %: 0 = highs decay like mids, 100 = highs decay 5x faster
    float early = 50.0f;         // % early reflections re the tail (BOOTH / ROOM / HALL)
    float modulation = 50.0f;    // % (50: the tail as colourless as a commercial reverb, 7 cents of wobble at most)
    float revLowCut = 150.0f;    // Hz, into the reverb
    float revHighCut = 12000.0f; // Hz, into the reverb
    float width = 100.0f;        // %
    float reverbDb = 0.0f;       // level of the reverb in the wet mix
    // DELAY
    bool delayOn = false;
    bool sync = true;
    int note = k1_4;
    float timeMs = 250.0f;       // free time (1 .. 2000)
    float feedback = 30.0f;      // %
    bool pingPong = false;
    float dlyLowCut = 250.0f;    // Hz, in the feedback loop
    float dlyHighCut = 6000.0f;  // Hz, in the feedback loop
    float drive = 0.0f;          // %, saturation of the repeats
    float delayDb = 0.0f;        // level of the delay in the wet mix
    float delayToReverb = 0.0f;  // %, the delay's output sent into the reverb
    // DUCKING
    bool essDuck = true;
    float essDuckDb = 12.0f;     // how far an ess is turned down before the wet path
    float voiceDuckDb = 0.0f;    // how far the wet output is turned down while the voice is loud
    float duckReleaseMs = 300.0f;
    // OUTPUT
    float mix = 20.0f;           // %: dry stays at full level up to 50 %, the wet at full level from 50 %
    float outputDb = 0.0f;
    double bpm = 120.0;          // the host's tempo (the processor passes it in)
};

//== building blocks =================================================================================================
// one-pole low / high pass and RBJ biquads
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0.0f; }
    void set (double fs, double f, bool high, double q = 0.7071)
    {
        f = std::clamp (f, 10.0, 0.45 * fs);
        const double w = 2.0 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        if (high)
        {
            b0 = (float) ((1.0 + c) / 2.0 / a0); b1 = (float) (-(1.0 + c) / a0); b2 = b0;
        }
        else
        {
            b0 = (float) ((1.0 - c) / 2.0 / a0); b1 = (float) ((1.0 - c) / a0); b2 = b0;
        }
        a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - al) / a0);
    }
};

// a delay line read with 4-point (cubic Lagrange) interpolation
struct Line
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    void prepare (int maxSamples)
    {
        int n = 1;
        while (n < maxSamples + 4)
            n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        w = 0;
    }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
    float tap (int d) const noexcept { return buf[(size_t) ((w - 1 - d) & mask)]; }  // d = 0: the newest sample
    float read (double d) const noexcept
    {
        const int i = (int) d;
        const float t = (float) (d - i);
        const float ym1 = tap (i - 1), y0 = tap (i), y1 = tap (i + 1), y2 = tap (i + 2);
        // 4-point Lagrange, centred on y0 .. y1
        const float c0 = y0, c1 = y1 - ym1 * (1.0f / 3.0f) - y0 * 0.5f - y2 * (1.0f / 6.0f);
        const float c2 = 0.5f * (ym1 + y1) - y0, c3 = (1.0f / 6.0f) * (y2 - ym1) + 0.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }
};

// Schroeder allpass (diffusion)
struct Allpass
{
    Line line;
    int d = 1;
    float g = 0.6f;
    float process (float x) noexcept
    {
        const float v = line.tap (d - 1);
        const float u = x + g * v;
        line.push (u);
        return v - g * u;
    }
};

//== the reverb =====================================================================================================
class Reverb
{
public:
    static constexpr int N = 16;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const int maxLine = (int) std::ceil (0.25 * fs);          // the longest line at SIZE 100 % (HALL) + modulation
        for (auto& l : lines)
            l.prepare (maxLine);
        for (auto& a : diff)
            a.line.prepare ((int) std::ceil (0.03 * fs));
        pre.prepare ((int) std::ceil (0.30 * fs));
        erLine.prepare ((int) std::ceil (0.12 * fs));
        for (auto& a : erDiff)
            a.line.prepare ((int) std::ceil (0.004 * fs));
        reset();
        configured = false;
    }

    void reset() noexcept
    {
        for (auto& l : lines)
            l.reset();
        for (auto& a : diff)
            a.line.reset();
        pre.reset();
        erLine.reset();
        for (auto& a : erDiff)
            a.line.reset();
        absState.fill (0.0f);
        lfoPhase = 0.0;
    }

    // (re)builds the type-dependent parts; called when TYPE or SIZE change (SIZE also glides the line lengths)
    void configure (int type, float sizePct)
    {
        curType = std::clamp (type, 0, 3);
        // the span of the line lengths per type (ms, at SIZE 100 %); a SIZE of 0 % is a quarter of it
        static constexpr double lo[4] = { 3.0, 6.0, 9.0, 22.0 }, hi[4] = { 22.0, 45.0, 60.0, 140.0 };
        const double s = 0.25 + 0.75 * std::clamp (sizePct, 0.0f, 100.0f) / 100.0;
        for (int i = 0; i < N; ++i)
        {
            // exponentially spread, then the nearest prime (mutually prime lengths: no common echoes)
            const double ms = lo[curType] * std::pow (hi[curType] / lo[curType], (double) i / (N - 1)) * s;
            targetLen[(size_t) i] = (double) nearestPrime ((int) std::lround (ms * 0.001 * fs));
            if (! configured)
                curLen[(size_t) i] = targetLen[(size_t) i];
        }
        // input diffusion: four allpasses (PLATE / HALL dense, BOOTH / ROOM lighter)
        static constexpr double dms[kDiff] = { 1.531, 2.213, 3.595, 4.771, 7.307, 9.734 };
        const float gd = curType == kPlate ? 0.75f : (curType == kHall ? 0.72f : 0.65f);
        for (int k = 0; k < kDiff; ++k)
        {
            diff[(size_t) k].d = std::max (1, (int) std::lround (dms[k] * 0.001 * fs * (curType == kBooth ? 0.5 : 1.0)));
            diff[(size_t) k].g = gd;
        }
        // early-reflection diffusion: each reflection becomes a short dense burst, as off a real wall (L and R differ)
        static constexpr double eMs[6] = { 0.71, 1.13, 1.79, 0.83, 1.31, 1.97 };
        for (int k = 0; k < 6; ++k)
        {
            erDiff[(size_t) k].d = std::max (1, (int) std::lround (eMs[k] * 0.001 * fs));
            erDiff[(size_t) k].g = 0.62f;
        }
        buildEarly (sizePct);
        configured = true;
    }

    // per block: decay and damping -> the absorbing filters; modulation depth
    void setDecay (float rt60, float dampingPct, float modPct) noexcept
    {
        const double rt = std::clamp ((double) rt60, 0.1, 30.0);
        // DAMPING is defined at 8 kHz (the same at every sample rate): 100 % = the 8 kHz decay twice as fast
        const double rtHigh = rt / (1.0 + std::clamp ((double) dampingPct, 0.0, 100.0) / 100.0);
        const double wRef = 2.0 * kPi * std::min (8000.0, 0.4 * fs) / fs, cRef = std::cos (wRef);
        for (int i = 0; i < N; ++i)
        {
            const double L = curLen[(size_t) i];
            const double gLow = std::pow (10.0, -3.0 * L / (rt * fs));      // per pass: -60 dB after RT60
            const double gHigh = std::pow (10.0, -3.0 * L / (rtHigh * fs)); // per pass at 8 kHz
            // one-pole y = a x + b y1 with a = gLow (1 - b): DC gain gLow; |H| at 8 kHz = gHigh, solved exactly:
            // (1 - b)^2 = r (1 - 2 b cos w + b^2), r = (gHigh / gLow)^2
            const double r = (gHigh / gLow) * (gHigh / gLow);
            double b = 0.0;
            if (r < 1.0 - 1.0e-12)
            {
                const double A = 1.0 - r, B = 1.0 - r * cRef;
                b = (B - std::sqrt (std::max (0.0, B * B - A * A))) / A;
            }
            absA[(size_t) i] = (float) (gLow * (1.0 - b));
            absB[(size_t) i] = (float) b;
        }
        modDepth = 0.0020 * fs * std::clamp ((double) modPct, 0.0, 100.0) / 100.0;   // up to 2 ms, at a slow rate
    }

    void setPreDelay (float ms) noexcept { preSamples = std::clamp ((int) std::lround (ms * 0.001 * fs), 0, (int) pre.mask - 4); }
    void setEarly (float pct) noexcept { earlyGain = curType == kPlate ? 0.0f : std::clamp (pct, 0.0f, 100.0f) / 100.0f; }

    // one stereo sample in (already filtered), one stereo sample out (wet only)
    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        pre.push (0.5f * (inL + inR));
        const float x = pre.tap (preSamples);
        // early reflections: taps of the pre-delayed input
        erLine.push (x);
        float eL = 0.0f, eR = 0.0f;
        for (int k = 0; k < erCount; ++k)
        {
            const float v = erLine.tap (erDelay[(size_t) k]);
            eL += erGainL[(size_t) k] * v;
            eR += erGainR[(size_t) k] * v;
        }
        for (int k = 0; k < 3; ++k)
        {
            eL = erDiff[(size_t) k].process (eL);
            eR = erDiff[(size_t) (k + 3)].process (eR);
        }
        // diffusion into the tank
        float d = x;
        for (auto& a : diff)
            d = a.process (d);
        // read the lines (four modulated, the others glide to their SIZE length)
        std::array<float, N> v {};
        lfoPhase += lfoInc;
        if (lfoPhase > 2.0 * kPi)
            lfoPhase -= 2.0 * kPi;
        for (int i = 0; i < N; ++i)
        {
            curLen[(size_t) i] += 0.0005 * (targetLen[(size_t) i] - curLen[(size_t) i]);
            // every line moves a little, each at its own rate and phase: the modes never line up long enough to ring
            const double len = curLen[(size_t) i] + modDepth * modScale[(size_t) i] * std::sin (lfoPhase * modRate[(size_t) i] + 1.3 * i);
            v[(size_t) i] = lines[(size_t) i].read (std::max (1.0, len - 1.0));
        }
        // outputs before the mixing: orthogonal sign patterns for L and R
        float lateL = 0.0f, lateR = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            lateL += (((i >> 0) ^ (i >> 2)) & 1 ? -1.0f : 1.0f) * v[(size_t) i];
            lateR += (((i >> 1) ^ (i >> 3)) & 1 ? -1.0f : 1.0f) * v[(size_t) i];
        }
        // absorption, then the lossless Hadamard mixing (normalised: 1 / sqrt(N)), then the input in
        for (int i = 0; i < N; ++i)
        {
            float& z = absState[(size_t) i];
            z = absA[(size_t) i] * v[(size_t) i] + absB[(size_t) i] * z;
            v[(size_t) i] = z;
        }
        hadamard (v);
        for (int i = 0; i < N; ++i)
            lines[(size_t) i].push (v[(size_t) i] * 0.25f + (i & 1 ? -d : d) * 0.25f);
        const float lateGain = 0.35f;
        outL = earlyGain * eL + lateGain * lateL;
        outR = earlyGain * eR + lateGain * lateR;
    }

    double lineLength (int i) const noexcept { return curLen[(size_t) i]; }

private:
    static void hadamard (std::array<float, N>& v) noexcept
    {
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = v[(size_t) j], b = v[(size_t) (j + h)];
                    v[(size_t) j] = a + b;
                    v[(size_t) (j + h)] = a - b;
                }
    }
    static int nearestPrime (int n)
    {
        auto prime = [] (int k) {
            if (k < 2)
                return false;
            for (int q = 2; q * q <= k; ++q)
                if (k % q == 0)
                    return false;
            return true;
        };
        for (int o = 0;; ++o)
        {
            if (prime (n + o))
                return n + o;
            if (n - o > 1 && prime (n - o))
                return n - o;
        }
    }
    // image-source shoebox: first- and second-order reflections, two ear positions 17 cm apart
    void buildEarly (float sizePct)
    {
        erCount = 0;
        if (curType == kPlate)
            return;
        static constexpr double dims[4][3] = { { 1.8, 1.5, 2.2 }, { 4.2, 3.6, 2.6 }, { 0, 0, 0 }, { 16.0, 11.0, 7.0 } };
        const double s = 0.6 + 0.8 * std::clamp (sizePct, 0.0f, 100.0f) / 100.0;
        const double Lx = dims[curType][0] * s, Ly = dims[curType][1] * s, Lz = dims[curType][2] * s;
        const double src[3] = { 0.35 * Lx, 0.55 * Ly, 0.6 * Lz }, rcv[3] = { 0.62 * Lx, 0.45 * Ly, 0.55 * Lz };
        const double refl = curType == kBooth ? 0.55 : (curType == kRoom ? 0.75 : 0.8);
        const double direct = std::sqrt ((src[0] - rcv[0]) * (src[0] - rcv[0]) + (src[1] - rcv[1]) * (src[1] - rcv[1])
                                         + (src[2] - rcv[2]) * (src[2] - rcv[2]));
        for (int nx = -3; nx <= 3 && erCount < kMaxEr; ++nx)
            for (int ny = -3; ny <= 3 && erCount < kMaxEr; ++ny)
                for (int nz = -2; nz <= 2 && erCount < kMaxEr; ++nz)
                {
                    const int order = std::abs (nx) + std::abs (ny) + std::abs (nz);
                    if (order == 0 || order > 3)
                        continue;
                    auto img = [] (int n, double p, double L) { return (n % 2 == 0) ? n * L + p : n * L + (L - p); };
                    const double ix = img (nx, src[0], Lx), iy = img (ny, src[1], Ly), iz = img (nz, src[2], Lz);
                    double gs[2] {};
                    int delay = 0;
                    for (int ear = 0; ear < 2; ++ear)
                    {
                        const double ex = rcv[0] + (ear ? 0.085 : -0.085);
                        const double r = std::sqrt ((ix - ex) * (ix - ex) + (iy - rcv[1]) * (iy - rcv[1]) + (iz - rcv[2]) * (iz - rcv[2]));
                        // delay re the direct sound (the reverb is wet only); 1/r spreading, the walls' reflection per bounce
                        gs[ear] = std::pow (refl, order) * direct / r;
                        if (ear == 0)
                            delay = (int) std::lround ((r - direct) / 343.0 * fs);
                    }
                    if (delay < 1 || delay >= erLine.mask - 4)
                        continue;
                    erDelay[(size_t) erCount] = delay;
                    erGainL[(size_t) erCount] = (float) (0.55 * gs[0]);
                    erGainR[(size_t) erCount] = (float) (0.55 * gs[1] * ((erCount & 1) ? -1.0 : 1.0));
                    ++erCount;
                }
    }

    static constexpr int kMaxEr = 128;
    double fs = 48000.0;
    std::array<Line, N> lines;
    static constexpr int kDiff = 6;
    std::array<Allpass, kDiff> diff;
    Line pre, erLine;
    std::array<Allpass, 6> erDiff;
    std::array<double, N> targetLen {}, curLen {};
    std::array<float, N> absA {}, absB {}, absState {};
    // per-line modulation: rates spread 0.6 .. 1.9 x the base rate (no common period), depths 0.4 .. 1.0
    std::array<double, N> modRate { 1.00, 1.37, 0.61, 1.83, 0.79, 1.52, 1.11, 0.68, 1.66, 0.93, 1.29, 0.74, 1.91, 1.04, 0.86, 1.47 };
    std::array<double, N> modScale { 1.0, 0.6, 0.9, 0.5, 0.8, 0.7, 0.4, 1.0, 0.6, 0.9, 0.5, 0.8, 0.7, 0.4, 1.0, 0.6 };
    std::array<int, kMaxEr> erDelay {};
    std::array<float, kMaxEr> erGainL {}, erGainR {};
    int erCount = 0, preSamples = 0, curType = kPlate;
    float earlyGain = 0.5f;
    double modDepth = 0.0, lfoPhase = 0.0, lfoInc = 2.0 * kPi * 0.6 / 48000.0;
    bool configured = false;

public:
    void setLfoRate (double hz) noexcept { lfoInc = 2.0 * kPi * hz / fs; }
};

//== the delay ======================================================================================================
class Delay
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& l : lines)
            l.prepare ((int) std::ceil (4.2 * fs));
        reset();
    }
    void reset() noexcept
    {
        for (auto& l : lines)
            l.reset();
        for (auto* fs4 : { &lc, &hc, &olc, &ohc })
            for (auto& f : *fs4)
                f.reset();
        curT = -1.0;
    }
    // period = the musical delay time; offset = how late the input already arrives (the engine's look-ahead): the
    // first repeat is read `period - offset` after the input, every further one `period` after the previous one
    void set (double periodSamples, double offsetSamples, float feedbackPct, bool pp, float lowCut, float highCut, float drivePct) noexcept
    {
        targetT = std::clamp (periodSamples, offsetSamples + 1.0, (double) lines[0].mask - 8.0);
        off = offsetSamples;
        if (curT < 0.0)
            curT = targetT;
        fb = std::clamp (feedbackPct, 0.0f, 98.0f) / 100.0f;
        ping = pp;
        for (int c = 0; c < 2; ++c)
        {
            lc[(size_t) c].set (fs, lowCut, true);
            hc[(size_t) c].set (fs, highCut, false);
            olc[(size_t) c].set (fs, lowCut, true);
            ohc[(size_t) c].set (fs, highCut, false);
        }
        drive = std::clamp (drivePct, 0.0f, 100.0f) / 100.0f;
    }
    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        curT += 0.0007 * (targetT - curT);       // a time change glides (tape-like), never clicks
        // the loop: w(n) = in(n) + fb * filtered(w(n - T)); heard: w(n - (T - offset)), filtered alike
        const float fL = lines[0].read (curT - 1.0), fR = lines[1].read (curT - 1.0);
        const float sL = shape (hc[0].process (lc[0].process (fL))), sR = shape (hc[1].process (lc[1].process (fR)));
        if (ping)
        {
            // PING-PONG: the input enters on the left, every repeat crosses to the other side
            lines[0].push (0.5f * (inL + inR) + fb * sR);
            lines[1].push (fb * sL);
        }
        else
        {
            lines[0].push (inL + fb * sL);
            lines[1].push (inR + fb * sR);
        }
        const double tOut = std::max (1.0, curT - off);
        outL = shape (ohc[0].process (olc[0].process (lines[0].read (tOut)))) ;
        outR = shape (ohc[1].process (olc[1].process (lines[1].read (tOut))));
    }

private:
    float shape (float x) const noexcept
    {
        if (drive <= 0.0f)
            return x;
        const float k = 1.0f + 6.0f * drive;
        return std::tanh (k * x) / k;
    }
    double fs = 48000.0, targetT = 1000.0, curT = -1.0, off = 0.0;
    std::array<Line, 2> lines;
    std::array<Biquad, 2> lc, hc, olc, ohc;
    float fb = 0.3f, drive = 0.0f;
    bool ping = false;
};

//== the engine =====================================================================================================
class Engine
{
public:
    void prepare (double sampleRate, int /*maxBlock*/ = 0)
    {
        fs = sampleRate;
        ess.prepare (fs, axell::voice::DetectorConfig::live());
        look = ess.frameDelay() + ess.hopSize();             // the ess decision for a sample is known this late
        for (auto& l : lookLine)
            l.prepare (look + 8);
        rev.prepare (fs);
        dly.prepare (fs);
        prepared = true;
        reset();
        apply (params, true);
    }

    void reset() noexcept
    {
        for (auto& l : lookLine)
            l.reset();
        rev.reset();
        dly.reset();
        ess.reset();
        for (auto& f : revLc)
            f.reset();
        for (auto& f : revHc)
            f.reset();
        essP = 0.0f;
        essGain = 1.0f;
        env = 0.0f;
        peakDb = -60.0f;
        duckGain = 1.0f;
        typeFade = 1.0f;
        pendingType = -1;
        sample = 0;
    }

    void setParams (const Params& p) noexcept
    {
        const bool first = ! prepared;
        params = p;
        if (prepared && ! first)
            apply (params, false);
    }
    const Params& getParams() const noexcept { return params; }
    int lookAheadSamples() const noexcept { return look; }   // the wet path's built-in pre-delay (the dry has none)
    float essProbability() const noexcept { return essP; }
    // tests only: force the ess decision (-1 = the network decides, 0 = never an ess, 1 = always)
    void forceEssDecision (int v) noexcept { forcedEss = v; }
    float wetDuckDb() const noexcept { return 20.0f * std::log10 (std::max (1.0e-6f, duckGain)); }
    const Reverb& reverb() const noexcept { return rev; }

    // delay time in samples for the current settings (tempo-synced or free)
    double delaySamples() const noexcept
    {
        const double sec = params.sync ? noteBeats (params.note) * 60.0 / std::clamp (params.bpm, 20.0, 400.0)
                                       : std::clamp ((double) params.timeMs, 1.0, 2000.0) * 0.001;
        return std::max (1.0, sec * fs);
    }

    void process (float* const* io, int numChannels, int n) noexcept
    {
#ifdef AXS_FTZ
        const unsigned csr = _mm_getcsr();
        _mm_setcsr (csr | 0x8040);   // flush denormals to zero (FTZ + DAZ): a decaying tail never slows the CPU down
#endif
        const int ch = std::clamp (numChannels, 1, 2);
        float* L = io[0];
        float* R = ch > 1 ? io[1] : io[0];
        const float essTarget = std::pow (10.0f, -std::max (0.0f, params.essDuckDb) / 20.0f);
        const float duckDepth = std::max (0.0f, params.voiceDuckDb);
        const float relK = 1.0f - std::exp (-1.0f / (std::max (10.0f, params.duckReleaseMs) * 0.001f * (float) fs));
        const float atkK = 1.0f - std::exp (-1.0f / (0.005f * (float) fs));
        const float envRelK = 1.0f - std::exp (-1.0f / (0.025f * (float) fs));   // the level follower: fast, fixed
        for (int i = 0; i < n; ++i)
        {
            float xL = L[i], xR = R[i];
            if (! std::isfinite (xL)) xL = 0.0f;
            if (! std::isfinite (xR)) xR = 0.0f;
            const float m = 0.5f * (xL + xR);
            // ESS DUCK: the detector's decision for the sample leaving the look-ahead line
            if (params.essDuck)
            {
                if (ess.push (m))
                    essP = forcedEss >= 0 ? (float) forcedEss : ess.out().p;
                const float tgt = essP > 0.5f ? essTarget : 1.0f;
                essGain += (tgt - essGain) * (tgt < essGain ? 0.35f : 0.02f);   // fast down, ~1 ms up per hop of samples
            }
            else
                essGain += (1.0f - essGain) * 0.02f;
            lookLine[0].push (xL);
            lookLine[1].push (xR);
            const float wL = lookLine[0].tap (look) * essGain, wR = lookLine[1].tap (look) * essGain;
            // the delay
            float dL = 0.0f, dR = 0.0f;
            if (params.delayOn || delayTail)
                dly.process (params.delayOn ? wL : 0.0f, params.delayOn ? wR : 0.0f, dL, dR);
            // the reverb: its own band-limited input (+ part of the delay)
            float rL = 0.0f, rR = 0.0f;
            if (params.reverbOn || reverbTail)
            {
                const float send = params.delayToReverb * 0.01f;
                float iL = params.reverbOn ? revHc[0].process (revLc[0].process (wL + send * dL)) : 0.0f;
                float iR = params.reverbOn ? revHc[1].process (revLc[1].process (wR + send * dR)) : 0.0f;
                rev.process (iL, iR, rL, rR);
                rL *= typeFade;
                rR *= typeFade;
            }
            // WIDTH (mid / side) of the reverb
            glide (widthG, widthT);
            glide (revG, revT);
            glide (dlyG, dlyT);
            const float mid = 0.5f * (rL + rR), side = 0.5f * (rL - rR) * widthG;
            rL = mid + side;
            rR = mid - side;
            float wetL = revG * rL + dlyG * dL, wetR = revG * rR + dlyG * dR;
            // VOICE DUCK: the wet turned down while the voice is loud re its own running peak
            const float a = std::max (std::abs (xL), std::abs (xR));
            env += (a - env) * (a > env ? atkK : envRelK);
            const float envDb = 20.0f * std::log10 (env + 1.0e-9f);
            peakDb = std::max (envDb, peakDb - 1.0f / (float) fs);   // falls 1 dB per second
            // full ducking from 12 dB under the voice's recent peak, none from 24 dB under it (a pause, a breath)
            const float over = std::clamp ((envDb - (peakDb - 24.0f)) / 12.0f, 0.0f, 1.0f);
            const float dg = std::pow (10.0f, -duckDepth * over / 20.0f);
            duckGain += (dg - duckGain) * (dg < duckGain ? atkK : relK);
            wetL *= duckGain;
            wetR *= duckGain;
            // MIX and OUTPUT
            glide (dryG, dryT);
            glide (wetG, wetT);
            glide (outG, outT);
            L[i] = outG * (dryG * xL + wetG * wetL);
            if (ch > 1)
                R[i] = outG * (dryG * xR + wetG * wetR);
            // TYPE change: fade the reverb out, rebuild, fade in (no clicks)
            if (pendingType >= 0)
            {
                typeFade -= 1.0f / (0.015f * (float) fs);
                if (typeFade <= 0.0f)
                {
                    typeFade = 0.0f;
                    rev.reset();
                    rev.configure (pendingType, params.size);
                    rev.setDecay (params.decay, params.damping, params.modulation);
                    rev.setEarly (params.early);
                    pendingType = -1;
                }
            }
            else if (typeFade < 1.0f)
                typeFade = std::min (1.0f, typeFade + 1.0f / (0.015f * (float) fs));
            ++sample;
        }
        // a switched-off section rings out (its tail is not cut), then stops costing CPU
        reverbTail = params.reverbOn ? false : (reverbTail && tailSamplesLeft (revTailLeft, n));
        delayTail = params.delayOn ? false : (delayTail && tailSamplesLeft (dlyTailLeft, n));
#ifdef AXS_FTZ
        _mm_setcsr (csr);
#endif
    }

private:
    static bool tailSamplesLeft (int64_t& left, int n) noexcept { left -= n; return left > 0; }

    void apply (const Params& p, bool force) noexcept
    {
        if (force || p.type != lastType)
        {
            if (force)
            {
                rev.configure (p.type, p.size);
                typeFade = 1.0f;
            }
            else
                pendingType = p.type;
            lastType = p.type;
            lastSize = p.size;
        }
        else if (std::abs (p.size - lastSize) > 1.0e-4f)
        {
            rev.configure (p.type, p.size);   // the line lengths glide to the new size
            lastSize = p.size;
        }
        rev.setDecay (p.decay, p.damping, p.modulation);
        rev.setPreDelay (p.preDelayMs);
        rev.setEarly (p.early);
        rev.setLfoRate (0.35);   // slow and fixed: the pitch wobble stays small (depth x rate), MODULATION sets the depth
        for (int c = 0; c < 2; ++c)
        {
            revLc[(size_t) c].set (fs, p.revLowCut, true);
            revHc[(size_t) c].set (fs, p.revHighCut, false);
        }
        dly.set (delaySamples(), (double) look, p.feedback, p.pingPong, p.dlyLowCut, p.dlyHighCut, p.drive);
        widthT = std::clamp (p.width, 0.0f, 200.0f) / 100.0f;
        revT = std::pow (10.0f, p.reverbDb / 20.0f);
        dlyT = std::pow (10.0f, p.delayDb / 20.0f);
        const float mx = std::clamp (p.mix, 0.0f, 100.0f) / 100.0f;
        dryT = std::min (1.0f, 2.0f * (1.0f - mx));
        wetT = std::min (1.0f, 2.0f * mx);
        outT = std::pow (10.0f, p.outputDb / 20.0f);
        if (force)
        {
            dryG = dryT;
            wetG = wetT;
            outG = outT;
            widthG = widthT;
            revG = revT;
            dlyG = dlyT;
        }
        // a section switched off keeps ringing for its own decay (the reverb: 1.5 x RT60; the delay: until -60 dB)
        if (! p.reverbOn && wasReverbOn)
        {
            reverbTail = true;
            revTailLeft = (int64_t) (1.5 * p.decay * fs + 0.3 * fs);
        }
        if (! p.delayOn && wasDelayOn)
        {
            delayTail = true;
            const double reps = p.feedback > 1.0f ? std::min (60.0, 3.0 / std::max (0.01, -std::log10 (p.feedback / 100.0))) : 1.0;
            dlyTailLeft = (int64_t) ((reps + 1.0) * delaySamples());
        }
        wasReverbOn = p.reverbOn;
        wasDelayOn = p.delayOn;
    }

    double fs = 48000.0;
    Params params;
    bool prepared = false;
    axell::voice::SibilanceDetector ess;
    int look = 384;
    std::array<Line, 2> lookLine;
    Reverb rev;
    Delay dly;
    std::array<Biquad, 2> revLc, revHc;
    float essP = 0.0f, essGain = 1.0f, env = 0.0f, peakDb = -60.0f, duckGain = 1.0f;
    float widthG = 1.0f, revG = 1.0f, dlyG = 1.0f, dryT = 1.0f, wetT = 0.5f, dryG = 1.0f, wetG = 0.5f, outG = 1.0f;
    float widthT = 1.0f, revT = 1.0f, dlyT = 1.0f, outT = 1.0f;
    // a gain glides to its target (~20 ms at 48 kHz) and lands on it exactly (MIX 0 = the dry signal, bit for bit)
    static void glide (float& g, float t) noexcept
    {
        g += (t - g) * 0.001f;
        if (std::abs (t - g) < 1.0e-6f)
            g = t;
    }
    float typeFade = 1.0f;
    int pendingType = -1, lastType = -1, forcedEss = -1;
    float lastSize = -1.0f;
    bool reverbTail = false, delayTail = false, wasReverbOn = true, wasDelayOn = false;
    int64_t revTailLeft = 0, dlyTailLeft = 0, sample = 0;
};
} // namespace axs
