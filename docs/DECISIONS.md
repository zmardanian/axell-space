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

## S8 - 2.0: the reverb core, measured against references
Space 1.0.1, a respected commercial plate (rendered through the artist's licence, measurement only) and an open-source
reverb (GPL: a benchmark, never code), matched decay, the same measures (research/revq.py): 1.0 was accurate (RT60 within
6 %) and smooth (EDC curvature 0.27 - 0.45 dB) but THIN and BRIGHT (125 Hz share -22..-26 dB vs -13..-18 for the plate;
8 kHz -4.5 vs -9..-11) and its ROOM / BOOTH rang (colour 11.9 / 12.2). 2.0: 32 lines (Hadamard), an allpass in every loop
(density; modes smeared), three-band absorption (first-order shelves at 350 Hz and 5 kHz: BASS, DECAY, DAMPING; DAMPING
100 % = highs 2.5x faster), smooth two-sine modulation per line (up to 1 ms: no audible pitch), a voicing per type (low
shelf 300 Hz +1..+3 dB, high shelf 6 kHz -3..-6.5 dB), the late level matched to 1.0's at the same settings (+-1 dB).
True stereo: L / R inputs to even / odd lines and per-side early reflections; the outputs read their own side's lines at
full weight and the other's at 0.6 (the onset leans to the source, +2.3 dB for a hard-left source; mixed tails stay
decorrelated, IACC 0.03 - 0.05). The colour measure: 200 Hz - 6 kHz only - above, it reads a steep spectral slope (a high
cut, a voicing shelf) as colour; and a single impulse of a modulated tail varies by +-1 dB with the modulation's phase, so
every colour figure is an 8-impulse mean (an earlier single-impulse comparison overstated the gains and was redone). Final:
PLATE 9.2 / HALL 8.9 / BOOTH 10.0 / ROOM 9.9 (1.0: 10.1 / 9.4 / 10.3 / 9.9). The plate's lines were lengthened (10 - 85 ms:
more modes per hertz) to reach the commercial plate (9.3); the open-source plate stays smoother (8.9).
CPU: the 64 per-sample sine calls of the modulation became rotating phasors (renormalised every 4096 samples): 6.0 - 6.9 %
-> 3.4 - 4.1 % of a core. Echo density is measured on the audible band (decimated to 48 kHz above it).
Rejected: a 4x stronger DAMPING curve (it bent the decays: EDC curvature 0.9 - 1.4 dB); 1.8 ms modulation (more pitch
wobble than 1.0); a single side-blind output (no stereo onset).

## S9 - 3.0: the delay's character and UNMASK
**TAPE / ANALOG.** The record head saturates what goes onto the line (input + feedback: TAPE tanh with a small even
term, ANALOG a soft clip; a 5 Hz DC blocker keeps the even term's offset out of the loop), the play head reads it moving
(wow 0.8 ms at 0.6 Hz = 0.30 % peak speed change, flutter 30 us at 7.1 Hz, a slow random drift; ANALOG a fifth) and
filters each pass (TAPE: 7.5 kHz two-pole and a +1.2 dB head bump at 110 Hz with the whole curve trimmed under 0 dB, so
the loop never gains; ANALOG 3.6 kHz). Measured: wow 1.5 / 4.0 cents rms at 40 / 100 %, repeat 1 at 10 kHz -7 dB re
1 kHz (TAPE) / -20 dB (ANALOG), then about -6.5 dB per pass; THD at -6 dBFS -29 dB (TAPE, 2nd -37 dB) / -28 dB (ANALOG,
odd only); FEEDBACK 95 % + DRIVE 60 + DIFFUSE 60: 14 s later 50 - 66 dB down. The reference we own (a commercial tape
echo) could not be loaded in the measurement host (it hangs while loading under Wine), so TAPE was set against
published tape-echo behaviour (wow and flutter in tenths of a percent, a darker repeat each pass, soft saturation), not
against that plugin. DIFFUSE: two short allpasses per side at the record head (1.3 / 2.9 ms left, 1.6 / 3.4 ms right),
their delay taken off each side's read time - the repeats stay on the beat, and both sides read the tape at the same
instant (a centred voice stays bit-identical left and right; reading per side with one mean compensation had combed the
repeats by 0.85 ms - found on the artist's vocal as a 7 dB mono loss, fixed before release). A MODE change runs both
characters for 10 ms of warm-up and a 20 ms crossfade of what is heard AND what is written (reading the line written in
the old character with the new head had made a step 100 ms later).

**UNMASK.** An offline ideal (dip exactly the third-octave x 20 ms cells the voice uses, nothing else) showed the room:
VOICE DUCK 6 dB's clarity for 1.9 dB of space instead of 5.7. The first real-time versions acted almost broadband (the
same space as VOICE DUCK): six wide bands, a 25 dB activity window, a 60 ms release (the dip lingered after the voice left
a band), 2nd-order detectors leaking a loud neighbour band, overlapping peaks summing to 15 dB where 9 were set,
coefficient steps every 32 samples (artefacts -37 dB). 3.0: 18 third-octave bands; 4th-order detectors; a band dips
while the voice is within 20 dB of that band's loud level AND within 30 dB of its loudest band AND the wet comes within
30 dB of the voice there; 3 ms attack, 20 ms release; the 18 gains solved together through the inverse of the bells'
overlap matrix; state-variable bells, gains gliding every sample (0.7 ms) - artefacts 106 - 109 dB down (floor 112 - 115).
On the artist's vocal (one wet gain for all variants; masking = wet / voice in the cells the voice uses): UNMASK 75 % =
-20.8 dB for 3.5 dB of space (2.4 in the free bands) vs VOICE DUCK 6 dB = -20.3 dB for 5.7 (5.7). Consonant clarity (the
2 - 6 kHz envelope range) improves less than with VOICE DUCK, which also clears the gaps between syllables - the two
combine (Unmasked Hall).
Rejected: STFT processing (latency); per-band peak tracking at 3 dB/s (a quiet band became "active" at its own level);
a 46 dB share window (a 1 kHz voice leaked into the 5 kHz detector at -45 dB).
