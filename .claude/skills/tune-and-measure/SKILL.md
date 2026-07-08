---
name: tune-and-measure
description: Closed-loop perceptual tuning of the Touchstone compressor — render a labeled audition sweep, measure LUFS / true-peak / spectral with the canonical harness, A/B in LiveProfessor + Logic, adjust params toward the stated perceptual goal, and re-test in a loop until the goal is met. Use when the user says "tune the compressor," "dial in the Voice mode," "make it warmer / closer / less pumpy," "measure and adjust until it sounds right," or names a mode/param to converge (Intimacy, Mic Profile, attack/release, FET/Opto/VariMu/VCA character, HQ aliasing). Runs the loop autonomously — no asking between iterations. Does NOT fire when there is no measurable/audition harness to converge against (then fall back to build-and-validate), and does NOT fire for pure DSP-correctness work where the gate is `refcomp_tests` exit code alone with no perceptual target.
---

# tune-and-measure — Closed-loop perceptual tuning

The user wants a parameter (or mode) moved toward a perceptual goal — warmer, closer, less pumping, cleaner transients — and wants it converged, not guessed. This is a verifier-gated loop: render → measure → A/B → adjust → re-test, repeated until the goal is met. High autonomy is correct here. James: "run it in a loop over and over until it's right." Do not stop between iterations to ask permission; stop only on convergence or a hard block.

## When to use / not use

- **Use** when there is a measurable target and an audition corpus to converge against: a Voice/Intimacy/Mic-Profile goal, a classic's attack/release feel, an HQ aliasing target, a loudness-fair character A/B.
- **Use** when the goal is perceptual ("make it warmer") and must be backed by measurement (centroid drop, top-octave dB, LRA, GR) so "sounds good" becomes a repeatable number.
- **Do NOT use** when no audition/measure harness applies — fall back to build-and-validate (`scripts/build.sh` → `scripts/validate.sh`).
- **Do NOT use** for pure DSP-correctness changes whose only gate is `build/tests/refcomp_tests` exit code with no listening target — that is the offline harness, run it directly.

## Verified baseline (state before acting)

Read these and state the current ground truth BEFORE rendering anything:

- **Source of truth for guarantees:** `docs/SPECS.md` — "the harness exit code is the gate." Every number you target must not violate a guarantee it enforces (e.g. HQ latency exactly 43 samples / 53 in FET/Opto; HQ alias suppression FET −69→−80 dBc, Opto −85→−92 dBc).
- **Prior measured numbers:** the latest `docs/measurements-<date>.json` and `auditions/README.md`. The README already pins the perceptual targets you are converging toward — Intimacy 0→100 on a real vocal: centroid drops ~24%, top −3.5 dB; articulation (~1–2 kHz) left intact; shout-band effort variation 5.6→4.1 dB. Quote the actual prior numbers; do not invent a baseline.
- **Build state:** the loop needs `build/tools/render/touchstone_render` and (for the safety gate) `build/tests/refcomp_tests`. If either is missing, build first: `cmake --build build --target touchstone_render` (or full `./scripts/build.sh`).
- **Source reality:** files tagged `synth-` are the deterministic synthetic stem (`tools/render/make_synth_vocal.py`), NOT a real voice. Per `auditions/README.md` the synthetic stem under-demonstrates Mic Profile / effort. If `auditions/source/verse.wav` and `auditions/source/chorus.wav` exist, the harness uses them and drops the `synth-` tag. If only synthetic stems are present, say so explicitly in every verdict — do not let a placeholder masquerade as the real verdict.

## Execution loop

Run the following loop. Each pass is fully automatic; iterate without asking.

### 0 — Pin the goal and the metric (once)
Translate the perceptual goal into a measured target and the param(s) that move it. Examples grounded in this engine's CLI (`tools/render/render.cpp`, units are user-facing dB/ms/%/Hz):
- "warmer / closer" → raise `--intimacy` (0..100 → −1..1 internally); target = centroid down, top octave (`air`/`high` bands in `measure.py`) down, ~1–2 kHz (`mid`/`highmid`) intact.
- "less pumping" → slower `--attack` / longer `--release`, or `--mode voice` Intimacy 0 (leveling only); target = lower short-term LRA swing, GR ~6–10 dB.
- "cleaner transients" → `--hq`; target = the SPECS aliasing dBc numbers, same tone otherwise.
Write the goal + numeric target down — it is the loop's exit condition.

### 1 — Render the labeled sweep
Render audition WAVs across the parameter sweep so they sort and A/B cleanly. For the standard corpus run the driver:
```
./scripts/render_auditions.sh
```
It writes 46 latency-compensated 32-bit-float WAVs to `auditions/voice/` (Intimacy {0,25,50,75,100} × {dynClose,condClose,condFar} × verse+chorus), `auditions/classics/` (clean/fet/opto/varimu/vca, `--automakeup`), and `auditions/hq_showcase/` (FET & Opto base vs `--hq`). For a focused tune, render only the swept axis directly through the same shipping engine, e.g.:
```
build/tools/render/touchstone_render --in auditions/source/verse.wav --mode voice \
  --intimacy 75 --mic 0 --out auditions/voice/verse_voice_dynClose_int075.wav
```
Keep the labeled filename convention (`{tag}{take}_{mode}_{micname}_int{NNN}.wav`) so files sort and A/B in order.

