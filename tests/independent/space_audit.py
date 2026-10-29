"""Independent black-box audit of the built Axell Space VST3 through a real VST3 host (SpaceAuditHost, JUCE's VST3 hosting
code), with test signals and analysis written here from textbook definitions (numpy/scipy) - nothing uses the plugin's
own code. Exit code = number of failures. Environment: AXELL_AUDIT_HOST, AXELL_VST3, AXELL_AUDIT_WORK."""
import os, sys, time
import numpy as np
from scipy import signal as sg

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
os.environ.setdefault("AXELL_AUDIT_HOST", os.path.join(ROOT, "build/SpaceAuditHost_artefacts/Release/SpaceAuditHost"))
os.environ.setdefault("AXELL_VST3", os.path.join(ROOT, "build/AxellSpace_artefacts/Release/VST3/Axell Space.vst3"))
os.environ.setdefault("AXELL_AUDIT_WORK", os.path.expanduser("~/.cache/axell-space-audit"))
_vendored = os.path.join(ROOT, "external", "axell-core")
CORE = os.environ.get("AXELL_CORE", _vendored if os.path.isdir(_vendored) else os.path.join(os.path.dirname(ROOT), "axell-core"))
sys.path.insert(0, os.path.join(CORE, "tools"))
import lib  # noqa: E402
from lib import Run  # noqa: E402

START = time.time()
os.makedirs(os.environ["AXELL_AUDIT_WORK"], exist_ok=True)

def run_all(runs, inputs, **kw):
    return lib.run_all(runs, {k: np.asarray(v, np.float32) for k, v in inputs.items()}, **kw)

def f32(x):
    return np.asarray(x, np.float32).astype(np.float64)

results = []
def report(name, ok, detail):
    results.append((name, ok, detail))
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}", flush=True)

# wet only, the effects' own filters wide open, ESS DUCK off: the space itself
WET = {"Mix": "100", "Ess Duck": "Off", "Reverb Low Cut": "20", "Reverb High Cut": "20000", "Pre-Delay": "0"}
DLY = {"Mix": "100", "Ess Duck": "Off", "Reverb": "Off", "Delay": "On", "Delay Sync": "Off", "Delay Low Cut": "20",
       "Delay High Cut": "20000", "Drive": "0"}

def impulse(fs, secs, ch=2):
    x = np.zeros((int(secs * fs), ch)); x[0] = 1.0
    return x

def octave(x, fs, fc):
    sos = sg.butter(3, [fc / np.sqrt(2), min(fc * np.sqrt(2), 0.45 * fs)], btype="band", fs=fs, output="sos")
    return sg.sosfilt(sos, x)

def t30(x, fs, fc):
    """reverberation time from Schroeder's backward integral of one octave band, -5 to -35 dB (ISO 3382)"""
    y = octave(x, fs, fc)
    e = np.cumsum((y ** 2)[::-1])[::-1]
    d = 10 * np.log10(e / e[0] + 1e-30)
    i5, i35 = np.argmax(d <= -5), np.argmax(d <= -35)
    t = np.arange(i5, i35) / fs
    k = np.polyfit(t, d[i5:i35], 1)[0]
    return -60.0 / k

def onset(x):
    return int(np.argmax(np.abs(x) > 1e-4 * np.abs(x).max()))

