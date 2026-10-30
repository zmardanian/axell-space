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

#include "ReverbV2.h"

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
    // 2.0
    int engine = 2;              // 1: the 1.0 reverb (sessions saved before 2.0 keep it, bit for bit); 2: the 2.0 core
    float bass = 100.0f;         // % : the low decay (below ~250 Hz) re DECAY, 50 .. 200 (2.0 core)
    bool freeze = false;         // hold the tail forever, no new input (2.0 core)
    bool preSync = false;        // PRE-DELAY in note values of the host tempo
    int preNote = 0;             // k1_16 .. (the delay's note list)
    // 3.0
    int dlyMode = 0;             // 0 CLEAN (1.0's delay, bit for bit), 1 TAPE, 2 ANALOG
    float wowFlutter = 40.0f;    // % : TAPE / ANALOG pitch movement of the repeats
    float diffuse = 0.0f;        // % : each repeat smears a little more (allpasses in the loop)
    float unmask = 0.0f;         // % : the effects step back in the bands the voice is using, bloom in the gaps
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
    // RBJ band-pass (0 dB peak)
    void setBand (double fs, double f, double q)
    {
        f = std::clamp (f, 10.0, 0.45 * fs);
        const double w = 2.0 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (float) (al / a0); b1 = 0.0f; b2 = (float) (-al / a0);
        a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - al) / a0);
    }
    // RBJ peaking EQ (gain in dB; 0 dB = an exact pass-through)
    void setPeak (double fs, double f, double q, double gainDb)
    {
        f = std::clamp (f, 10.0, 0.45 * fs);
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + al / A;
        b0 = (float) ((1.0 + al * A) / a0); b1 = (float) (-2.0 * c / a0); b2 = (float) ((1.0 - al * A) / a0);
        a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - al / A) / a0);
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
        for (auto& v : voice)
            v.prepare (fs);
        // the allpasses' delay, taken off each side's read time: both sides exactly on the beat, in step with each other
        apDelay = { voice[0].apDelay (0), voice[0].apDelay (1) };
        xStep = (float) (1.0 / (0.02 * fs));
        driftK = 1.0 - std::exp (-1.0 / (0.4 * fs));
        // the gain that gives the twice-filtered noise about a unit standard deviation
        driftG = std::sqrt (3.0) / std::sqrt (driftK / 2.0) * std::sqrt (2.0);
        wowW = 2.0 * kPi * 0.6 / fs;
        flW = 2.0 * kPi * 7.1 / fs;
        reset();
    }
    void reset() noexcept
    {
        for (auto& l : lines)
            l.reset();
        for (auto* fs4 : { &lc, &hc, &olc, &ohc })
            for (auto& f : *fs4)
                f.reset();
        for (auto& v : voice)
            v.reset();
        drift = drift1 = 0.0;
        if (xfading || pending)
        {
            curPath = wantPath();
            if (curPath > 0) voice[(size_t) (curPath - 1)].configure (wantMode, diff);
        }
        xfading = pending = false;
        xm = 0.0f;
        curT = -1.0;
    }
    // 3.0: the character of the repeats. CLEAN with DIFFUSE 0 is 1.0's delay, bit for bit; TAPE, ANALOG (or DIFFUSE)
    // run the character path. A change crossfades over 20 ms - what is heard AND what is written to the line, so the
    // repeats already on their way pass from one character to the other without a step.
    void setCharacter (int modeIn, float wowPct, float diffusePct) noexcept
    {
        wantMode = std::clamp (modeIn, 0, 2);
        wow = std::clamp (wowPct, 0.0f, 100.0f) / 100.0f;
        diff = std::clamp (diffusePct, 0.0f, 100.0f) / 100.0f;
        if (! started)
        {
            started = true;
            curPath = wantPath();
            if (curPath > 0) voice[(size_t) (curPath - 1)].configure (wantMode, diff);
            return;
        }
        if (xfading)
        {
            pending = true;   // after this crossfade, the next
            return;
        }
        const int want = wantPath();
        const bool same = want == 0 ? curPath == 0 : (curPath > 0 && voice[(size_t) (curPath - 1)].mode == wantMode);
        if (same)
        {
            if (curPath > 0) voice[(size_t) (curPath - 1)].configure (wantMode, diff);   // DIFFUSE moves smoothly
            return;
        }
        startCrossfade();
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
        for (auto& v : voice)
            v.setCuts (fs, lowCut, highCut);
        drive = std::clamp (drivePct, 0.0f, 100.0f) / 100.0f;
    }
    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        curT += 0.0007 * (targetT - curT);       // a time change glides (tape-like), never clicks
        if (! xfading && curPath == 0)
        {
            processClean (inL, inR, outL, outR);   // 1.0's delay, unchanged
            return;
        }
        advanceMod();
        Out a {};
        write (curPath, inL, inR, a);
        if (! xfading)
        {
            push (a.wL, a.wR);
            play (curPath, a);
            outL = a.oL;
            outR = a.oR;
            return;
        }
        Out b {};
        write (nextPath, inL, inR, b);
        // the new path warms up for 10 ms at weight 0 (its filters and allpasses filled with the running signal), then
        // the 20 ms crossfade
        xm = std::min (1.0f, xm + xStep);
        const float kb = std::max (0.0f, xm), ka = 1.0f - kb;
        push (ka * a.wL + kb * b.wL, ka * a.wR + kb * b.wR);
        play (curPath, a);
        play (nextPath, b);
        outL = ka * a.oL + kb * b.oL;
        outR = ka * a.oR + kb * b.oR;
        if (xm >= 1.0f)
        {
            xfading = false;
            curPath = nextPath;
            if (pending)
            {
                pending = false;
                const int want = wantPath();
                const bool same = want == 0 ? curPath == 0 : (curPath > 0 && voice[(size_t) (curPath - 1)].mode == wantMode);
                if (! same) startCrossfade();
                else if (curPath > 0) voice[(size_t) (curPath - 1)].configure (wantMode, diff);
            }
        }
    }