### 2 — Measure (LUFS / true-peak / spectral)
Measurement is the convergence record — numbers, not prose. Loudness-match first so the A/B compares the processor, not a level difference (`auditions/README.md`: "Unmatched bypass is the main source of compressor self-deception"):
```
python3 tools/audio_analysis/normalize.py <target_lufs> <ceiling_dbtp> auditions/voice/*int*.wav
python3 tools/audio_analysis/measure.py <file.wav>   # one JSON object/file: lufs_i, lra_lu, true_peak_dbtp, crest_db, spectral_centroid_hz, band_db_rel{...}, f0_median_hz
```
Capture the JSON for every file in the sweep (one object per file) and diff it against the goal target from step 0 and the prior baseline. Convergence data lives in these JSON objects (and, when you snapshot a pass, a `docs/measurements-<date>.json`-style file), not in narration.

### 3 — A/B in LiveProfessor + Logic
Load the matched renders and listen against the dry source per the `auditions/README.md` protocol: source vs `voice_*_int000` (does it level without pumping?), sweep `int000→int100` on one Mic Profile (what does Intimacy do?), swap `dynClose/condClose/condFar` at fixed Intimacy (Mic Profile), `*_base` vs `*_hq` (HQ payoff). Blind/shuffle if possible. Write one verdict line per comparison — that line is the listening gate's output.
- If James is at the desk, drive the host directly: `mcp__logic-pro__*` to load/transport/bounce in Logic; LiveProfessor for the live A/B rack. If running headless with no listener available, the audible step cannot be self-satisfied — say so and gate on the measured numbers (step 2) plus James's listening verdict, rather than asserting a perceptual pass you did not hear.

### 4 — Adjust toward the goal
Move the param(s) in the direction the measurement + verdict indicate (e.g. centroid still too high → raise Intimacy; pumping audible → lengthen `--release`). Make one coherent adjustment per pass so the next measurement attributes the change. If the change is in the DSP itself (not just a render param), rebuild the render target before re-rendering.

### 5 — Re-test → repeat
Re-render (step 1), re-measure (step 2), re-A/B (step 3). **Loop until the goal's numeric target AND the listening verdict are both met.** No confirmation between iterations.

## Binary gates

- **Safety gate (every loop that touches DSP, not just render params):** the offline harness exit code is the spec gate. Run `./scripts/run_measurements.sh` (→ `build/tests/refcomp_tests`); exit 0 iff every guarantee in `docs/SPECS.md` holds. A non-zero exit hard-blocks the loop — a perceptual "improvement" that breaks a verified guarantee is not an improvement. Stop and surface it.
- **Loudness-fairness gate:** never A/B or declare a perceptual verdict on un-normalized files. `normalize.py` must have run on the set first (linear gain only, no limiting) so nothing clips and loudness is equal.
- **Convergence gate (loop exit):** stop when the step-0 numeric target is met in the `measure.py` JSON AND the step-3 listening verdict is positive. If the metric converges but the ears disagree (or vice versa), that is a real divergence — surface it, do not paper over it.
- **Non-convergence:** if N passes (default cap 6) move the metric the wrong way or the same defect persists across two consecutive passes, stop and surface the residual with the measured trend — do not keep looping blind.

## Notify once

Report only on done or hard-block — not per iteration:
```
TUNE COMPLETE
─────────────
Goal:        <perceptual goal + numeric target>
Param(s):    <what moved, from → to>
Passes:      <N>
Source:      REAL (verse/chorus.wav) | SYNTHETIC (synth- tagged)
Measured:    <key before → after numbers: centroid / top-octave dB / LRA / GR / dBTP>
Listening:   <one-line verdict per headline comparison>
Spec gate:   refcomp_tests PASS | BLOCKED <which guarantee>
Files:       auditions/<...> renders + measurement JSON
```

## Invariants

- **Loudness-match before every A/B.** Equal-loud or the verdict is self-deception.
- **Never trade away a `docs/SPECS.md` guarantee for feel.** The harness exit code outranks the ears on enforced numbers (latency, alias dBc, exactness).
- **Renders go through the shipping engine** (`touchstone_render` = `refcomp::Engine<float, FastMath>`, the plugin's path) so what you tune is what ships. Do not tune against a divergent stand-in.
- **Convergence lives in measurement files,** not prose. Every claimed improvement carries its before/after number.
- **Synthetic source is flagged, always.** A `synth-`-tagged verdict is provisional until re-run on real `auditions/source/{verse,chorus}.wav`.
- **Re-renders overwrite in place;** if a pass needs to be preserved for diffing, snapshot the measurement JSON (date-stamped like `docs/measurements-<date>.json`) before the next render.
