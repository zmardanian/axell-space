# Design decisions

Each entry: what was chosen, what it was measured against, and what was rejected.

## S1 - One feedback delay network for all four types (1.0)
16 delay lines, mixed by a fast Walsh-Hadamard transform (x 1/4: lossless), prime lengths spread over a span per type
(BOOTH 3-22 ms, ROOM 6-45 ms, PLATE 9-60 ms, HALL 22-140 ms) scaled by SIZE. Each line has a one-pole absorbing filter
whose gain at DC gives the set RT60 and whose gain at 8 kHz gives the damped RT60 (Jot & Chaigne). Six input
allpasses diffuse the attack. Measured: T30 within 0-5 % of the knob for every type at 0.6-5 s and at 44.1-192 kHz.

## S2 - Modulation on every line, slow and shallow (1.0)
All 16 lines are read with cubic Lagrange interpolation at a slowly moving position (0.35 Hz base rate, per-line rates
0.6-1.9 x, depth up to 2 ms x MOD). Measured on 2.5 s impulse responses: tail colour (finest spectral peaks over the
1/3-octave envelope; white noise: +8.1 dB) Booth / Room / Plate / Hall +17.3 / +14.9 / +14.5 / +11.8 dB without
modulation, +10.5 / +11.0 / +10.2 / +9.4 dB at 50 % (a well-regarded free open-source reverb: +10.0 to +11.9 dB). Rejected: a fast LFO (about 30 cents of pitch wobble on held notes);
allpasses inside the loop (no gain in colour, slower echo-density build-up) - reverted.

## S3 - Early reflections from a room's geometry (1.0)
BOOTH / ROOM / HALL: image-source reflections of a shoebox up to the 3rd order (at most 128 taps, Allen & Berkley)
with per-side diffusers, so the first 10-60 ms sound like a room, not a pattern. PLATE has none (a plate has no walls).
Echo density reaches 0.9 of Gaussian 10-20 ms after the first sound.

## S4 - The delay reads twice (1.0)
The wet path starts 8 ms late (the ess look-ahead). A naive delay put every repeat 8 ms late (a 1/4 at 120 BPM came
at 492 / 992 / 1484 ms). The output is read at T - 8 ms and the feedback at T: every repeat is exact to the sample
(500.000 ms) and feedback stays exact (-6.02 dB at 50 %).

## S5 - ESS DUCK from the suite's LIVE sibilance network (1.0)
The core's SibilanceDetector in its causal LIVE configuration (Axell Velvet 3.0's network) decides per hop; the
effects' input is delayed by its frame delay + hop (384 samples at 48 kHz) so the decision arrives before the ess. The
dry signal is not delayed: no latency for the host. Measured on the artist's dry vocal: esses in the effects -12 dB
(median), 85 % of ess frames > 6 dB down, vowel frames 0 % touched; on an independent synthetic voice -12.0 dB / 0.00 dB.
Rejected: a high-band level detector (it also ducks bright vowels and hi-hat bleed).

## S6 - VOICE DUCK from the voice's level re its recent peak (1.0)
A 5 ms / 25 ms envelope against a peak that falls 1 dB/s: full ducking within 12 dB of the peak, none 24 dB under it
(a pause or a breath), with the RELEASE knob for the way back. Measured: -11.8 dB under the voice, back within 0.7 dB in
the pauses. A first version followed the voice with the RELEASE time and stayed ducked through short pauses (-12 dB);
rejected.

## S7 - Exactness (1.0)
Every gain glides (~20 ms) and lands exactly on its target: MIX 0 % and host bypass give the input bit for bit
(tested at 44.1-192 kHz through a real host). No latency, no allocation while processing, block-size independent.
