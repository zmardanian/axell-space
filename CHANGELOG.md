# Changelog

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
