// Axell Space - DSP suite: the shipping engine (Source/Engine.h, JUCE-free), measured.
//   SpaceDspTests [--quick]   (quick: 48 kHz only, no CPU benchmark)
#include "../Source/Engine.h"
#include "../Source/Presets.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

using namespace axs;

// allocation counter (process() must never allocate)
static bool countAllocs = false;
static int allocCount = 0;
void* operator new (std::size_t n)
{
    if (countAllocs)
        ++allocCount;
    if (void* p = std::malloc (n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

static int checks = 0, fails = 0;
static void check (const std::string& name, double v, double lo, double hi, const char* unit)
{
    ++checks;
    const bool ok = v >= lo && v <= hi;
    fails += ok ? 0 : 1;
    std::printf ("%s  %-104s %11.4f %s  [%g .. %g]\n", ok ? "PASS" : "FAIL", name.c_str(), v, unit, lo, hi);
}
static std::string rateName (double fs) { char b[32]; std::snprintf (b, sizeof b, " @ %.1f kHz", fs / 1000.0); return b; }
static double db (double a) { return 20.0 * std::log10 (std::max (a, 1e-30)); }

using Buf = std::vector<std::vector<float>>;
static Buf render (double fs, const Params& p, Buf x, int block = 512, std::mt19937* rng = nullptr, bool count = false,
                   const std::vector<int>* essTruth = nullptr)
{
    Engine e;
    e.setParams (p);
    e.prepare (fs, block);
    e.setParams (p);
    if (essTruth != nullptr)
        block = 64;   // the forced decision follows the truth at the detector's hop rate
    const size_t n = x[0].size();
    std::uniform_int_distribution<int> bl (1, 2048);
    for (size_t pos = 0; pos < n;)
    {
        const int m = (int) std::min<size_t> ((size_t) (rng ? bl (*rng) : block), n - pos);
        float* io[2] = { x[0].data() + pos, x[1].data() + pos };
        if (essTruth != nullptr)
        {
            // the decision the network would make, as early as it can make it (its frame centre + look-ahead)
            const size_t at = pos + (size_t) m / 2;
            e.forceEssDecision (at < essTruth->size() && (*essTruth)[at] == 2 ? 1 : 0);
        }
        countAllocs = count;
        e.process (io, 2, m);
        countAllocs = false;
        pos += (size_t) m;
    }
    return x;
}
static Buf impulse (double fs, double sec)
{
    Buf x (2, std::vector<float> ((size_t) (sec * fs), 0.0f));
    x[0][0] = x[1][0] = 1.0f;
    return x;
}
static Params wetOnly (Params p) { p.mix = 100.0f; p.essDuck = false; p.revLowCut = 20.0f; p.revHighCut = 20000.0f; return p; }

// a 2nd-order band-pass (RBJ, constant peak gain) for octave measurements
static std::vector<float> bandpass (const std::vector<float>& x, double fs, double fc)
{
    const double w = 2.0 * kPi * fc / fs, q = 1.414, al = std::sin (w) / (2.0 * q), c = std::cos (w), a0 = 1.0 + al;
    const double b0 = al / a0, b2 = -al / a0, a1 = -2.0 * c / a0, a2 = (1.0 - al) / a0;
    std::vector<float> y (x.size());
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (int pass = 0; pass < 2; ++pass)   // twice: steeper
    {
        const auto& in = pass == 0 ? x : y;
        std::vector<float> o (x.size());
        x1 = x2 = y1 = y2 = 0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double v = b0 * in[i] + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = in[i]; y2 = y1; y1 = v;
            o[i] = (float) v;
        }
        y = o;
    }
    return y;
}
// T30 (Schroeder backward integration, -5 .. -35 dB, extrapolated to 60 dB)
static double rt60 (const std::vector<float>& x, double fs, double fc)
{
    const auto y = bandpass (x, fs, fc);
    std::vector<double> e (y.size());
    double s = 0;
    for (size_t i = y.size(); i-- > 0;)
    {
        s += (double) y[i] * y[i];
        e[i] = s;
    }
    size_t i5 = 0, i35 = 0;
    for (size_t i = 0; i < e.size(); ++i)
    {
        const double d = 10.0 * std::log10 (e[i] / e[0] + 1e-30);
        if (! i5 && d <= -5.0) i5 = i;
        if (! i35 && d <= -35.0) { i35 = i; break; }
    }
    if (i35 <= i5 + 10)
        return 0.0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const double n = (double) (i35 - i5);
    for (size_t i = i5; i < i35; ++i)
    {
        const double t = (double) i / fs, d = 10.0 * std::log10 (e[i] / e[0] + 1e-30);
        sx += t; sy += d; sxx += t * t; sxy += t * d;
    }
    const double k = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    return -60.0 / k;
}
static size_t onset (const std::vector<float>& x)
{
    float mx = 0.0f;
    for (float v : x) mx = std::max (mx, std::abs (v));
    for (size_t i = 0; i < x.size(); ++i)
        if (std::abs (x[i]) > 1e-4f * mx)
            return i;
    return 0;
}
// normalised echo density (Abel & Huang): 1 = Gaussian-dense; returns ms from the onset until it first reaches 0.9
static double densityMs (const std::vector<float>& xin, double fsIn)
{
    // echo density as heard: above 48 kHz the response is first band-limited to 20 kHz and decimated to <= 48 kHz (at
    // 96 kHz a soft-topped tail is a smoother waveform and crosses its RMS less often - the same sound, a lower count)
    std::vector<float> x0 = xin;
    double fs = fsIn;
    const int dec = fsIn > 50000.0 ? (int) std::lround (fsIn / 48000.0) : 1;
    if (dec > 1)
    {
        const int taps = 127;
        std::vector<double> h ((size_t) taps);
        const double fc = 20000.0 / fsIn;
        double sum = 0;
        for (int k = 0; k < taps; ++k)
        {
            const double m = k - (taps - 1) / 2.0;
            const double sinc = m == 0 ? 2.0 * fc : std::sin (2.0 * kPi * fc * m) / (kPi * m);
            h[(size_t) k] = sinc * (0.42 - 0.5 * std::cos (2.0 * kPi * k / (taps - 1)) + 0.08 * std::cos (4.0 * kPi * k / (taps - 1)));
            sum += h[(size_t) k];
        }
        std::vector<float> y (xin.size() / (size_t) dec);
        for (size_t i = 0; i < y.size(); ++i)
        {
            double acc = 0;
            for (int k = 0; k < taps; ++k)
            {
                const long j = (long) (i * (size_t) dec) - k + (taps - 1) / 2;
                if (j >= 0 && (size_t) j < xin.size()) acc += h[(size_t) k] * xin[(size_t) j];
            }
            y[i] = (float) (acc / sum);
        }
        x0 = y;
        fs = fsIn / dec;
    }
    const size_t o = onset (x0);
    const int w = (int) (0.02 * fs), hop = w / 4;
    for (size_t i = o; i + (size_t) w < x0.size(); i += (size_t) hop)
    {
        double ws = 0, e = 0;
        for (int k = 0; k < w; ++k)
        {
            const double h = 0.5 - 0.5 * std::cos (2.0 * kPi * k / (w - 1));
            ws += h; e += h * (double) x0[i + (size_t) k] * x0[i + (size_t) k];
        }
        const double sd = std::sqrt (e / ws);
        double cnt = 0;
        for (int k = 0; k < w; ++k)
            if (std::abs (x0[i + (size_t) k]) > sd)
                cnt += 0.5 - 0.5 * std::cos (2.0 * kPi * k / (w - 1));
        if (cnt / ws / 0.3173 > 0.9)
            return 1000.0 * ((double) (i - o) + w / 2.0) / fs;
    }
    return 1e9;
}
// interaural cross-correlation of the tail (max |r| within +-1 ms)
static double iacc (const Buf& y, double fs, double t0, double t1)
{
    const size_t a = (size_t) (t0 * fs), b = std::min (y[0].size(), (size_t) (t1 * fs));
    const int lag = (int) (0.001 * fs);
    double ll = 0, rr = 0, best = 0;
    for (size_t i = a; i < b; ++i) { ll += (double) y[0][i] * y[0][i]; rr += (double) y[1][i] * y[1][i]; }
    for (int d = -lag; d <= lag; ++d)
    {
        double s = 0;
        for (size_t i = a + (size_t) lag; i + (size_t) lag < b; ++i)
            s += (double) y[0][i] * y[1][(size_t) ((long) i + d)];
        best = std::max (best, std::abs (s) / std::sqrt (ll * rr + 1e-30));
    }
    return best;
}

static void testReverb (double fs, bool quick, int engine)
{
    const std::string R = rateName (fs) + (engine == 1 ? " [1.0 core]" : " [2.0 core]");
    const char* names[4] = { "BOOTH", "ROOM", "PLATE", "HALL" };
    for (int t = 0; t < 4; ++t)
        for (double dec : { 0.5, 1.5, 4.0 })
        {
            if (quick && dec == 4.0)
                continue;
            Params p = wetOnly (Params {});
            p.engine = engine;
            p.type = t; p.decay = (float) dec; p.preDelayMs = 0.0f; p.damping = 0.0f;
            const auto y = render (fs, p, impulse (fs, dec * 1.6 + 0.6));
            std::vector<float> m (y[0].size());
            for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (y[0][i] + y[1][i]);
            const double r = (rt60 (m, fs, 500) + rt60 (m, fs, 1000) + rt60 (m, fs, 2000)) / 3.0;
            char b[160];
            std::snprintf (b, sizeof b, "%s DECAY %.1f s: measured RT60 (T30, 500 Hz - 2 kHz) re the setting", names[t], dec);
            check (b + R, r / dec, 0.9, 1.1, "x");
            if (dec == 1.5)
            {
                std::snprintf (b, sizeof b, "%s: echo density reaches 0.9 (Gaussian-dense) after the onset", names[t]);
                check (b + R, densityMs (m, fs), 0.0, 40.0, "ms");
                std::snprintf (b, sizeof b, "%s: L / R correlation of the tail (IACC, 100 ms - 1 s)", names[t]);
                check (b + R, iacc (y, fs, 0.1, 1.0), 0.0, 0.25, "");
            }
        }
    // DAMPING: the highs decay faster
    {
        Params p = wetOnly (Params {});
        p.engine = engine;
        p.type = kPlate; p.decay = 2.0f; p.damping = 100.0f; p.preDelayMs = 0.0f;
        const auto y = render (fs, p, impulse (fs, 4.0));
        std::vector<float> m (y[0].size());
        for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (y[0][i] + y[1][i]);
        check ("DAMPING 100 %: RT60 at 8 kHz re 1 kHz" + R, rt60 (m, fs, 8000) / rt60 (m, fs, 1000), 0.4, 0.65, "x");
    }
    // PRE-DELAY: the wet path starts look-ahead + pre-delay after the input
    {
        Params p = wetOnly (Params {});
        p.engine = engine;
        p.type = kPlate; p.preDelayMs = 50.0f;
        Engine e;
        e.prepare (fs);
        const auto y = render (fs, p, impulse (fs, 0.3));
        const double first = (double) onset (y[0]) / fs * 1000.0;
        const double expect = 50.0 + 1000.0 * e.lookAheadSamples() / fs;
        // never before look-ahead + PRE-DELAY; the PLATE's first echo comes one (shortest) tank line later
        check ("PRE-DELAY 50 ms: the reverb starts after look-ahead + 50 ms (re that)" + R, first - expect, 0.0, 12.0, "ms");
    }
}

// 2.0: BASS, FREEZE, true stereo, PRE-DELAY in note values
static void testReverb2 (double fs)
{
    const std::string R = rateName (fs);
    auto mono = [] (const Buf& y) { std::vector<float> m (y[0].size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (y[0][i] + y[1][i]); return m; };
    for (float bass : { 200.0f, 50.0f })
    {
        Params p = wetOnly (Params {});
        p.type = kHall; p.decay = 1.5f; p.damping = 0.0f; p.preDelayMs = 0.0f; p.bass = bass;
        const auto m = mono (render (fs, p, impulse (fs, 1.5 * 2.0 * 1.6 + 0.6)));
        const double r = rt60 (m, fs, 125) / rt60 (m, fs, 1000);
        check (std::string ("BASS ") + (bass > 100 ? "200" : "50") + " %: RT60 at 125 Hz re 1 kHz" + R, r, bass > 100 ? 1.6 : 0.4, bass > 100 ? 2.4 : 0.65, "x");
    }
    // FREEZE: the tail holds (no decay) and takes no new input
    {
        Params p = wetOnly (Params {});
        p.type = kHall; p.decay = 1.0f; p.preDelayMs = 0.0f;
        Engine e;
        e.setParams (p);
        e.prepare (fs);
        e.setParams (p);
        Buf x = impulse (fs, 7.0);
        const size_t n = x[0].size();
        Buf y (2, std::vector<float> (n));
        for (size_t pos = 0; pos < n; pos += 256)
        {
            const int m = (int) std::min<size_t> (256, n - pos);
            if (pos >= (size_t) (0.4 * fs) && ! p.freeze) { p.freeze = true; e.setParams (p); }
            if (pos >= (size_t) (2.0 * fs) && pos < (size_t) (2.0 * fs) + 256) { x[0][pos] = x[1][pos] = 1.0f; }   // a click while frozen
            float* io[2] = { x[0].data() + pos, x[1].data() + pos };
            e.process (io, 2, m);
            for (int c = 0; c < 2; ++c) std::copy (io[c], io[c] + m, y[(size_t) c].begin() + (long) pos);
        }
        auto energy = [&] (double a, double b) { double s = 0; for (size_t i = (size_t) (a * fs); i < (size_t) (b * fs); ++i) s += (double) y[0][i] * y[0][i] + (double) y[1][i] * y[1][i]; return 10.0 * std::log10 (s / ((b - a) * fs) + 1e-30); };
        check ("FREEZE: the tail holds (energy 5.5 - 6.5 s re 1.0 - 1.5 s)" + R, energy (5.5, 6.5) - energy (1.0, 1.5), -1.0, 1.0, "dB");
        check ("FREEZE: a click while frozen does not enter (energy 2.1 - 2.6 s re 1.0 - 1.5 s)" + R, energy (2.1, 2.6) - energy (1.0, 1.5), -1.0, 1.0, "dB");
    }
    // true stereo: a source on the left gives a left-weighted reverb onset; left- and right-only sources give different tails
    {
        Params p = wetOnly (Params {});
        p.type = kPlate; p.decay = 1.5f; p.preDelayMs = 0.0f; p.width = 100.0f;
        Buf xl = impulse (fs, 2.0), xr = xl;
        for (auto& v : xl[1]) v = 0.0f;
        for (auto& v : xr[0]) v = 0.0f;
        const auto yl = render (fs, p, xl), yr = render (fs, p, xr);
        auto e = [&] (const std::vector<float>& v, double a, double b) { double s = 0; for (size_t i = (size_t) (a * fs); i < (size_t) (b * fs); ++i) s += (double) v[i] * v[i]; return s; };
        check ("TRUE STEREO: a hard-left source - the reverb's first 60 ms, left re right" + R, 10.0 * std::log10 (e (yl[0], 0.0, 0.07) / e (yl[0 + 1], 0.0, 0.07)), 2.0, 40.0, "dB");
        double c = 0, a = 0, b = 0;
        for (size_t i = (size_t) (0.05 * fs); i < (size_t) (1.0 * fs); ++i) { c += (double) yl[0][i] * yr[0][i]; a += (double) yl[0][i] * yl[0][i]; b += (double) yr[0][i] * yr[0][i]; }
        check ("TRUE STEREO: left-only vs right-only sources, the tails' correlation (left output)" + R, std::abs (c) / std::sqrt (a * b), 0.0, 0.3, "");
    }
    // PRE-DELAY SYNC: 1/8 at 120 BPM = 250 ms after the look-ahead
    {
        Params p = wetOnly (Params {});
        p.type = kHall; p.preSync = true; p.preNote = k1_8; p.bpm = 120.0;
        Engine e;
        e.prepare (fs);
        const auto y = render (fs, p, impulse (fs, 0.6));
        const double first = (double) onset (y[0]) / fs * 1000.0 - 1000.0 * e.lookAheadSamples() / fs;
        check ("PRE-DELAY SYNC 1/8 at 120 BPM: the reverb starts 250 ms after the look-ahead (re that)" + R, first - 250.0, 0.0, 12.0, "ms");
    }
}

static void testDelay (double fs)
{
    const std::string R = rateName (fs);
    Params p = wetOnly (Params {});
    p.reverbOn = false; p.delayOn = true; p.sync = true; p.note = k1_4; p.bpm = 120.0; p.feedback = 50.0f;
    p.dlyLowCut = 20.0f; p.dlyHighCut = 20000.0f;
    const auto y = render (fs, p, impulse (fs, 2.2));
    // a 1 kHz burst (20 ms, Hann) for the levels: band-limited like real material (an impulse loses its ultrasonic
    // part to the HIGH CUT on every pass at high rates)
    Buf burst (2, std::vector<float> ((size_t) (2.2 * fs), 0.0f));
    for (size_t i = 0; i < (size_t) (0.02 * fs); ++i)
        burst[0][i] = burst[1][i] = (float) (std::sin (2.0 * kPi * 1000.0 * i / fs) * (0.5 - 0.5 * std::cos (2.0 * kPi * i / (0.02 * fs))));
    const auto yb = render (fs, p, burst);
    auto energyNear = [&] (double t) {
        double e = 0;
        for (size_t i = (size_t) ((t - 0.01) * fs); i < (size_t) ((t + 0.04) * fs); ++i) e += (double) yb[0][i] * yb[0][i];
        return e;
    };
    auto peakNear = [&] (double t, int ch) {
        const size_t c = (size_t) (t * fs), w = (size_t) (0.004 * fs);
        double best = 0; size_t at = c;
        for (size_t i = c - w; i < c + w && i < y[0].size(); ++i)
            if (std::abs (y[(size_t) ch][i]) > best) { best = std::abs (y[(size_t) ch][i]); at = i; }
        return std::pair<double, double> (best, (double) at / fs);
    };
    const auto r1 = peakNear (0.5, 0), r2 = peakNear (1.0, 0), r3 = peakNear (1.5, 0);
    check ("SYNC 1/4 at 120 BPM: first repeat at 500 ms (error)" + R, std::abs (r1.second - 0.5) * 1000.0, 0.0, 0.1, "ms");
    check ("SYNC 1/4 at 120 BPM: repeats every 500 ms - 2nd and 3rd (largest error)" + R,
           std::max (std::abs (r2.second - 1.0), std::abs (r3.second - 1.5)) * 1000.0, 0.0, 0.1, "ms");
    check ("FEEDBACK 50 %: each repeat 6 dB under the last (1 kHz burst, energy, 2nd re 1st)" + R,
           10.0 * std::log10 (energyNear (1.0) / energyNear (0.5)), -6.5, -5.5, "dB");
    // free time, ping-pong
    Params q = p;
    q.sync = false; q.timeMs = 180.0f; q.pingPong = true; q.feedback = 60.0f;
    const auto z = render (fs, q, impulse (fs, 1.0));
    auto side = [&] (double t) {
        const size_t c = (size_t) (t * fs), w = (size_t) (0.004 * fs);
        double l = 0, r = 0;
        for (size_t i = c - w; i < c + w; ++i) { l = std::max (l, (double) std::abs (z[0][i])); r = std::max (r, (double) std::abs (z[1][i])); }
        return db (l / std::max (r, 1e-12));
    };
    check ("PING-PONG, 180 ms: 1st repeat on the left (L re R)" + R, side (0.18), 20.0, 400.0, "dB");
    check ("PING-PONG: 2nd repeat on the right (L re R)" + R, side (0.36), -400.0, -20.0, "dB");
    // the repeats ring out after DELAY is switched off (no cut tail)
    {
        Params a = p;
        Engine e;
        e.setParams (a);
        e.prepare (fs, 256);
        Buf x = impulse (fs, 1.2);
        double after = 0;
        for (size_t pos = 0; pos < x[0].size(); pos += 256)
        {
            if (pos == (size_t) (0.6 * fs) / 256 * 256)
            {
                a.delayOn = false;
                e.setParams (a);
            }
            float* io[2] = { x[0].data() + pos, x[1].data() + pos };
            e.process (io, 2, (int) std::min<size_t> (256, x[0].size() - pos));
        }
        for (size_t i = (size_t) (0.95 * fs); i < (size_t) (1.05 * fs); ++i)
            after = std::max (after, (double) std::abs (x[0][i]));
        check ("DELAY switched off at 0.6 s: the repeat due at 1.0 s still sounds" + R, db (after), -20.0, 0.0, "dBFS");
    }
}

// a vowel / ess voice: 0.25 s vowels (harmonic, 160 Hz) and 0.12 s esses (4-10 kHz noise), with pauses
static Buf makeVoice (double fs, double sec, std::vector<int>& kind)
{
    Buf x (2, std::vector<float> ((size_t) (sec * fs), 0.0f));
    kind.assign (x[0].size(), 0);
    std::mt19937 g (7);
    std::normal_distribution<float> nd;
    double ph = 0;
    float b1 = 0, b2 = 0;
    for (size_t i = 0; i < x[0].size(); ++i)
    {
        const double t = (double) i / fs, c = std::fmod (t, 0.6);
        float v = 0.0f;
        if (c < 0.25)
        {
            ph += 2.0 * kPi * 160.0 / fs;
            for (int h = 1; h <= 25; ++h)
                v += (float) (std::sin (h * ph) / h);
            v *= 0.12f;
            kind[i] = 1;
        }
        else if (c >= 0.3 && c < 0.42)
        {
            const float n = nd (g);   // high-passed noise ~ an S
            b1 = 0.6f * b1 + n;
            v = 0.07f * (n - 0.9f * b2);
            b2 = n;
            kind[i] = 2;
        }
        x[0][i] = x[1][i] = v;
    }
    return x;
}

static void testDucking (double fs)
{
    const std::string R = rateName (fs);
    std::vector<int> kind;
    // ESS DUCK through a clean delay copy (100 ms, no feedback): what reaches the effects, ess vs vowel
    Params p = Params {};
    p.mix = 100.0f; p.reverbOn = false; p.delayOn = true; p.sync = false; p.timeMs = 100.0f; p.feedback = 0.0f;
    p.dlyLowCut = 20.0f; p.dlyHighCut = 20000.0f; p.essDuck = true; p.essDuckDb = 12.0f;
    const auto x = makeVoice (fs, 6.0, kind);
    Params off = p;
    off.essDuck = false;
    const auto on = render (fs, p, x, 64, nullptr, false, &kind), of = render (fs, off, x, 64);
    const size_t D = (size_t) (0.1 * fs);
    double eOn = 0, eOff = 0, vOn = 0, vOff = 0;
    for (size_t i = (size_t) (1.2 * fs); i + D < x[0].size(); ++i)
    {
        const double a = (double) on[0][i + D] * on[0][i + D], b = (double) of[0][i + D] * of[0][i + D];
        if (kind[i] == 2) { eOn += a; eOff += b; }
        if (kind[i] == 1) { vOn += a; vOff += b; }
    }
    check ("ESS DUCK 12 dB (decision forced on the esses): the esses reaching the effects (re off)" + R, 10.0 * std::log10 (eOn / eOff), -14.0, -6.0, "dB");
    check ("ESS DUCK: the vowels reaching the effects (re ESS DUCK off)" + R, 10.0 * std::log10 (vOn / vOff), -0.5, 0.0, "dB");
    // VOICE DUCK: the wet lower while the voice is loud, back in the gaps
    Params v = Params {};
    v.mix = 100.0f; v.reverbOn = true; v.type = kPlate; v.decay = 2.5f; v.essDuck = false; v.voiceDuckDb = 12.0f; v.duckReleaseMs = 100.0f;
    Params v0 = v;
    v0.voiceDuckDb = 0.0f;
    const auto dk = render (fs, v, x), nd = render (fs, v0, x);
    double inV = 0, inV0 = 0, gap = 0, gap0 = 0;
    for (size_t i = (size_t) (1.2 * fs); i < x[0].size(); ++i)
    {
        const double c = std::fmod ((double) i / fs, 0.6);
        if (c > 0.1 && c < 0.24) { inV += (double) dk[0][i] * dk[0][i]; inV0 += (double) nd[0][i] * nd[0][i]; }
        if (c > 0.52 && c < 0.59) { gap += (double) dk[0][i] * dk[0][i]; gap0 += (double) nd[0][i] * nd[0][i]; }
    }
    check ("VOICE DUCK 12 dB: the reverb while the voice is loud (re no ducking)" + R, 10.0 * std::log10 (inV / inV0), -14.0, -6.0, "dB");
    check ("VOICE DUCK: the reverb in the gaps comes back (re no ducking)" + R, 10.0 * std::log10 (gap / gap0), -6.0, 0.5, "dB");
}

static void testSafety (double fs)
{
    const std::string R = rateName (fs);
    std::mt19937 rng (3);
    std::vector<int> kind;
    const auto x = makeVoice (fs, 3.0, kind);
    // MIX 0 = the input, sample for sample (and OUTPUT is exact)
    {
        Params p = factoryPresets()[1].p;
        p.mix = 0.0f;
        const auto y = render (fs, p, x);
        double e = 0;
        for (size_t i = 0; i < x[0].size(); ++i) e = std::max (e, (double) std::abs (y[0][i] - x[0][i]));
        check ("MIX 0 %: the output is the input (max difference)" + R, e, 0.0, 0.0, "");
        p.outputDb = -6.0f;
        const auto z = render (fs, p, x);
        double r = 0, s = 0;
        for (size_t i = 0; i < x[0].size(); ++i) { r += (double) z[0][i] * z[0][i]; s += (double) x[0][i] * x[0][i]; }
        check ("OUTPUT -6 dB at MIX 0 %" + R, 10.0 * std::log10 (r / s), -6.01, -5.99, "dB");
    }
    // every preset: silence in -> silence out, bad input -> finite output, block size changes nothing
    int silentBad = 0, nonFinite = 0;
    double blockDiff = 0;
    for (const auto& pr : factoryPresets())
    {
        Buf z (2, std::vector<float> ((size_t) fs, 0.0f));
        const auto zo = render (fs, pr.p, z);
        for (auto& c : zo)
            for (float v : c)
                silentBad += v != 0.0f ? 1 : 0;
        Buf bad = x;
        for (size_t i = 1000; i < bad[0].size(); i += 4801)
            bad[0][i] = (i / 4801) % 2 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
        const auto bo = render (fs, pr.p, bad);
        for (auto& c : bo)
            for (float v : c)
                nonFinite += std::isfinite (v) ? 0 : 1;
        const auto a = render (fs, pr.p, x, 512), b = render (fs, pr.p, x, 512, &rng);
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = 0; i < x[0].size(); ++i)
                blockDiff = std::max (blockDiff, (double) std::abs (a[c][i] - b[c][i]));
    }
    check ("every preset: silence in -> silence out (non-zero samples)" + R, silentBad, 0, 0, "");
    check ("every preset: NaN / Inf in -> finite output (non-finite samples)" + R, nonFinite, 0, 0, "");
    check ("every preset: blocks of 512 vs random 1-2048 (max difference)" + R, blockDiff, 0.0, 1.0e-6, "");
    // TYPE change while the reverb rings: no click (the largest sample step re the signal's)
    {
        Params p = factoryPresets()[1].p;
        Engine e;
        e.setParams (p);
        e.prepare (fs, 256);
        Buf y = x;
        for (size_t pos = 0; pos < y[0].size(); pos += 256)
        {
            if (pos == (size_t) (1.5 * fs) / 256 * 256)
            {
                p.type = kHall;
                e.setParams (p);
            }
            float* io[2] = { y[0].data() + pos, y[1].data() + pos };
            e.process (io, 2, (int) std::min<size_t> (256, y[0].size() - pos));
        }
        double stepIn = 0, stepOut = 0;
        for (size_t i = 1; i < y[0].size(); ++i)
        {
            stepIn = std::max (stepIn, (double) std::abs (x[0][i] - x[0][i - 1]));
            stepOut = std::max (stepOut, (double) std::abs (y[0][i] - y[0][i - 1]));
        }
        check ("TYPE switched while ringing: largest sample step re the input's" + R, stepOut / stepIn, 0.0, 1.6, "x");
    }
}

static void testAllocAndCpu (bool quick)
{
    std::vector<int> kind;
    const double fs = 48000.0;
    const auto x = makeVoice (fs, 4.0, kind);
    std::mt19937 rng (5);
    allocCount = 0;
    for (const auto& pr : factoryPresets())
        render (fs, pr.p, x, 512, &rng, true);
    check ("no memory allocation in process() (every preset, random blocks)", allocCount, 0, 0, "allocs");
    if (quick)
        return;
    for (const char* name : { "Vocal Plate", "Hall Hook", "Ad-lib Tail", "Real: Studio Live Room", "Real: Wood Chapel", "Reverse Swell" })
        for (const auto& pr : factoryPresets())
            if (std::string (pr.name) == name)
            {
                const auto y = makeVoice (fs, 10.0, kind);
                const auto a = std::chrono::steady_clock::now();
                render (fs, pr.p, y, 128);
                const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - a).count();
                check (std::string ("CPU, stereo @ 48 kHz, 128-sample blocks, ") + name + " (% of one core)", 100.0 * sec / 10.0, 0.0, 6.0, "%");
            }
}

//== 3.0: the delay's character and UNMASK =========================================================================
static Params delayOnly (Params p)
{
    p = wetOnly (p);
    p.reverbOn = false; p.delayOn = true; p.sync = false; p.timeMs = 250.0f; p.feedback = 50.0f; p.pingPong = false;
    p.dlyLowCut = 20.0f; p.dlyHighCut = 20000.0f; p.drive = 0.0f; p.delayDb = 0.0f; p.delayToReverb = 0.0f; p.voiceDuckDb = 0.0f;
    p.wowFlutter = 0.0f;
    return p;
}
// the energy centroid (ms) of the response inside [a, b] ms
static double centroidMs (const std::vector<float>& y, double fs, double a, double b)
{
    double e = 0, t = 0;
    for (size_t i = (size_t) (a * 0.001 * fs); i < (size_t) (b * 0.001 * fs) && i < y.size(); ++i)
    {
        e += (double) y[i] * y[i];
        t += (double) y[i] * y[i] * (double) i;
    }
    return e > 0 ? 1000.0 * t / e / fs : -1.0;
}
static void testDelay3 (double fs)
{
    const std::string R = rateName (fs);
    static const char* names[3] = { "CLEAN", "TAPE", "ANALOG" };
    for (int mode = 0; mode < 3; ++mode)
        for (float diffuse : { 0.0f, 60.0f })
        {
            if (mode == 0 && diffuse == 0.0f) continue;   // 1.0's delay: tested in testDelay
            Params p = delayOnly (Params {});
            p.dlyMode = mode; p.diffuse = diffuse;
            const auto y = render (fs, p, impulse (fs, 1.2))[0];
            const double c1 = centroidMs (y, fs, 150, 350), c2 = centroidMs (y, fs, 400, 600);
            const std::string n = std::string (names[mode]) + (diffuse > 0 ? " + DIFFUSE 60 %" : "");
            check (n + ": repeat 1 on time (250 ms, energy centroid)" + R, c1 - 250.0, -0.6, 0.6, "ms");
            check (n + ": repeat 2 on time (500 ms)" + R, c2 - 500.0, -1.0, 1.0, "ms");
        }
    // a centred (mono) voice stays centred: at DIFFUSE 0 both sides' repeats are identical in every mode
    for (int mode = 1; mode < 3; ++mode)
    {
        Params p = delayOnly (Params {});
        p.dlyMode = mode; p.wowFlutter = 50.0f;
        Buf x (2, std::vector<float> ((size_t) (1.5 * fs)));
        for (size_t i = 0; i < x[0].size(); ++i) x[0][i] = x[1][i] = (i < (size_t) (0.3 * fs)) ? 0.2f * (float) std::sin (0.07 * (double) i) * (float) std::sin (0.0011 * (double) i) : 0.0f;
        const auto y = render (fs, p, x);
        double d = 0, e = 0;
        for (size_t i = 0; i < y[0].size(); ++i) { d += std::pow ((double) y[0][i] - y[1][i], 2.0); e += std::pow ((double) y[0][i], 2.0); }
        check (std::string (names[mode]) + ": a centred voice's repeats stay centred (L - R re L)" + R, 10.0 * std::log10 (d / e + 1e-30), -400, -100, "dB");
    }
    // FEEDBACK 95 % with DRIVE, DIFFUSE and WOW: every mode dies away (the loop never gains, even saturated)
    for (int mode = 1; mode < 3; ++mode)
    {
        Params p = delayOnly (Params {});
        p.dlyMode = mode; p.feedback = 95.0f; p.drive = 60.0f; p.diffuse = 60.0f; p.wowFlutter = 60.0f; p.timeMs = 180.0f;
        Buf x (2, std::vector<float> ((size_t) (16.0 * fs)));
        std::mt19937 g (5);
        std::normal_distribution<float> nd (0.0f, 0.5f);
        for (size_t i = 0; i < (size_t) (0.3 * fs); ++i) x[0][i] = x[1][i] = nd (g);
        const auto y = render (fs, p, x)[0];
        auto pk = [&] (double a) { float m = 0; for (size_t i = (size_t) (a * fs); i < (size_t) ((a + 1.0) * fs); ++i) m = std::max (m, std::abs (y[i])); return 20.0 * std::log10 (m + 1e-12); };
        check (std::string (names[mode]) + ", FEEDBACK 95 %, DRIVE 60: 14 s later the repeats are 30 dB down or more" + R, pk (14.5) - pk (0.5), -400, -30, "dB");
    }
    // a mode change fades the repeats out and in (no click): a sustained repeated tone, CLEAN -> TAPE -> ANALOG
    {
        Params p = delayOnly (Params {});
        p.feedback = 0.0f; p.timeMs = 100.0f;
        Engine e;
        e.setParams (p);
        e.prepare (fs);
        e.setParams (p);
        const size_t n = (size_t) (2.0 * fs);
        Buf x (2, std::vector<float> (n));
        for (size_t i = 0; i < n; ++i) x[0][i] = x[1][i] = 0.3f * (float) std::sin (2.0 * kPi * 440.0 * (double) i / fs);
        std::vector<float> y (n);
        for (size_t pos = 0; pos < n; pos += 128)
        {
            if (pos == (size_t) (0.6 * fs) / 128 * 128) { p.dlyMode = 1; e.setParams (p); }
            if (pos == (size_t) (1.2 * fs) / 128 * 128) { p.dlyMode = 2; e.setParams (p); }
            const int m = (int) std::min<size_t> (128, n - pos);
            float* io[2] = { x[0].data() + pos, x[1].data() + pos };
            e.process (io, 2, m);
            std::copy (io[0], io[0] + m, y.begin() + (long) pos);
        }
        // the largest sample-to-sample step after the first repeat, re the steady tone's own largest step
        double steady = 0, worst = 0;
        for (size_t i = (size_t) (0.3 * fs); i < n; ++i)
        {
            const double d = std::abs ((double) y[i] - y[i - 1]);
            if (i < (size_t) (0.55 * fs)) steady = std::max (steady, d);
            worst = std::max (worst, d);
        }
        check ("delay mode change CLEAN > TAPE > ANALOG: no step larger than the tone's own" + R, worst / steady, 0.0, 1.05, "x");
    }
}
static void testUnmask (double fs)
{
    const std::string R = rateName (fs);
    // the voice holds a 1 kHz tone; the wet holds 1 kHz and 5 kHz: only the wet's 1 kHz steps back
    {
        Unmask u;
        u.prepare (fs);
        u.setAmount (100.0f);
        const size_t n = (size_t) (1.5 * fs);
        double e1 = 0, e5 = 0, r1 = 0, r5 = 0;
        Biquad b1, b5, c1, c5;
        b1.setBand (fs, 1000.0, 4.0); b5.setBand (fs, 5000.0, 4.0); c1.setBand (fs, 1000.0, 4.0); c5.setBand (fs, 5000.0, 4.0);
        for (size_t i = 0; i < n; ++i)
        {
            const double t = (double) i / fs;
            const float v = 0.3f * (float) std::sin (2.0 * kPi * 1000.0 * t);
            const float w1 = 0.05f * (float) std::sin (2.0 * kPi * 1000.0 * t + 1.0), w5 = 0.05f * (float) std::sin (2.0 * kPi * 5000.0 * t);
            float l = w1 + w5, r = l;
            u.process (v, l, r);
            const float o1 = b1.process (l), o5 = b5.process (l), i1 = c1.process (w1 + w5), i5 = c5.process (w1 + w5);
            if (t > 1.0) { e1 += (double) o1 * o1; e5 += (double) o5 * o5; r1 += (double) i1 * i1; r5 += (double) i5 * i5; }
        }
        check ("UNMASK 100 %: the wet steps back where the voice sings (1 kHz; up to 12 dB)" + R, 10.0 * std::log10 (e1 / r1), -13.0, -10.5, "dB");
        check ("UNMASK 100 %: the wet keeps the band the voice leaves free (5 kHz)" + R, 10.0 * std::log10 (e5 / r5), -0.5, 0.5, "dB");
    }
    // a voice gating on and off 8 times a second over a steady wet tone: the dips move smoothly (no zipper, no clicks).
    // Measured above 5 kHz with an 8th-order high-pass (the 1 kHz tone itself is > 100 dB down there), re the same
    // tone without UNMASK (the measurement's own floor)
    {
        const size_t n = (size_t) (2.0 * fs);
        auto run = [&] (bool on) {
            Unmask u;
            u.prepare (fs);
            u.setAmount (100.0f);
            std::array<Biquad, 4> h;
            for (auto& f : h) f.set (fs, 5000.0, true);
            double eh = 0, et = 0;
            for (size_t i = 0; i < n; ++i)
            {
                const double t = (double) i / fs;
                const bool gate = std::fmod (t * 8.0, 1.0) < 0.5;
                const float v = gate ? 0.3f * (float) std::sin (2.0 * kPi * 1000.0 * t) : 0.0f;
                float l = 0.1f * (float) std::sin (2.0 * kPi * 1000.0 * t + 0.5), r = l;
                if (on) u.process (v, l, r);
                float y = l;
                for (auto& f : h) y = f.process (y);
                if (i > (size_t) (0.5 * fs)) { eh += (double) y * y; et += (double) l * l; }
            }
            return 10.0 * std::log10 (eh / et + 1e-30);
        };
        const double floorDb = run (false), withDb = run (true);
        check ("UNMASK on a gated voice: artefacts above 5 kHz re the wet tone (floor without UNMASK: " + std::to_string ((int) std::lround (floorDb)) + " dB)" + R, withDb, -400, -90, "dB");
    }
    // UNMASK 0 %: the plugin's wet is untouched, bit for bit (the block is not run)
    {
        Params p = wetOnly (Params {});
        p.unmask = 0.0f;
        Buf x = impulse (fs, 1.0);
        for (size_t i = 0; i < x[0].size(); ++i) x[0][i] = x[1][i] = 0.2f * (float) std::sin (0.05 * (double) i) * (i % 4800 < 2400 ? 1.0f : 0.0f);
        const auto a = render (fs, p, x);
        Params q = p; q.unmask = 30.0f;
        const auto b = render (fs, q, x);
        Params z = q; z.unmask = 0.0f;
        const auto c = render (fs, z, x);
        double d = 0, d2 = 0;
        for (size_t i = 0; i < a[0].size(); ++i) { d = std::max (d, (double) std::abs (a[0][i] - c[0][i])); d2 = std::max (d2, (double) std::abs (a[0][i] - b[0][i])); }
        check ("UNMASK 0 %: the output is the 2.0 output, bit for bit" + R, d, 0.0, 0.0, "");
        check ("UNMASK 30 % acts (sanity)" + R, d2, 1e-4, 10.0, "");
    }
}

//== 4.0: REAL SPACES and SHAPE ====================================================================================
static void testReal (double fs, bool quick)
{
    const std::string R = rateName (fs);
    auto mono = [] (const Buf& y) { std::vector<float> m (y[0].size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (y[0][i] + y[1][i]); return m; };
    for (int k = 0; k < axs::real::kRooms; ++k)
    {
        if (quick && k % 3 != 0) continue;
        const auto& rm = axs::real::kRoomList[k];
        Params p = wetOnly (Params {});
        p.space = k + 1; p.preDelayMs = 0.0f; p.delayOn = false;
        const auto y = render (fs, p, impulse (fs, std::min (12.0, 1.7 * std::max ({ rm.eyLow, rm.eyMid, rm.eyHigh }) + 0.6)));
        const auto m = mono (y);
        const std::string n = std::string (rm.name);
        check (n + ": decay at 1 kHz re the room's physics (Eyring)" + R, rt60 (m, fs, 1000) / rm.eyMid, 0.85, 1.15, "x");
        check (n + ": decay at 250 Hz re the room's physics" + R, rt60 (m, fs, 250) / (0.5 * (rm.eyLow + rm.eyMid)), 0.6, 1.4, "x");
        check (n + ": decay at 4 kHz re the room's physics" + R, rt60 (m, fs, 4000) / (0.5 * (rm.eyMid + rm.eyHigh)), 0.6, 1.4, "x");
        // the first reflection: as late after the wet path's start as the geometry says (the wet path's look-ahead first)
        Engine e;
        e.setParams (p);
        e.prepare (fs);
        const double look = (double) e.lookAheadSamples();
        const size_t o = onset (y[0]);
        double first = 1.0e9;
        for (int q = 0; q < 4; ++q) first = std::min (first, (double) rm.taps[q][0].t);
        check (n + ": first reflection on time (re the geometry)" + R, 1000.0 * (((double) o - look) / fs - first), -0.1, 0.1, "ms");
        double eL = 0, eR = 0;
        for (size_t i = 0; i < y[0].size(); ++i) { eL += (double) y[0][i] * y[0][i]; eR += (double) y[1][i] * y[1][i]; }
        check (n + ": a centred voice's room is balanced (L re R)" + R, 10.0 * std::log10 (eL / eR), -1.0, 1.0, "dB");
    }
}
static void testShape (double fs)
{
    const std::string R = rateName (fs);
    // GATED 300 ms: a 0.5 s burst into a 3 s hall - open while it sounds and for 300 ms after, then closed
    {
        Params p = wetOnly (Params {});
        p.type = kHall; p.decay = 3.0f; p.preDelayMs = 0.0f; p.delayOn = false; p.shape = 1; p.shapeMs = 300.0f;
        Buf x (2, std::vector<float> ((size_t) (2.0 * fs)));
        std::mt19937 g (3);
        std::normal_distribution<float> nd (0.0f, 0.2f);
        for (size_t i = 0; i < (size_t) (0.5 * fs); ++i) x[0][i] = x[1][i] = nd (g);
        const auto y = render (fs, p, x)[0];
        auto lvl = [&] (double a, double b) { double e = 0; for (size_t i = (size_t) (a * fs); i < (size_t) (b * fs); ++i) e += (double) y[i] * y[i]; return 10.0 * std::log10 (e / ((b - a) * fs) + 1e-30); };
        const double during = lvl (0.35, 0.5), hold = lvl (0.62, 0.72), after = lvl (0.95, 1.2);
        check ("GATED 300 ms: the tail holds after the voice (0.12 - 0.22 s after it, re during)" + R, hold - during, -12.0, 1.0, "dB");
        check ("GATED 300 ms: then it is gone (0.45 s after the voice, re during)" + R, after - during, -400.0, -40.0, "dB");
    }
    // REVERSE 400 ms: an impulse into a 2 s hall comes back as a swell (each segment rises)
    {
        Params p = wetOnly (Params {});
        p.type = kHall; p.decay = 2.0f; p.preDelayMs = 0.0f; p.delayOn = false; p.shape = 2; p.shapeMs = 400.0f;
        const auto y = render (fs, p, impulse (fs, 2.0))[0];
        // the first full reversed segment of the tail: its last quarter louder than its first quarter
        double a = 0, b = 0;
        const size_t s0 = (size_t) (0.42 * fs), q = (size_t) (0.1 * fs);
        for (size_t i = s0; i < s0 + q; ++i) a += (double) y[i] * y[i];
        for (size_t i = s0 + 2 * q; i < s0 + 3 * q; ++i) b += (double) y[i] * y[i];
        check ("REVERSE 400 ms: the tail comes back as a swell (later part re earlier part)" + R, 10.0 * std::log10 (b / a), 3.0, 60.0, "dB");
    }
    // REVERSE keeps the level: steady noise through NATURAL and REVERSE (the two heads' windows sum to 1)
    {
        Params p = wetOnly (Params {});
        p.type = kHall; p.decay = 1.0f; p.preDelayMs = 0.0f; p.delayOn = false;
        Buf x (2, std::vector<float> ((size_t) (4.0 * fs)));
        std::mt19937 g (9);
        std::normal_distribution<float> nd (0.0f, 0.1f);
        for (size_t i = 0; i < x[0].size(); ++i) x[0][i] = x[1][i] = nd (g);
        const auto a = render (fs, p, x)[0];
        p.shape = 2; p.shapeMs = 250.0f;
        const auto b = render (fs, p, x)[0];
        double ea = 0, eb = 0;
        for (size_t i = (size_t) (1.5 * fs); i < a.size(); ++i) { ea += (double) a[i] * a[i]; eb += (double) b[i] * b[i]; }
        check ("REVERSE: the level of a steady tail as NATURAL's" + R, 10.0 * std::log10 (eb / ea), -1.5, 1.5, "dB");
    }
}

int main (int argc, char** argv)
{
    const bool quick = argc > 1 && std::strcmp (argv[1], "--quick") == 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (double fs : quick ? std::vector<double> { 48000.0 } : std::vector<double> { 44100.0, 48000.0, 96000.0 })
    {
        testReverb (fs, quick, 2);
        testReverb (fs, quick, 1);
        testReverb2 (fs);
        testDelay (fs);
        testDelay3 (fs);
        testUnmask (fs);
        testReal (fs, quick);
        testShape (fs);
        testDucking (fs);
        testSafety (fs);
    }
    testAllocAndCpu (quick);
    const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    if (fails == 0)
        std::printf ("ALL PASS: %d of %d checks passed (%.1f s)\n", checks, checks, sec);
    else
        std::printf ("FAILED: %d of %d checks (%.1f s)\n", fails, checks, sec);
    return fails;
}
