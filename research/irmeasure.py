"""irmeasure.py: impulse-response quality of Axell Space (SpaceRender --impulse) - RT60 per octave (T30, Schroeder),
echo density (normalised echo density, Abel & Huang: 1 = Gaussian-dense), when it reaches 0.9 (ms), tail colour
(how far the finest spectral peaks of the late tail stand above a 1/3-octave-smoothed envelope: a noise-like tail
has its 99.9th percentile near +8.4 dB, metallic modes go far above), and interaural cross-correlation of the tail."""
import sys, subprocess, numpy as np, soundfile as sf
from scipy import signal as sg
import os
R = os.environ.get("SPACE_RENDER", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "SpaceRender")); FS = 48000
def ir(keys, sec=6.0, fs=FS, out="ir.wav"):
    subprocess.run([R, "--impulse", out, str(sec), str(fs)] + [f"{k}={v}" for k, v in keys.items()], check=True)
    return sf.read(out)[0]
def rt60(x, fs, fc):
    b, a = sg.butter(3, [fc / 2 ** 0.5 / (fs / 2), min(0.99, fc * 2 ** 0.5 / (fs / 2))], "band"); y = sg.lfilter(b, a, x)
    e = np.cumsum(y[::-1] ** 2)[::-1]; edc = 10 * np.log10(e / e[0] + 1e-30); t = np.arange(len(x)) / fs
    i5, i35 = np.argmax(edc <= -5), np.argmax(edc <= -35)
    if i35 <= i5: return np.nan
    k = np.polyfit(t[i5:i35], edc[i5:i35], 1)[0]; return -60.0 / k
def onset(x):
    return int(np.argmax(np.abs(x) > 1e-4 * np.abs(x).max()))
def ned(x, fs, t90=True):
    x = x[onset(x):]                      # from the first audible sample (Space's wet path starts 8 ms late)
    w = int(0.02 * fs); out = []
    for i in range(0, len(x) - w, w // 4):
        s = x[i:i + w] * np.hanning(w); sd = np.sqrt(np.sum(s ** 2) / np.sum(np.hanning(w) ** 2))
        out.append(np.sum((np.abs(x[i:i + w]) > sd) * np.hanning(w)) / np.sum(np.hanning(w)) / 0.3173)
    out = np.array(out); k = np.argmax(out > 0.9)
    return (k * (w // 4) + w / 2) / fs * 1000 if out.max() > 0.9 else np.nan, float(np.median(out[len(out) // 4: len(out) // 2]))
def colour(x, fs, t0=0.15, t1=0.8):
    s = x[int(t0 * fs):int(t1 * fs)]; S = np.abs(np.fft.rfft(s * np.hanning(len(s)))) ** 2; f = np.fft.rfftfreq(len(s), 1 / fs)
    m = (f > 200) & (f < 12000); dB = 10 * np.log10(S + 1e-30); env = np.zeros_like(dB)
    for i in np.where(m)[0]:
        sel = (f > f[i] / 2 ** (1 / 6)) & (f < f[i] * 2 ** (1 / 6)); env[i] = 10 * np.log10(S[sel].mean() + 1e-30)
    d = (dB - env)[m]; return float(np.percentile(d, 99.9)), float(d.max())
def iacc(L, R, fs, t0=0.1, t1=0.6):
    l, r = L[int(t0 * fs):int(t1 * fs)], R[int(t0 * fs):int(t1 * fs)]; c = sg.correlate(l, r, "full") / np.sqrt(np.sum(l ** 2) * np.sum(r ** 2))
    mid = len(c) // 2; k = int(0.001 * fs); return float(np.abs(c[mid - k:mid + k + 1]).max())
if __name__ == "__main__":
    for t, name in enumerate(["Booth", "Room", "Plate", "Hall"]):
        for dec in ([0.4, 1.0, 2.5] if t < 2 else [1.0, 2.5, 5.0]):
            y = ir({"type": t, "decay": dec, "predelay": 0, "damping": 0, "revlow": 20, "revhigh": 20000, "early": 50})
            L, Rr = y[:, 0], y[:, 1]; m = 0.5 * (L + Rr)
            rts = [rt60(m, FS, fc) for fc in (500, 1000, 2000)]; t90, nd = ned(m, FS); p999, pmax = colour(m, FS, 0.05 + 0.1 * (dec > 0.5), min(0.05 + dec, 1.0))
            print(f"{name:5s} decay {dec:3.1f} s: RT60 500/1k/2k {rts[0]:.2f}/{rts[1]:.2f}/{rts[2]:.2f} s ({np.nanmean(rts)/dec*100-100:+.0f} %), "
                  f"echo density 0.9 at {t90:.0f} ms (tail {nd:.2f}), colour p99.9 {p999:+.1f} dB max {pmax:+.1f}, IACC {iacc(L, Rr, FS):.2f}", flush=True)
