# Changelog

## 2.0.0 - 2026-10-30
**A new reverb core**, measured on the artist's dry vocal and against a respected commercial plate (the artist's own
licence; measurement only) and an open-source hall / room / plate (a benchmark only):
- **32 lines with diffusion inside every loop** (1.0: 16, none): Gaussian echo density within 10 - 15 ms, and less modal
  colour - averaged over 8 impulses (a single snapshot of a modulated tail varies by +-1 dB), 200 Hz - 6 kHz, a noise-like
  tail = 8.4: PLATE 9.2 (1.0: 10.1; the commercial plate 9.3, the open plate 8.9), HALL 8.9 (1.0: 9.4; open hall 9.9),
  BOOTH 10.0 (1.0: 10.3), ROOM 9.9 (1.0: 9.9; open room 11.2).
- **Three-band decay**: lows (BASS, new: 50 - 200 % of DECAY), mids (DECAY), highs (DAMPING) each with their own decay;
  **every type voiced** - a warmer low end and a softer top than 1.0's thin, bright tail (125 Hz: -19 dB vs -22..-26 of the
  wet's energy; 8 kHz: -6 vs -4.5).
- **True stereo**: the left and right inputs feed different lines and early reflections - a wide or double-tracked vocal
  keeps its image in the tail (1.0 summed the input to mono); a hard-left source leans left in the onset (+2.3 dB).
- **On the artist's vocal** (a send, the wet matched in loudness): consonants smeared 2.0 dB (1.0: 3.0; the commercial
  plate 2.2), esses in the tail 19 dB under the vowels (1.0: 14), with ESS DUCK 28 dB (1.0: 23) - the clearest of all
  measured.
- **FREEZE** (new): the tail holds (within 0.5 dB), nothing new comes in. **PRE-DELAY SYNC** (new): note values of the
  tempo.
- **Your sessions are safe**: a session saved before 2.0 keeps the 1.0 reverb, bit for bit (every factory preset with the
  1.0 core renders identically to 1.0.1); new instances and the factory presets use the 2.0 core; ENGINE chooses.
- CPU 3.4 - 4.1 % of one core (1.0: 2.7 - 3.3).
- Verified: DSP 205, plugin 33, ASan + UBSan clean, independent audit 74/74, pluginval strictness 10, VST3 validator
  47/47.

## 1.0.1 - 2026-10-29
- **The preset name is saved with the project** (and per A/B slot): a session or an Axell Signature Dojo chain reopens
  showing its preset instead of "Init". Audio bit-identical to 1.0.0 (every factory preset, checked by the
  independent audit); projects saved with 1.0.0 load unchanged.
- Verified: DSP suite 118 checks, plugin suite 29, ASan + UBSan clean, independent audit 69/69, pluginval
  strictness 10, Steinberg's VST3 validator 47/47.

## 1.0.0 - 2026-10-29
- **First release**: a reverb (BOOTH / ROOM / PLATE / HALL: a 16-line feedback delay network with Hadamard mixing,
  per-line damping, slow modulation of every line, input diffusion and image-source early reflections) and a
  tempo-synced delay (sample-exact, feedback filters, ping-pong, drive, into the reverb), for vocals.
- **ESS DUCK**: the suite's LIVE sibilance network (Axell Velvet 3.0) keeps the esses out of the effects: -12 dB on
  esses, vowels untouched; the dry voice has no latency.
- **VOICE DUCK**: the effects step back while the voice is loud and bloom in the gaps.
- 13 factory presets, A/B, Axell's Dojo look (Space's seal: 響, "resonance", a violet ground).
- Verified: DSP suite 118 checks, plugin suite 25, ASan + UBSan clean, independent audit 68/68, pluginval
  strictness 10, Steinberg's VST3 validator 47/47.
