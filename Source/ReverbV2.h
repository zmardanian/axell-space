#pragma once
// Axell Space 2.0 - the reverb core. A 32-line feedback delay network (Jot & Chaigne's structure; Hadamard mixing, which
// is lossless), every line with a short allpass inside the loop (echo density builds within milliseconds and the modes
// smear instead of ringing - Dattorro's in-tank diffusion, here on every line), three-band absorption per line (the
// decay set separately below ~250 Hz, at mid frequencies and above ~5 kHz: BASS, DECAY, DAMPING), smooth random
// modulation of every line (no common period), and a TRUE-STEREO input: the left and right inputs feed different
// lines, so a wide or double-tracked vocal keeps its image inside the tail. FREEZE turns the absorption off and the
// input away: the network is lossless, the tail holds. Each type is voiced (the wet's spectral balance) to sit between
// a real plate and natural halls (measured: docs/DECISIONS S2).
#include <array>
#include <algorithm>
#include <cmath>
#include <vector>

namespace axs::v2
{
constexpr double kPi2 = 3.14159265358979323846;

struct DLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    void prepare (int maxSamples)
    {
        int n = 1;
        while (n < maxSamples + 8)
            n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        w = 0;
    }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
    float tap (int d) const noexcept { return buf[(size_t) ((w - 1 - d) & mask)]; }
    float read (double d) const noexcept
    {
        const int i = (int) d;
        const float t = (float) (d - i);
        const float ym1 = tap (i - 1), y0 = tap (i), y1 = tap (i + 1), y2 = tap (i + 2);
        const float c0 = y0, c1 = y1 - ym1 * (1.0f / 3.0f) - y0 * 0.5f - y2 * (1.0f / 6.0f);
        const float c2 = 0.5f * (ym1 + y1) - y0, c3 = (1.0f / 6.0f) * (y2 - ym1) + 0.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }
};

struct AP
{
    DLine line;
    int d = 1;
    float g = 0.5f;
    float process (float x) noexcept
    {
        const float v = line.tap (d - 1);
        const float u = x + g * v;
        line.push (u);
        return v - g * u;
    }
};

// first-order shelf (bilinear): gain `lowGain` below fc and 1 above (low shelf), or 1 below and `highGain` above
struct Shelf1
{
    float b0 = 1, b1 = 0, a1 = 0, z = 0;
    void lowShelf (double fs, double fc, double g)
    {
        const double k = std::tan (kPi2 * fc / fs);
        // H(s) = (s + g wc) / (s + wc)
        const double a0 = k + 1.0;
        b0 = (float) ((1.0 + g * k) / a0); b1 = (float) ((g * k - 1.0) / a0); a1 = (float) ((k - 1.0) / a0);
    }
    void highShelf (double fs, double fc, double g)
    {
        const double k = std::tan (kPi2 * fc / fs);
        // H(s) = (g s + wc) / (s + wc)
        const double a0 = k + 1.0;
        b0 = (float) ((g + k) / a0); b1 = (float) ((k - g) / a0); a1 = (float) ((k - 1.0) / a0);
    }
    float process (float x) noexcept
    {
        const float y = b0 * x + z;
        z = b1 * x - a1 * y;
        return y;
    }
    void reset() noexcept { z = 0.0f; }
};