private:
    struct Out { float wL, wR, oL, oR; double mod; };
    // one character of the repeats: the play head's loop and heard paths (each: LOW / HIGH CUT, the head's tone,
    // DIFFUSE's allpasses) and the record head
    struct Voice
    {
        struct Chain
        {
            Biquad lc, hc, age, bump;
            float trim = 1.0f;
            float process (float v) noexcept
            {
                v = hc.process (lc.process (v));
                return trim * age.process (bump.process (v));
            }
            void reset() noexcept { lc.reset(); hc.reset(); age.reset(); bump.reset(); }
        };
        std::array<std::array<Chain, 2>, 2> chain;   // the play head: [loop, heard][channel]
        // DIFFUSE: two short allpasses per channel at the record head (both sides read the tape at the same instant)
        std::array<std::array<Allpass, 2>, 2> ap;
        std::array<float, 2> dcX {}, dcY {};
        float dcR = 0.9993f;
        double fs = 48000.0;
        int mode = 1;
        void prepare (double sampleRate)
        {
            fs = sampleRate;
            // the right side's allpasses a little longer: with DIFFUSE the two sides' smears decorrelate
            static constexpr double dms[2][2] = { { 1.31, 2.87 }, { 1.63, 3.41 } };
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < 2; ++k)
                {
                    auto& a = ap[(size_t) c][(size_t) k];
                    a.line.prepare ((int) std::ceil (0.006 * fs));
                    a.d = (int) std::lround (dms[c][k] * 0.001 * fs);
                    a.g = 0.0f;
                }
            dcR = (float) std::exp (-2.0 * kPi * 5.0 / fs);
            configure (mode, 0.0f);
        }
        double apDelay (int c) const noexcept { return (double) ap[(size_t) c][0].d + ap[(size_t) c][1].d; }
        void reset() noexcept
        {
            for (auto& path : chain)
                for (auto& c : path)
                    c.reset();
            for (auto& side : ap)
                for (auto& a : side)
                    a.line.reset();
            dcX = {}; dcY = {};
        }
        void setCuts (double f, float lowCut, float highCut) noexcept
        {
            for (auto& path : chain)
                for (auto& c : path)
                {
                    c.lc.set (f, lowCut, true);
                    c.hc.set (f, highCut, false);
                }
        }
        // TAPE: a gentle loss of top per pass and a head bump (the whole curve sits at or under 0 dB, so the loop never
        // gains: FEEDBACK < 100 % always dies away); ANALOG: a dark two-pole low pass (a bucket-brigade line); CLEAN
        // (with DIFFUSE): no tone of its own
        void configure (int m, float diffuse) noexcept
        {
            mode = m;
            for (auto& path : chain)
                for (auto& ch : path)
                {
                    if (mode == 1)
                    {
                        ch.age.set (fs, 7500.0, false);
                        ch.bump.setPeak (fs, 110.0, 0.9, 1.2);
                        ch.trim = std::pow (10.0f, -1.2f / 20.0f);
                    }
                    else if (mode == 2)
                    {
                        ch.age.set (fs, 3600.0, false);
                        ch.bump.setPeak (fs, 110.0, 0.9, 0.0);
                        ch.trim = 1.0f;
                    }
                    else
                    {
                        ch.age.setPeak (fs, 1000.0, 0.7, 0.0);
                        ch.bump.setPeak (fs, 110.0, 0.9, 0.0);
                        ch.trim = 1.0f;
                    }
                }
            for (auto& side : ap)
                for (auto& a : side)
                    a.g = 0.68f * diffuse;
        }
        // the record head: TAPE a soft, slightly asymmetric curve (even harmonics) that always works a little, more
        // with DRIVE; ANALOG a soft symmetric clip; CLEAN (with DIFFUSE) DRIVE's tanh only
        float record (int c, float x, float drive) noexcept
        {
            float y;
            if (mode == 1)
            {
                const float k = 1.4f + 6.0f * drive;
                y = std::tanh (k * (x + 0.08f * x * x)) / k;
            }
            else if (mode == 2)
            {
                const float k = 1.1f + 6.0f * drive;
                y = x / (1.0f + 0.5f * std::abs (k * x));
            }
            else if (drive > 0.0f)
            {
                const float k = 1.0f + 6.0f * drive;
                y = std::tanh (k * x) / k;
            }
            else
                y = x;
            // DC blocker (~5 Hz): the even term's offset never builds up in the loop
            const float o = y - dcX[(size_t) c] + dcR * dcY[(size_t) c];
            dcX[(size_t) c] = y; dcY[(size_t) c] = o;
            return ap[(size_t) c][1].process (ap[(size_t) c][0].process (o));
        }
    };
    int wantPath() const noexcept { return (wantMode != 0 || diff > 0.0f) ? 1 : 0; }
    void startCrossfade() noexcept
    {
        // the new path starts from rest: CLEAN's own filters, or the character voice not in use
        if (wantPath() == 0)
        {
            nextPath = 0;
            for (auto* fs4 : { &lc, &hc, &olc, &ohc })
                for (auto& f : *fs4)
                    f.reset();
        }
        else
        {
            nextPath = curPath == 1 ? 2 : 1;
            auto& v = voice[(size_t) (nextPath - 1)];
            v.reset();
            v.configure (wantMode, diff);
        }
        xm = -0.5f;   // 10 ms of warm-up
        xfading = true;
    }
    void advanceMod() noexcept
    {
        wowPh += wowW;   if (wowPh > 2.0 * kPi) wowPh -= 2.0 * kPi;
        flPh += flW;     if (flPh > 2.0 * kPi) flPh -= 2.0 * kPi;
        // a slow random drift of the tape speed: white noise through two one-poles (~0.4 s)
        rnd = rnd * 1664525u + 1013904223u;
        const double u = (double) (rnd >> 8) / 8388608.0 - 1.0;
        drift1 += driftK * (driftG * u - drift1);
        drift += driftK * (drift1 - drift);
    }
    float shape (float x) const noexcept
    {
        if (drive <= 0.0f)
            return x;
        const float k = 1.0f + 6.0f * drive;
        return std::tanh (k * x) / k;
    }
    void push (float wL, float wR) noexcept
    {
        lines[0].push (wL);
        lines[1].push (wR);
    }
    // path 0 = CLEAN (1.0's arithmetic), 1 / 2 = a character voice. write(): what it puts on the line (before the push);
    // play(): what it plays (after the push - as 1.0's delay reads it)
    void write (int path, float inL, float inR, Out& o) noexcept
    {
        if (path == 0)
        {
            const float fL = lines[0].read (curT - 1.0), fR = lines[1].read (curT - 1.0);
            const float sL = shape (hc[0].process (lc[0].process (fL))), sR = shape (hc[1].process (lc[1].process (fR)));
            if (ping) { o.wL = 0.5f * (inL + inR) + fb * sR; o.wR = fb * sL; }
            else      { o.wL = inL + fb * sL;                o.wR = inR + fb * sR; }
            return;
        }
        auto& v = voice[(size_t) (path - 1)];
        // the read head's movement in seconds at WOW & FLUTTER 100 % (TAPE): wow 0.8 ms at 0.6 Hz (0.30 % peak speed
        // change), flutter 30 us at 7.1 Hz (0.13 %), drift 0.3 ms; ANALOG a fifth of it (clock jitter); CLEAN none
        const double scale = (v.mode == 1 ? 1.0 : (v.mode == 2 ? 0.2 : 0.0)) * wow * fs;
        o.mod = scale * (0.0008 * std::sin (wowPh) + 0.00003 * std::sin (flPh) + 0.0003 * drift);
        // the allpasses' own delay is taken off each side's read time: the repeats stay on the beat
        const float sL = v.chain[0][0].process (lines[0].read (std::max (2.0, curT - 1.0 - apDelay[0] + o.mod)));
        const float sR = v.chain[0][1].process (lines[1].read (std::max (2.0, curT - 1.0 - apDelay[1] + o.mod)));
        if (ping) { o.wL = v.record (0, 0.5f * (inL + inR) + fb * sR, drive); o.wR = v.record (1, fb * sL, drive); }
        else      { o.wL = v.record (0, inL + fb * sL, drive);                o.wR = v.record (1, inR + fb * sR, drive); }
    }
    void play (int path, Out& o) noexcept
    {
        if (path == 0)
        {
            const double tOut = std::max (1.0, curT - off);
            o.oL = shape (ohc[0].process (olc[0].process (lines[0].read (tOut))));
            o.oR = shape (ohc[1].process (olc[1].process (lines[1].read (tOut))));
            return;
        }
        auto& v = voice[(size_t) (path - 1)];
        o.oL = v.chain[1][0].process (lines[0].read (std::max (2.0, curT - off - apDelay[0] + o.mod)));
        o.oR = v.chain[1][1].process (lines[1].read (std::max (2.0, curT - off - apDelay[1] + o.mod)));
    }
    void processClean (float inL, float inR, float& outL, float& outR) noexcept
    {
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
    double fs = 48000.0, targetT = 1000.0, curT = -1.0, off = 0.0;
    std::array<double, 2> apDelay {};
    std::array<Line, 2> lines;
    std::array<Biquad, 2> lc, hc, olc, ohc;
    std::array<Voice, 2> voice;
    float fb = 0.3f, drive = 0.0f, wow = 0.4f, diff = 0.0f, xm = 0.0f, xStep = 0.001f;
    int wantMode = 0, curPath = 0, nextPath = 0;
    bool started = false, xfading = false, pending = false, ping = false;
    double wowPh = 0.0, flPh = 0.0, wowW = 0.0, flW = 0.0, drift = 0.0, drift1 = 0.0, driftK = 1.0e-4, driftG = 1.0;
    uint32_t rnd = 12345u;
};

