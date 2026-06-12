# Touchstone LA-8

A leveling amplifier for macOS (AU + VST3), built around one
idea: **every claim is measured, and the measurements ship with the code.**

Six modes:

- **Voice** — not a compressor. An outcome-defined vocal engine that holds
  three things a great engineer rides by hand: level (a bidirectional
  corridor rider), vocal effort (spectral effort detection driving
  counter-tilt, shout-formant taming, and breath lift — the Intimacy
  control), and distance (mic-aware proximity stabilization). It learns
  your voice's baseline, freezes it on demand, and saves the profile with
  your session.
- **Clean** — mathematically exact digital compression. Feedforward,
  log-domain, soft knee, smooth decoupled ballistics.
- **FET** — 1176-class feedback topology, sample-serial like the hardware,
  ratio detents 4/8/12/20/All, 20–800 µs attacks, two-capacitor
  program-dependent release.
- **Opto** — LA-2A-class T4 cell with light-history memory and the
  two-stage release.
- **Vari-Mu** — Fairchild-class progressive ratio with slow ballistics and
  LF-sensitive transformer-flavoured saturation.
- **VCA** — dbx/SSL-class RMS detection with literal controls.

## Engineering claims, verified

The repository contains a JUCE-free DSP core (`dsp/include/refcomp/`), a
double-precision scalar reference implementation, and an offline harness
(`tests/`) enforcing 186 checks: bit-exact block-size invariance, the
optimized float path null-tested against the reference at −137 dBFS or
better, frequency response flat to ±0.05 dB, exact latency truth-telling
(0 samples; 43 with HQ 2x), aliasing bounds per mode, time-constant
conformance to published hardware behavior, and CPU budgets (worst mode
328x realtime at 48 kHz on an M1). Full guarantees: `docs/SPECS.md`.
Latest measurement snapshot: `docs/measurements-*.json`.

Notable engineering decisions, each driven by measurement (details in
SPECS): residual-form ADAA (textbook first-order ADAA lowpasses the wet
path by −11.7 dB at 20 kHz — measured, then fixed), antialiased detector
nonlinearities, a double-precision control path inside the float engine,
algebraic-sigmoid saturators (exact antiderivative, one sqrt), and
linear-phase FIR halfbands designed analytically at prepare time.

## Building

macOS, CMake ≥ 3.22, Apple clang. JUCE 8.0.13 is fetched automatically.

```sh
./scripts/build.sh            # plugin (AU + VST3) + test harness
./scripts/run_measurements.sh # the 186-check verification suite
./scripts/validate.sh         # auval + pluginval strictness 10
./scripts/package.sh          # universal release build + zip
```

Plugins install to `~/Library/Audio/Plug-Ins/{Components,VST3}` on build.

## License

GNU Affero General Public License v3.0 — see `LICENSE`. Touchstone is
built with [JUCE](https://juce.com) under its AGPL option. KissFFT
(BSD-3-Clause) is vendored in `third_party/kissfft` for the test harness
only and is not part of the shipped plugin.