class Reverb
{
public:
    static constexpr int N = 32;
    enum Type { kBooth = 0, kRoom, kPlate, kHall };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const int maxLine = (int) std::ceil (0.20 * fs);
        for (auto& l : lines)
            l.prepare (maxLine);
        for (auto& a : loopAp)
            a.line.prepare ((int) std::ceil (0.012 * fs));
        for (auto& a : inAp)
            a.line.prepare ((int) std::ceil (0.03 * fs));
        pre.prepare ((int) std::ceil (2.2 * fs));   // up to a synced whole note at 120 bpm (2 s) + headroom
        preS.prepare ((int) std::ceil (2.2 * fs));
        erLine.prepare ((int) std::ceil (0.12 * fs));
        erLineR.prepare ((int) std::ceil (0.12 * fs));
        for (auto& a : erDiff)
            a.line.prepare ((int) std::ceil (0.004 * fs));
        // smooth random modulation: per line, two incommensurate slow sines (rates 0.31 .. 1.37 Hz)
        for (int i = 0; i < N; ++i)
        {
            const double gr = 0.6180339887498949 * (i + 1);
            const double w1 = 2.0 * kPi2 * (0.31 + 0.53 * (gr - std::floor (gr))) / fs;
            const double gr2 = 0.7548776662466927 * (i + 3);
            const double w2 = 2.0 * kPi2 * (0.57 + 0.80 * (gr2 - std::floor (gr2))) / fs;
            // each sine as a rotating phasor (one complex multiply per sample instead of a sin call)
            rc1[(size_t) i] = std::cos (w1); rs1[(size_t) i] = std::sin (w1);
            rc2[(size_t) i] = std::cos (w2); rs2[(size_t) i] = std::sin (w2);
            oc1[(size_t) i] = std::cos (1.7 * i); os1[(size_t) i] = std::sin (1.7 * i);
            oc2[(size_t) i] = std::cos (2.9 * i + 0.5); os2[(size_t) i] = std::sin (2.9 * i + 0.5);
        }
        reset();
        configured = false;
    }

    void reset() noexcept
    {
        for (auto& l : lines) l.reset();
        for (auto& a : loopAp) a.line.reset();
        for (auto& a : inAp) a.line.reset();
        for (auto& f : lowSh) f.reset();
        for (auto& f : highSh) f.reset();
        for (auto& f : voiceLo) f.reset();
        for (auto& f : voiceHi) f.reset();
        pre.reset();
        preS.reset();
        erLine.reset();
        erLineR.reset();
        for (auto& a : erDiff) a.line.reset();
    }

    void configure (int type, float sizePct)
    {
        curType = std::clamp (type, 0, 3);
        static constexpr double lo[4] = { 2.6, 5.0, 10.0, 18.0 }, hi[4] = { 21.0, 43.0, 85.0, 135.0 };
        const double s = 0.25 + 0.75 * std::clamp ((double) sizePct, 0.0, 100.0) / 100.0;
        for (int i = 0; i < N; ++i)
        {
            const double ms = lo[curType] * std::pow (hi[curType] / lo[curType], (double) i / (N - 1)) * s;
            len[(size_t) i] = nearestPrime ((int) std::lround (ms * 0.001 * fs));
            if (! configured)
                curLen[(size_t) i] = len[(size_t) i];   // later SIZE changes glide there (no click)
            // the in-loop allpass: ~13 % of its line (0.3 .. 9 ms), its own prime
            loopAp[(size_t) i].d = nearestPrime (std::clamp ((int) std::lround (0.13 * len[(size_t) i]), (int) (0.0003 * fs) + 2, (int) (0.009 * fs)));
            loopAp[(size_t) i].g = curType == kPlate ? 0.7f : (curType == kHall ? 0.62f : 0.5f);
        }
        // input diffusion, per side (L uses 0..3, R 4..7, slightly different lengths)
        static constexpr double dms[12] = { 1.531, 2.213, 3.595, 4.771, 6.317, 8.233, 1.693, 2.437, 3.271, 5.113, 6.871, 8.917 };
        const float gd = curType == kPlate ? 0.75f : (curType == kHall ? 0.72f : 0.62f);
        for (int k = 0; k < 12; ++k)
        {
            inAp[(size_t) k].d = std::max (1, (int) std::lround (dms[k] * 0.001 * fs * (curType == kBooth ? 0.5 : 1.0)));
            inAp[(size_t) k].g = gd;
        }
        static constexpr double eMs[6] = { 0.71, 1.13, 1.79, 0.83, 1.31, 1.97 };
        for (int k = 0; k < 6; ++k)
        {
            erDiff[(size_t) k].d = std::max (1, (int) std::lround (eMs[k] * 0.001 * fs));
            erDiff[(size_t) k].g = 0.62f;
        }
        buildEarly (sizePct);
        // the type's voicing of the wet (its tail's spectral balance): low shelf at 300 Hz, high shelf at 6 kHz
        static constexpr double vLo[4] = { 1.0, 2.0, 3.0, 2.5 }, vHi[4] = { -3.0, -4.5, -6.5, -5.5 };
        for (int c = 0; c < 2; ++c)
        {
            voiceLo[(size_t) c].lowShelf (fs, 300.0, std::pow (10.0, vLo[curType] / 20.0));
            voiceHi[(size_t) c].highShelf (fs, 6000.0, std::pow (10.0, vHi[curType] / 20.0));
        }
        // the late tail's level: as 1.0's at the same settings (measured), so presets and mixes keep their balance
        static constexpr double gainDb[4] = { 7.6, 7.6, 8.0, 7.6 };
        lateGain = (float) (0.25 * std::pow (10.0, gainDb[curType] / 20.0));
        configured = true;
    }

    // RT60 at mid frequencies; BASS = the low RT as a multiple of it (0.5 .. 2); DAMPING (%) = the high RT shorter (0: as
    // the mids, 100: a fifth); MODULATION (%); FREEZE holds the tail (lossless, no input)
    void setDecay (float rt60, float bassMult, float dampingPct, float modPct, bool freeze) noexcept
    {
        frozen = freeze;
        const double rt = std::clamp ((double) rt60, 0.1, 30.0);
        const double rtLow = rt * std::clamp ((double) bassMult, 0.25, 4.0);
        const double rtHigh = rt / (1.0 + 1.5 * std::clamp ((double) dampingPct, 0.0, 100.0) / 100.0);   // 100 %: highs 2.5x faster
        for (int i = 0; i < N; ++i)
        {
            const double L = (double) len[(size_t) i] + (double) loopAp[(size_t) i].d;   // the loop's whole delay
            auto g = [&] (double t) { return freeze ? 1.0 : std::pow (10.0, -3.0 * L / (t * fs)); };
            const double gM = g (rt), gL = g (rtLow), gH = g (rtHigh);
            gMid[(size_t) i] = (float) gM;
            lowSh[(size_t) i].lowShelf (fs, 350.0, gL / gM);   // first order: 125 Hz well inside the shelf
            highSh[(size_t) i].highShelf (fs, 5000.0, gH / gM);
        }
        modDepth = 0.0010 * fs * std::clamp ((double) modPct, 0.0, 100.0) / 100.0;   // up to 1 ms: smears modes, no audible pitch
    }

    void setPreDelay (double samples) noexcept { preSamples = std::clamp ((int) std::lround (samples), 0, pre.mask - 8); }
    void setEarly (float pct) noexcept { earlyGain = curType == kPlate ? 0.0f : std::clamp (pct, 0.0f, 100.0f) / 100.0f; }

    // one stereo sample in (filtered), one stereo sample out (wet only)
    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        pre.push (0.5f * (inL + inR));
        preS.push (0.5f * (inL - inR));
        const float m = pre.tap (preSamples), sd = preS.tap (preSamples);
        const float xL = m + sd, xR = m - sd;
        // early reflections, per side: a source on the left reflects mostly into the left ear (a third crosses over)
        erLine.push (xL);
        erLineR.push (xR);
        float eL = 0.0f, eR = 0.0f;
        for (int k = 0; k < erCount; ++k)
        {
            const float vl = erLine.tap (erDelay[(size_t) k]), vr = erLineR.tap (erDelay[(size_t) k]);
            eL += erGainL[(size_t) k] * (0.8f * vl + 0.35f * vr);
            eR += erGainR[(size_t) k] * (0.8f * vr + 0.35f * vl);
        }
        for (int k = 0; k < 3; ++k)
        {
            eL = erDiff[(size_t) k].process (eL);
            eR = erDiff[(size_t) (k + 3)].process (eR);
        }
        // input diffusion per side
        float dL = xL, dR = xR;
        for (int k = 0; k < 6; ++k)
        {
            dL = inAp[(size_t) k].process (dL);
            dR = inAp[(size_t) (k + 6)].process (dR);
        }
        if (++renorm >= 4096)
        {
            renorm = 0;
            for (int i = 0; i < N; ++i)
            {
                const double a = 1.0 / std::sqrt (oc1[(size_t) i] * oc1[(size_t) i] + os1[(size_t) i] * os1[(size_t) i]);
                const double b = 1.0 / std::sqrt (oc2[(size_t) i] * oc2[(size_t) i] + os2[(size_t) i] * os2[(size_t) i]);
                oc1[(size_t) i] *= a; os1[(size_t) i] *= a; oc2[(size_t) i] *= b; os2[(size_t) i] *= b;
            }
        }
        const float inGain = frozen ? 0.0f : 1.0f;
        fadeIn += ((inGain) - fadeIn) * 0.0005f;   // ~40 ms: FREEZE in / out without a click
        // read the lines, modulated
        std::array<float, N> v {};
        for (int i = 0; i < N; ++i)
        {
            {
                const double c1 = oc1[(size_t) i], s1 = os1[(size_t) i], c2 = oc2[(size_t) i], s2 = os2[(size_t) i];
                oc1[(size_t) i] = c1 * rc1[(size_t) i] - s1 * rs1[(size_t) i]; os1[(size_t) i] = s1 * rc1[(size_t) i] + c1 * rs1[(size_t) i];
                oc2[(size_t) i] = c2 * rc2[(size_t) i] - s2 * rs2[(size_t) i]; os2[(size_t) i] = s2 * rc2[(size_t) i] + c2 * rs2[(size_t) i];
            }
            const double mod = modDepth * (0.6 * os1[(size_t) i] + 0.4 * os2[(size_t) i]);
            curLen[(size_t) i] += 0.0005 * ((double) len[(size_t) i] - curLen[(size_t) i]);
            v[(size_t) i] = lines[(size_t) i].read (std::max (1.0, curLen[(size_t) i] + mod - 1.0));
        }
        float lateL = 0.0f, lateR = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            // the left output reads the lines fed from the left (even) at full weight, the right-fed ones at 0.6 - and
            // the other way round: the first echoes lean to the source's side; mixed tails stay balanced and decorrelated
            const float wl = (i & 1) ? 0.6f : 1.0f, wr = (i & 1) ? 1.0f : 0.6f;
            lateL += wl * ((((i >> 0) ^ (i >> 2) ^ (i >> 4)) & 1) ? -1.0f : 1.0f) * v[(size_t) i];
            lateR += wr * ((((i >> 1) ^ (i >> 3) ^ (i >> 4)) & 1) ? -1.0f : 1.0f) * v[(size_t) i];
        }
        // absorption (three bands), the in-loop allpass, the lossless mixing, the input in (L to even lines, R to odd)
        for (int i = 0; i < N; ++i)
        {
            float y = highSh[(size_t) i].process (lowSh[(size_t) i].process (v[(size_t) i] * gMid[(size_t) i]));
            v[(size_t) i] = loopAp[(size_t) i].process (y);
        }
        hadamard (v);
        const float nrm = 0.17677669529663687f;   // 1 / sqrt(32)
        for (int i = 0; i < N; ++i)
        {
            const float x = (i & 1) ? dR : dL;
            const float sgn = ((i >> 1) & 1) ? -1.0f : 1.0f;
            lines[(size_t) i].push (v[(size_t) i] * nrm + sgn * x * fadeIn * 0.25f);
        }
        // the onset keeps the source's side: the diffused input itself (a dense burst per side) before the tank mixes it
        const float onsetGain = curType == kPlate ? 0.22f : 0.08f;
        float oL = earlyGain * eL + lateGain * lateL + onsetGain * dL, oR = earlyGain * eR + lateGain * lateR + onsetGain * dR;
        oL = voiceHi[0].process (voiceLo[0].process (oL));
        oR = voiceHi[1].process (voiceLo[1].process (oR));
        outL = oL;
        outR = oR;
    }

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
            if (k < 2) return false;
            for (int q = 2; q * q <= k; ++q)
                if (k % q == 0) return false;
            return true;
        };
        for (int o = 0;; ++o)
        {
            if (prime (n + o)) return n + o;
            if (n - o > 1 && prime (n - o)) return n - o;
        }
    }
    // image-source shoebox (as 1.0): first- to third-order reflections, two ears 17 cm apart
    void buildEarly (float sizePct)
    {
        erCount = 0;
        if (curType == kPlate)
            return;
        static constexpr double dims[4][3] = { { 1.8, 1.5, 2.2 }, { 4.2, 3.6, 2.6 }, { 0, 0, 0 }, { 16.0, 11.0, 7.0 } };
        const double s = 0.6 + 0.8 * std::clamp ((double) sizePct, 0.0, 100.0) / 100.0;
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
    double fs = 48000.0, modDepth = 0.0;
    std::array<DLine, N> lines;
    std::array<AP, N> loopAp;
    std::array<AP, 12> inAp;
    std::array<Shelf1, N> lowSh, highSh;
    std::array<Shelf1, 2> voiceLo, voiceHi;
    std::array<int, N> len {};
    std::array<float, N> gMid {};
    std::array<double, N> rc1 {}, rs1 {}, rc2 {}, rs2 {}, oc1 {}, os1 {}, oc2 {}, os2 {}, curLen {};
    int renorm = 0;
    DLine pre, preS, erLine, erLineR;
    std::array<AP, 6> erDiff;
    std::array<int, kMaxEr> erDelay {};
    std::array<float, kMaxEr> erGainL {}, erGainR {};
    int erCount = 0, preSamples = 0, curType = kPlate;
    float earlyGain = 0.5f, lateGain = 0.25f, fadeIn = 1.0f;
    bool configured = false, frozen = false;
};
} // namespace axs::v2