//== UNMASK (3.0) ==================================================================================================
// The effects step back only where they would cover the voice: 18 third-octave bands (200 Hz - 10 kHz). A band dips while
// the voice uses it (within 20 dB of that band's own loud level, and holding a real share of the voice: within 30 dB of
// its loudest band) AND the wet there comes within 30 dB of the voice. The 18 dips are solved together (the bells'
// skirts overlap: the combined curve lands on each band's target, a free band next to a dipped one stays level) and
// drawn with state-variable bells whose gain glides every sample (no zipper). AMOUNT 100 % = up to 12 dB per band.
class Unmask
{
public:
    static constexpr int B = 18;
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        for (int b = 0; b < B; ++b)
        {
            for (auto& f : detV[(size_t) b]) f.setBand (fs, freq (b), kDetQ);
            for (auto& f : detW[(size_t) b]) f.setBand (fs, freq (b), kDetQ);
            gw[(size_t) b] = std::tan (kPi * std::min (freq (b), 0.45 * fs) / fs);
        }
        // the bells' interaction: M[i][j] = bell j's response (dB) at band i's centre, per dB of its own gain (at -6 dB)
        double M[B][B], I[B][B];
        for (int i = 0; i < B; ++i)
            for (int j = 0; j < B; ++j)
            {
                const double w = gw[(size_t) i] / gw[(size_t) j], a6 = std::pow (10.0, -6.0 / 40.0);
                const double re = 1.0 - w * w, n = re * re + std::pow (w * a6 / kQ, 2.0), d = re * re + std::pow (w / (a6 * kQ), 2.0);
                M[i][j] = 10.0 * std::log10 (n / d) / -6.0;
                I[i][j] = i == j ? 1.0 : 0.0;
            }
        for (int c = 0; c < B; ++c)   // Gauss-Jordan (M is well conditioned: diagonal 1, neighbours ~0.3)
        {
            const double pv = M[c][c];
            for (int k = 0; k < B; ++k) { M[c][k] /= pv; I[c][k] /= pv; }
            for (int r = 0; r < B; ++r)
                if (r != c)
                {
                    const double f = M[r][c];
                    for (int k = 0; k < B; ++k) { M[r][k] -= f * M[c][k]; I[r][k] -= f * I[c][k]; }
                }
        }
        for (int i = 0; i < B; ++i)
            for (int j = 0; j < B; ++j)
                inv[(size_t) i][(size_t) j] = (float) I[i][j];
        atk = 1.0f - std::exp (-32.0f / (0.003f * (float) fs));
        rel = 1.0f - std::exp (-32.0f / (0.02f * (float) fs));
        envK = 1.0f - std::exp (-1.0f / (0.006f * (float) fs));
        smK = 1.0f - std::exp (-1.0f / (0.0007f * (float) fs));
        reset();
    }
    void reset() noexcept
    {
        for (int b = 0; b < B; ++b)
        {
            for (auto& f : detV[(size_t) b]) f.reset();
            for (auto& f : detW[(size_t) b]) f.reset();
            envV[(size_t) b] = envW[(size_t) b] = 0.0f;
            peak[(size_t) b] = -100.0f;
            tgt[(size_t) b] = 0.0f;
            A[(size_t) b] = aTo[(size_t) b] = As[(size_t) b] = 1.0f;
            aStep[(size_t) b] = 0.0f;
            ramp[(size_t) b] = 0;
            for (auto& st : ic[(size_t) b]) st = {};
        }
        count = 0;
        moving = false;
    }
    void setAmount (float pct) noexcept { depth = 12.0f * std::clamp (pct, 0.0f, 100.0f) / 100.0f; }
    bool active() const noexcept { return depth > 0.0f || moving; }
    // voice = the dry voice (mono, undelayed); wL / wR = the wet, filtered in place
    void process (float voice, float& wL, float& wR) noexcept
    {
        const float wm = 0.5f * (wL + wR);
        for (int b = 0; b < B; ++b)
        {
            const float v = detV[(size_t) b][1].process (detV[(size_t) b][0].process (voice));
            const float w = detW[(size_t) b][1].process (detW[(size_t) b][0].process (wm));
            envV[(size_t) b] += (v * v - envV[(size_t) b]) * envK;
            envW[(size_t) b] += (w * w - envW[(size_t) b]) * envK;
        }
        if (++count >= 32)
        {
            count = 0;
            control();
        }
        // the bells (Simper's trapezoidal state-variable form; only the gain terms move, every sample)
        for (int b = 0; b < B; ++b)
        {
            if (ramp[(size_t) b] > 0)
            {
                A[(size_t) b] += aStep[(size_t) b];
                if (--ramp[(size_t) b] == 0) A[(size_t) b] = aTo[(size_t) b];
            }
            // always run while UNMASK is on: at A = 1 the bell's output is its input exactly (m1 = 0) and its states stay
            // in step, so a dip that starts later starts without a transient
            // the 32-sample ramps rounded by a 0.7 ms one-pole (no corners: no sidebands at multiples of fs / 32)
            As[(size_t) b] += (A[(size_t) b] - As[(size_t) b]) * smK;
            if (std::abs (As[(size_t) b] - A[(size_t) b]) < 1.0e-7f) As[(size_t) b] = A[(size_t) b];
            const float a = As[(size_t) b], g = (float) gw[(size_t) b], k = 1.0f / ((float) kQ * a);
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2, m1 = k * (a * a - 1.0f);
            wL = bell (ic[(size_t) b][0], wL, a1, a2, a3, m1);
            wR = bell (ic[(size_t) b][1], wR, a1, a2, a3, m1);
        }
    }
    float bandGainDb (int b) const noexcept { return 40.0f * std::log10 (As[(size_t) b]); }