def density_ms(x, fs):
    """normalised echo density (Abel & Huang 2006): fraction of samples outside one standard deviation in a 20 ms Hann
    window, over its Gaussian value erfc(1/sqrt 2); ms from the first sound until it first reaches 0.9"""
    o = onset(x); w = int(0.02 * fs); h = np.hanning(w); h /= h.sum()
    for i in range(o, len(x) - w, w // 4):
        s = x[i:i + w]
        sd = np.sqrt(np.sum(h * s * s))
        if np.sum(h * (np.abs(s) > sd)) / 0.3173 > 0.9:
            return 1000 * (i - o + w / 2) / fs
    return np.inf

def iacc(y, fs, t0, t1):
    a, b = y[int(t0 * fs):int(t1 * fs), 0], y[int(t0 * fs):int(t1 * fs), 1]
    lag = int(0.001 * fs)
    c = sg.correlate(a, b, mode="full")
    mid = len(b) - 1
    return np.max(np.abs(c[mid - lag:mid + lag + 1])) / np.sqrt(np.sum(a * a) * np.sum(b * b))

def voice(fs, secs, gain_db=-14.0, seed=11):
    """independent synthetic voice: glottal-like pulse train (f0 110-170 Hz) through three formant resonators, alternating
    with sibilants (white noise through a 7 kHz or 3.5 kHz two-pole resonance, 120 ms, raised-sine envelope) and pauses.
    Returns the signal and the (start, end) samples of the vowel and sibilant centres."""
    rng = np.random.default_rng(seed)
    n = int(secs * fs); t = np.arange(n) / fs
    f0 = 140 + 30 * np.sin(2 * np.pi * 0.37 * t)
    ph = np.cumsum(f0 / fs)
    pulses = (np.diff(np.floor(ph), prepend=0) > 0).astype(float)
    src = sg.lfilter([1], [1, -0.95], pulses)
    vow = np.zeros(n)
    for fc, bw, g in ((700, 120, 1.0), (1200, 150, 0.6), (2600, 250, 0.25)):
        r = np.exp(-np.pi * bw / fs); w = 2 * np.pi * fc / fs
        vow += g * sg.lfilter([1 - r], [1, -2 * r * np.cos(w), r * r], src)
    noise = rng.standard_normal(n)
    def res(fc, bw):
        r = np.exp(-np.pi * bw / fs); w = 2 * np.pi * fc / fs
        return sg.lfilter([1 - r], [1, -2 * r * np.cos(w), r * r], noise)
    s_hi, s_lo = res(7000, 1500), res(3500, 1200)
    x = np.zeros(n); vowels, esses = [], []
    seg = 0.45
    for k in range(int(secs / seg)):
        a = int(k * seg * fs)
        if k % 5 == 4:
            continue
        v0, v1 = a, a + int(0.30 * fs)
        env = np.minimum(1, np.minimum(np.arange(v1 - v0), np.arange(v1 - v0)[::-1]) / (0.02 * fs))
        x[v0:v1] += vow[v0:v1] * env
        s0, s1 = v1, v1 + int(0.12 * fs)
        if s1 > n:
            break
        e = np.sin(np.pi * np.arange(s1 - s0) / (s1 - s0))
        x[s0:s1] += (s_hi if k % 2 == 0 else s_lo)[s0:s1] * e * (6.0 if k % 2 == 0 else 4.0)
        vowels.append((v0 + int(0.06 * fs), v1 - int(0.04 * fs))); esses.append((s0 + int(0.03 * fs), s1 - int(0.03 * fs)))
    x = x / np.max(np.abs(x)) * 10 ** (gain_db / 20)
    return f32(np.stack([x, x], 1)), vowels, esses

# ---------------------------------------------------------------- 1. silence, MIX 0, host bypass, block sizes, mono
for fs in (44100, 48000, 96000):
    x, _, _ = voice(fs, 3.0)
    runs = [Run(f"sil{t}{fs}", fs, sets={"Reverb Type": t, "Delay": "On", "Mix": "50"}) for t in ("Booth", "Room", "Plate", "Hall")]
    ins = {r.name: np.zeros((int(1.5 * fs), 2)) for r in runs}
    runs += [Run(f"m0{fs}", fs, sets={"Mix": "0", "Delay": "On"}), Run(f"byp{fs}", fs, sets={"Bypass": "On", "Delay": "On"})]
    ins[f"m0{fs}"] = ins[f"byp{fs}"] = x
    out = run_all(runs, ins, jobname=f"basic{fs}")
    loud = max(np.abs(out[f"sil{t}{fs}"]).max() for t in ("Booth", "Room", "Plate", "Hall"))
    report(f"silence in -> silence out, every reverb type with the delay on @ {fs}", loud == 0.0, f"peak {loud:.3g}")
    e = np.abs(out[f"m0{fs}"] - x).max()
    report(f"MIX 0 %: output = input, sample for sample @ {fs}", e == 0.0, f"max error {e:.3g}")
    e = np.abs(out[f"byp{fs}"] - x).max()
    report(f"host bypass: output = input, sample for sample @ {fs}", e == 0.0, f"max error {e:.3g}")
fs = 48000
x, _, _ = voice(fs, 6.0)
P = {"Delay": "On", "Mix": "40", "Voice Duck": "6"}
runs = [Run("b512", fs, sets=P), Run("brand", fs, sets=P, blocks="random 1 4096 7"), Run("mono", fs, ch=1, sets=P, blocks="700")]
out = run_all(runs, {"b512": x, "brand": x, "mono": x[:, :1]}, jobname="blocks")
e = np.abs(out["b512"] - out["brand"]).max()
report("blocks of 512 vs random 1-4096 samples: the same output (reverb + delay + ducking)", e < 1e-6, f"max difference {e:.3g}")
report("mono track (1 in / 1 out), 700-sample blocks: finite, the effect present",
       np.isfinite(out["mono"]).all() and np.abs(out["mono"] - x[:, :1]).max() > 1e-3, f"peak {np.abs(out['mono']).max():.3f}")

# ---------------------------------------------------------------- 2. the reverb: decay time, damping, density, decorrelation
for fs in (48000, 96000):
    for typ in ("Booth", "Room", "Plate", "Hall"):
        for dec in (0.6, 2.0, 5.0):
            if fs == 96000 and dec != 2.0:
                continue
            r = Run("ir", fs, sets={**WET, "Reverb Type": typ, "Decay": str(dec), "Damping": "0"})
            y = run_all([r], {"ir": impulse(fs, dec * 1.5 + 0.5)}, jobname="ir")["ir"]
            m = y.mean(axis=1)
            rt = np.mean([t30(m, fs, fc) for fc in (500, 1000, 2000)])
            report(f"{typ}, DECAY {dec} s: measured T30 (octaves 500 Hz - 2 kHz) @ {fs}", abs(rt / dec - 1) < 0.12, f"{rt:.3f} s ({100 * (rt / dec - 1):+.1f} %)")
            if dec == 2.0:
                d = density_ms(m, fs)
                report(f"{typ}: echo density reaches 0.9 (Gaussian) after the first sound @ {fs}", d <= 50, f"{d:.0f} ms (limit 50)")
                c = iacc(y, fs, 0.1, 1.0)
                report(f"{typ}: interaural cross-correlation of the tail, 0.1-1 s @ {fs}", c < 0.3, f"IACC {c:.3f} (limit 0.3)")
for fs in (44100, 96000):
    r = Run("dmp", fs, sets={**WET, "Reverb Type": "Plate", "Decay": "2", "Damping": "100"})
    m = run_all([r], {"dmp": impulse(fs, 4.0)}, jobname="dmp")["dmp"].mean(axis=1)
    q = t30(m, fs, 8000) / t30(m, fs, 1000)
    report(f"DAMPING 100 %: the 8 kHz decay re the 1 kHz decay @ {fs}", 0.4 < q < 0.7, f"{q:.2f} x (0.5 by design)")
fs = 48000
r = Run("pd", fs, sets={**WET, "Reverb Type": "Plate", "Pre-Delay": "80"})
r0 = Run("pd0", fs, sets={**WET, "Reverb Type": "Plate", "Pre-Delay": "0"})
out = run_all([r, r0], {"pd": impulse(fs, 0.5), "pd0": impulse(fs, 0.5)}, jobname="pd")
shift = 1000 * (onset(out["pd"].mean(axis=1)) - onset(out["pd0"].mean(axis=1))) / fs
report("PRE-DELAY 80 ms: the reverb starts 80 ms later than at 0 ms", abs(shift - 80) < 0.5, f"{shift:.2f} ms")

# ---------------------------------------------------------------- 3. the delay: timing, feedback, ping-pong, tempo
for fs in (44100, 48000, 96000):
    n = int(1.6 * fs)
    burst = np.zeros((n, 2)); k = int(0.02 * fs)
    burst[:k] = (np.sin(2 * np.pi * 1000 * np.arange(k) / fs) * np.hanning(k))[:, None]
    runs = [Run("imp", fs, sets={**DLY, "Delay Time": "300", "Feedback": "50"}),
            Run("bur", fs, sets={**DLY, "Delay Time": "300", "Feedback": "50"}),
            Run("pp", fs, sets={**DLY, "Delay Time": "180", "Feedback": "60", "Ping-Pong": "On"}),
            Run("syn", fs, sets={**DLY, "Delay Sync": "On", "Delay Note": "1/4", "Feedback": "0"})]
    out = run_all(runs, {"imp": impulse(fs, 1.6), "bur": burst, "pp": impulse(fs, 1.0), "syn": impulse(fs, 1.0)}, jobname=f"dly{fs}")
    y = np.abs(out["imp"][:, 0])
    pk = [np.argmax(y[int((t - 0.01) * fs):int((t + 0.01) * fs)]) + int((t - 0.01) * fs) for t in (0.3, 0.6, 0.9)]
    err = max(abs(1000 * p / fs - 300 * (j + 1)) for j, p in enumerate(pk))
    report(f"DELAY 300 ms: repeats at 300 / 600 / 900 ms (largest error) @ {fs}", err <= 0.05, f"{err:.3f} ms")
    b = out["bur"][:, 0]
    en = [np.sum(b[int((t - 0.01) * fs):int((t + 0.04) * fs)] ** 2) for t in (0.3, 0.6)]
    g = 10 * np.log10(en[1] / en[0])
    report(f"FEEDBACK 50 %: the 2nd repeat re the 1st (1 kHz burst) @ {fs}", abs(g + 6.02) < 0.5, f"{g:.2f} dB (-6.02 expected)")
    pp = out["pp"]
    def lr(t):
        s = slice(int((t - 0.005) * fs), int((t + 0.005) * fs))
        return 20 * np.log10(np.abs(pp[s, 0]).max() / max(np.abs(pp[s, 1]).max(), 1e-12))
    report(f"PING-PONG: repeats alternate left / right (L re R at 180 / 360 ms) @ {fs}", lr(0.18) > 20 and lr(0.36) < -20,
           f"{lr(0.18):.0f} / {lr(0.36):.0f} dB")
    sy = np.abs(out["syn"][:, 0]); t = 1000 * np.argmax(sy) / fs
    report(f"SYNC 1/4 without a host tempo (120 BPM): the repeat at 500 ms @ {fs}", abs(t - 500) <= 0.05, f"{t:.3f} ms")

# ---------------------------------------------------------------- 4. ESS DUCK and VOICE DUCK on an independent voice
for fs in (44100, 48000, 96000):
    x, vowels, esses = voice(fs, 9.0)
    D = int(0.1 * fs)
    base = {**DLY, "Delay Time": "100", "Feedback": "0"}
    runs = [Run("on", fs, sets={**base, "Ess Duck": "On", "Ess Duck Amount": "12"}), Run("off", fs, sets=base)]
    out = run_all(runs, {"on": x, "off": x}, jobname=f"ess{fs}")
    skip = int(1.0 * fs)
    def seg_db(segs):
        a = sum(np.sum(out["on"][s + D:e + D, 0] ** 2) for s, e in segs if s > skip)
        b = sum(np.sum(out["off"][s + D:e + D, 0] ** 2) for s, e in segs if s > skip)
        return 10 * np.log10(a / b)
    es, vw = seg_db(esses), seg_db(vowels)
    report(f"ESS DUCK 12 dB: the esses in the effect (re ESS DUCK off) @ {fs}", es < -6, f"{es:.1f} dB (limit < -6)")
    worst = min(10 * np.log10(np.sum(out["on"][s + D:e + D, 0] ** 2) / np.sum(out["off"][s + D:e + D, 0] ** 2)) for s, e in vowels if s > skip)
    report(f"ESS DUCK: the vowels in the effect, the most-changed vowel @ {fs}", worst > -1.0, f"{worst:.2f} dB (all vowels {vw:.2f} dB)")
fs = 48000
x, vowels, esses = voice(fs, 9.0)
pv = {"Mix": "100", "Ess Duck": "Off", "Reverb Type": "Plate", "Decay": "2.5", "Duck Release": "100"}
out = run_all([Run("vd", fs, sets={**pv, "Voice Duck": "12"}), Run("v0", fs, sets={**pv, "Voice Duck": "0"})],
              {"vd": x, "v0": x}, jobname="vduck")
during = 10 * np.log10(sum(np.sum(out["vd"][s:e] ** 2) for s, e in vowels[2:]) / sum(np.sum(out["v0"][s:e] ** 2) for s, e in vowels[2:]))
seg = 0.45; pauses = [(int((k * seg + 0.15) * fs), int((k * seg + 0.43) * fs)) for k in range(4, 19, 5)]
gaps = 10 * np.log10(sum(np.sum(out["vd"][s:e] ** 2) for s, e in pauses) / sum(np.sum(out["v0"][s:e] ** 2) for s, e in pauses))
report("VOICE DUCK 12 dB: the reverb while the voice sounds (re off)", -14 < during < -6, f"{during:.1f} dB")
report("VOICE DUCK: the reverb in the pauses (re off) - it blooms back", gaps > -6, f"{gaps:.1f} dB")

# ---------------------------------------------------------------- 5. every factory preset: finite, bounded
names = ["Init", "Vocal Plate", "Booth Air", "Small Room", "Dark Plate", "Hall Hook", "Slap Back", "1/4 Throw", "1/8 Ping-Pong",
         "Dotted 1/8 Space", "Ad-lib Tail", "Send: Vocal Plate (100 % wet)", "Send: 1/4 Delay (100 % wet)"]
x, _, _ = voice(fs, 6.0)
runs = [Run(f"p{i}", fs, sets={"Program": nm}) for i, nm in enumerate(names)]
out = run_all(runs, {r.name: x for r in runs}, jobname="presets")
bad = [nm for r, nm in zip(runs, names) if not np.isfinite(out[r.name]).all() or np.abs(out[r.name]).max() > np.abs(x).max() * 2.0]
report(f"all {len(names)} factory presets: finite, peak at most +6 dB over the input's", not bad, ", ".join(bad) if bad else "ok")
diff = [nm for r, nm in zip(runs[1:], names[1:]) if np.abs(out[r.name] - out["p0"]).max() < 1e-4]
report("every preset sounds different from Init", not diff, ", ".join(diff) if diff else "ok")

# ---------------------------------------------------------------- 6. the previous release renders the same (if it is here)
prev = os.environ.get("AXELL_PREV_VST3", os.path.expanduser("~/axell-space-release/AxellSpace-1.0.0-linux-x86_64/Axell Space.vst3"))
if os.path.isdir(prev) and os.path.realpath(prev) != os.path.realpath(os.environ["AXELL_VST3"]):
    runs_new = [Run(f"n{i}", fs, sets={"Program": nm}) for i, nm in enumerate(names)]
    runs_old = [Run(f"o{i}", fs, sets={"Program": nm}) for i, nm in enumerate(names)]
    a = run_all(runs_new, {r.name: x for r in runs_new}, jobname="new")
    b = run_all(runs_old, {r.name: x for r in runs_old}, jobname="old", plugin=prev)
    e = max(np.abs(a[f"n{i}"] - b[f"o{i}"]).max() for i in range(len(names)))
    report(f"every factory preset renders bit-identically to the previous release ({os.path.basename(os.path.dirname(prev))})", e == 0.0, f"max difference {e:.3g}")
else:
    print("skip  previous release not found")

fails = sum(1 for _, ok, _ in results if not ok)
print(f"\n{'ALL PASS' if fails == 0 else 'FAILURES'}: {len(results) - fails} of {len(results)} independent checks passed ({time.time() - START:.0f} s)")
sys.exit(fails)
