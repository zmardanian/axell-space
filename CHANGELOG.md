# Changelog

## 3.0.0 - 2026-10-30
**The delay gets a character, the effects learn to step out of the voice's way**, measured on the artist's dry vocal:
- **UNMASK** (new): the reverb and the delay step back only where they would cover the voice - per third of an octave,
  while the voice uses that band - and stay full everywhere else (the air while a vowel sings, the body while an ess
  hisses). On the artist's vocal, against VOICE DUCK at the same clarity (6 dB): 2.2 dB more of the space kept, 3.3 dB
  more in the bands the voice leaves free. Eighteen bells solved together (their overlaps cancel), gains gliding every
  sample: no zipper (artefacts at least 106 dB down). A live strip in the display shows what it carves out.
- **Delay MODE** (new): **CLEAN** (the 1.0 / 2.0 delay, bit for bit), **TAPE** (recorded on tape: soft, slightly
  asymmetric saturation at the record head, a head bump, each pass about 6.5 dB darker at 10 kHz, wow and flutter
  0.2 - 0.3 % like a real machine), **ANALOG** (a bucket-brigade line: dark from the first repeat, soft clipping, a hint
  of clock jitter). TAPE and ANALOG never run away: FEEDBACK 95 % with DRIVE dies away. A mode change crossfades
  what is heard and what is written, so repeats already on their way change character without a click.
- **WOW & FLUTTER** (new): 40 % = a healthy machine (1.5 cents), 100 % = a worn one (4 cents); a centred voice stays
  exactly centred.
- **DIFFUSE** (new): each repeat smears a little more than the last, from echoes to a soft trail - still on the beat
  (within 0.3 ms).
- **Presets**: Vocal Plate, Hall Hook, 1/4 Throw, Dotted 1/8 Space, Ad-lib Tail and both sends now use UNMASK (on the
  artist's vocal: the voice 3.3 dB less covered and the consonants clearer, for 1.5 - 2 dB less space); Slap Back is a
  tape slap (the voice 1.35 dB less covered, consonants 0.6 dB clearer); new: **Tape Echo**, **Analog Dub**, **Diffuse
  Trail**, **Unmasked Hall** (2.0's Hall Hook's consonant clarity with the voice 5.8 dB less covered).
- **Your sessions are safe**: sessions saved with 2.0 render bit-identically to 2.0.0, sessions saved with 1.0 to
  1.0.1 (every factory preset, checked by the independent audit).
- CPU 4.6 - 5.4 % of one core with UNMASK on (2.0: 3.4 - 4.1 %); UNMASK at 0 % costs nothing.
- Verified: DSP 265, plugin 34, ASan + UBSan clean, independent audit 80/80, pluginval strictness 10, VST3 validator
  47/47.

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