private:
    struct Svf { float z1 = 0.0f, z2 = 0.0f; };
    static float bell (Svf& s, float x, float a1, float a2, float a3, float m1) noexcept
    {
        const float v3 = x - s.z2, v1 = a1 * s.z1 + a2 * v3, v2 = s.z2 + a2 * s.z1 + a3 * v3;
        s.z1 = 2.0f * v1 - s.z1;
        s.z2 = 2.0f * v2 - s.z2;
        return x + m1 * v1;
    }
    void control() noexcept
    {
        float loud = -200.0f;
        std::array<float, B> ev {}, ew {};
        for (int b = 0; b < B; ++b)
        {
            ev[(size_t) b] = 10.0f * std::log10 (envV[(size_t) b] + 1.0e-12f);
            ew[(size_t) b] = 10.0f * std::log10 (envW[(size_t) b] + 1.0e-12f);
            peak[(size_t) b] = std::max (ev[(size_t) b], peak[(size_t) b] - 0.5f * 32.0f / (float) fs);   // falls 0.5 dB/s
            loud = std::max (loud, ev[(size_t) b]);
        }
        std::array<float, B> want {};
        for (int b = 0; b < B; ++b)
        {
            const float own = std::clamp ((ev[(size_t) b] - (peak[(size_t) b] - 26.0f)) / 6.0f, 0.0f, 1.0f);
            const float share = std::clamp ((ev[(size_t) b] - (loud - 36.0f)) / 6.0f, 0.0f, 1.0f);
            const float covers = std::clamp ((ew[(size_t) b] - (ev[(size_t) b] - 30.0f)) / 6.0f, 0.0f, 1.0f);
            const float t = -depth * own * share * covers;
            tgt[(size_t) b] += (t - tgt[(size_t) b]) * (t < tgt[(size_t) b] ? atk : rel);
            want[(size_t) b] = tgt[(size_t) b];
        }
        // solve for the bells' own gains (a free band beside a dip gets a small lift that cancels the skirt; at most +3 dB)
        moving = false;
        for (int i = 0; i < B; ++i)
        {
            float gdb = 0.0f;
            for (int j = 0; j < B; ++j) gdb += inv[(size_t) i][(size_t) j] * want[(size_t) j];
            gdb = std::clamp (gdb, -18.0f, 3.0f);
            const bool atRest = std::abs (gdb) < 0.005f;
            const float to = atRest ? 1.0f : std::pow (10.0f, gdb / 40.0f);
            aTo[(size_t) i] = to;
            aStep[(size_t) i] = (to - A[(size_t) i]) / 32.0f;
            ramp[(size_t) i] = 32;   // a linear glide of the gain over the next 32 samples, landing exactly
            moving = moving || ! atRest || std::abs (A[(size_t) i] - 1.0f) > 1.0e-7f || std::abs (As[(size_t) i] - 1.0f) > 1.0e-7f;
        }
    }
    static double freq (int b) noexcept { return 200.0 * std::pow (2.0, b / 3.0); }   // 1/3-octave: 200 Hz - 10 kHz
    static constexpr double kQ = 4.32, kDetQ = 2.8;   // detectors: two sections of Q 2.8 = about a third of an octave
    double fs = 48000.0;
    std::array<std::array<Biquad, 2>, B> detV, detW;
    std::array<double, B> gw {};
    std::array<std::array<float, B>, B> inv {};
    std::array<std::array<Svf, 2>, B> ic {};
    std::array<float, B> envV {}, envW {}, peak {}, tgt {}, A {}, aTo {}, aStep {}, As {};
    float depth = 0.0f, atk = 0.5f, rel = 0.05f, envK = 0.002f, smK = 0.03f;
    std::array<int, B> ramp {};
    int count = 0;
    bool moving = false;
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
        rev2.prepare (fs);
        dly.prepare (fs);
        unmask.prepare (fs);
        prepared = true;
        reset();
        apply (params, true);
    }

    void reset() noexcept
    {
        for (auto& l : lookLine)
            l.reset();
        rev.reset();
        rev2.reset();
        dly.reset();
        unmask.reset();
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
    static constexpr int kUnmaskBands = Unmask::B;
    float unmaskDb (int b) const noexcept { return unmaskOn ? unmask.bandGainDb (b) : 0.0f; }   // UNMASK's band gains now
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
                if (activeEngine == 2)
                    rev2.process (iL, iR, rL, rR);   // true stereo
                else
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
            // UNMASK: the wet dips in the bands the (dry, undelayed) voice is using; off = not run at all
            if (unmaskOn)
                unmask.process (m, wetL, wetR);
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
                    rev2.reset();
                    activeEngine = params.engine == 1 ? 1 : 2;
                    rev.configure (pendingType, params.size);
                    rev.setDecay (params.decay, params.damping, params.modulation);
                    rev.setEarly (params.early);
                    rev2.configure (pendingType, params.size);
                    rev2.setDecay (params.decay, params.bass / 100.0f, params.damping, params.modulation, params.freeze);
                    rev2.setEarly (params.early);
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
        const int eng = p.engine == 1 ? 1 : 2;
        if (force || p.type != lastType || eng != activeEngine)
        {
            if (force)
            {
                rev.configure (p.type, p.size);
                rev2.configure (p.type, p.size);
                activeEngine = eng;
                typeFade = 1.0f;
            }
            else
                pendingType = p.type;   // TYPE or ENGINE: fade out, rebuild, fade in
            lastType = p.type;
            lastSize = p.size;
        }
        else if (std::abs (p.size - lastSize) > 1.0e-4f)
        {
            rev.configure (p.type, p.size);   // the line lengths glide to the new size (1.0)
            rev2.configure (p.type, p.size);
            lastSize = p.size;
        }
        rev.setDecay (p.decay, p.damping, p.modulation);
        rev.setPreDelay (p.preDelayMs);
        rev.setEarly (p.early);
        rev2.setDecay (p.decay, p.bass / 100.0f, p.damping, p.modulation, p.freeze);
        rev2.setEarly (p.early);
        // PRE-DELAY: ms, or a note value of the host tempo (2.0 core)
        const double preSec = p.preSync ? noteBeats (p.preNote) * 60.0 / std::clamp (p.bpm, 20.0, 400.0) : (double) p.preDelayMs * 0.001;
        rev2.setPreDelay (preSec * fs);
        rev.setLfoRate (0.35);   // slow and fixed: the pitch wobble stays small (depth x rate), MODULATION sets the depth
        for (int c = 0; c < 2; ++c)
        {
            revLc[(size_t) c].set (fs, p.revLowCut, true);
            revHc[(size_t) c].set (fs, p.revHighCut, false);
        }
        dly.set (delaySamples(), (double) look, p.feedback, p.pingPong, p.dlyLowCut, p.dlyHighCut, p.drive);
        dly.setCharacter (p.dlyMode, p.wowFlutter, p.diffuse);
        unmask.setAmount (p.unmask);
        unmaskOn = p.unmask > 0.0f || unmask.active();
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
    v2::Reverb rev2;
    int activeEngine = 2;
    Delay dly;
    Unmask unmask;
    bool unmaskOn = false;
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
