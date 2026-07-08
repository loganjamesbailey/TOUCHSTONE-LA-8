---
name: build-and-validate
description: Design, build, and PROVE a Touchstone DSP mode against its spec — null-test the shipping float path against the double reference, measure aliasing/THD/phase/CPU against budgets, run the offline harness, then auval + pluginval strictness 10, and write a fresh docs/measurements-<date>.json. Use when the user says "build and validate," "add a mode," "validate a mode," "prove the compressor against spec," "run the null test," or hands over a new/changed DSP topology that must pass the gate. Use when a change touches dsp/include/refcomp/ or any Topology*.h and the result has to be re-proven. Does NOT fire for LiveProfessor / live-rig routing, host setup, or non-DSP plugin-shell work — only for designing and proving compressor DSP against docs/SPECS.md.
---

# build-and-validate — Design a DSP mode and prove it

The user wants a compressor mode (new or changed) designed, implemented in the dual-path engine, and proven to the spec. The null test is the only objective proof; the harness exit code is the gate. Loop the build until every gate is green — long iterative build-to-gate work is authorized; surface only a genuinely hard blocker, and notify once on done or hard-block.

## When to use / not use

- **Use** when designing or modifying a `refcomp::Engine` mode, a saturator, a detector/ballistics stage, the ADAA path, or the HQ resampler, and the change must re-pass `docs/SPECS.md`.
- **Use** when the request is "prove it" / "null-test it" / "re-measure against budget" on existing DSP.
- **Do NOT use** for LiveProfessor live-rig routing, the JUCE plugin shell, branding/`scripts/brand_assets.py`, packaging/notarization (`scripts/package.sh`, `scripts/sign_notarize.sh`), or website work. Those are out of scope here.

## Source of truth

- `docs/SPECS.md` — every guarantee and its numeric threshold; "the harness exit code is the gate." Read it first, every time. Operative thresholds get quoted verbatim, not paraphrased.
- `docs/measurements-<latest>.json` — the last green run (currently `docs/measurements-2026-06-18.json`: `total: 188, failures: 0`). This is the baseline you must not regress.
- DSP lives in `dsp/include/refcomp/` (header-only): `Engine.h`, the seven `Topology*.h`, `ADAA.h`, `Saturators.h`, `DetectorChain.h`, `Ballistics.h`, `StaticCurve.h`, `Halfband.h`, `GainSolverQP.h`, `MathOps.h`.
- Harness cases in `tests/cases/` (`test_null_modes.cpp`, `test_opt_vs_scalar.cpp`, `test_aliasing.cpp`, `test_freq_response.cpp`, `test_latency.cpp`, `test_cpu_bench.cpp`, `test_voice.cpp`, `test_optimal.cpp`, …) build into `build/tests/refcomp_tests`.

## Execution

Run from the repo root: `/Users/James/Documents/Live Professor Project/touchstone`. Do not stop between phases for confirmation; stop only on a hard gate failure that needs a design call.

### Phase 1 — State the verified baseline (no edits yet)
Read `docs/SPECS.md` and the latest `docs/measurements-<date>.json`. Before touching code, state in plain terms:
- The mode's row in the **Per-mode models** table (detector / curve / ballistics / saturator) and its binding thresholds, quoted verbatim from SPECS.md — e.g. Clean null `≤ −110 dBFS`; FET/Opto/VariMu null `peak ≤ −90 dBFS, RMS ≤ −100 dBFS`; aliasing binding limits from the table (Clean `−90 / −110`, FET `−55 / −75`, …); freq response `±0.05 dB`; CPU `≥66x` at 48k base and `≥25x` at 96k+HQ.
- The matching numbers from the latest JSON so a regression is visible: `opt_vs_scalar.*.worst_peak_dbfs` vs `peak_limit`, `aliasing.*.worst_spur_dbc` vs `limit_dbc`, `cpu_bench.*.worst_realtime_factor` vs `budget_realtime_factor`, `latency.*` reported vs measured.
This is the contract the build has to beat. Tag any SPECS quote `[verbatim / local-KB]`.

### Phase 2 — Design the topology and name the tradeoff
Specify detector (peak/RMS, feedforward vs feedback), gain-computer curve (knee/ratio law), ballistics, and saturator `(s, b)` per the SPECS table convention. State the deliberate tradeoff in SPECS's own terms — transparency vs character, latency vs alias suppression, model accuracy vs CPU. If the design is a feedback topology (detector taps previous output, as FET/Opto), note that HQ runs the 4x cascade (53-sample latency, not 43). No "inferred-without-support" design claims — anchor each choice to a published behavioral spec or to an existing topology in `dsp/include/refcomp/`.

### Phase 3 — Implement the dual-path template
Add/modify the `Topology*.h` header so the same code instantiates both:
- `Engine<float, FastMath>` — shipping path (float audio, double control path, polynomial exp2/log2).
- `Engine<double, PreciseMath>` — scalar reference (libm, double through).
Hold the invariants in SPECS "Architecture facts": **zero allocation in `process()`** (size everything in `prepare()`), denormals flushed (FPCR FZ) identically in plugin and harness, ADAA in **residual form** `y = x + ADAA1(f(x) − x)`. New harness coverage goes in `tests/cases/` and is added to `tests/CMakeLists.txt` (the `refcomp_tests` target).

### Phase 4 — Build, then null-test shipping vs reference (the only objective proof)
```
./scripts/build.sh                         # cmake -B build … && cmake --build build -jN
./scripts/run_measurements.sh docs/_run.json   # ./build/tests/refcomp_tests > out; exit 0 iff every guarantee holds
```
The null test is `opt_vs_scalar` (`Engine<float,FastMath>` vs `Engine<double,PreciseMath>`). Hard gate: `worst_peak_dbfs ≤ peak_limit` — **−110 dBFS** for Clean/VCA/Voice, **−90 dBFS** (peak) / **−100 dBFS** (RMS) for FET/Opto/Vari-Mu; Optimal `opt_vs_scalar_peak_db ≤ −110` (strict tier, measured −156/−153). Baseline to beat: latest run nulls at −131…−158 dBFS. If the null fails, the optimized path diverges from the math — fix the kernel, do not loosen the threshold. **This loop is authorized to repeat: build → run → read failures → fix → rebuild, until `failures: 0`.**

### Phase 5 — Measure aliasing / THD / phase / CPU against budgets
Same harness run produces:
- **Aliasing** — `aliasing.<mode>.<hq>.musical.<rate>.worst_spur_dbc ≤ limit_dbc` (binding limits from the SPECS table; "extreme" config bounded at −40/−45 dBc as the documented physical ceiling). `thd_pct_997` reported per mode.
- **Frequency response / phase** — `freq_response.*.worst_dev_db ≤ limit_db` (0.05), mix 100% and 50%, HQ on/off, both rates.
- **Latency** — `latency.*` reported == measured (0 base, 43 HQ, 53 HQ in FET/Opto, 144/288 Optimal).
- **CPU** — `cpu_bench.*.worst_realtime_factor ≥ budget_realtime_factor` (66 at 48k base, 25 at 96k+HQ). A CPU regression that drops below budget is a hard fail even if audio is correct (precedent: tanh `log cosh` antiderivative blew 96k+HQ to 18.4x vs the 25x floor — rejected for the algebraic sigmoid).

### Phase 6 — Prove vs docs/SPECS.md
Walk every guarantee class in SPECS "Verified guarantees" — Structural, Frequency response, Time constants, Null, Aliasing, Analog Character, CPU, Hosts — and confirm the new run satisfies it for the touched mode × HQ on/off × 48k/96k. The harness returning exit 0 (`failures: 0`) is the binary gate; a nonzero exit means a named guarantee broke — read the failing entry, fix, return to Phase 4. Do not hand-wave a near-miss as "close enough."

### Phase 7 — Host validation (auval + pluginval strictness 10, AU + VST3)
After the offline gate is green, build/install the plugin and run:
```
./scripts/validate.sh
```
which runs `auval -v aufx Tstn Jbco`, then pluginval at `--strictness-level 10 --timeout-ms 600000` against both `~/Library/Audio/Plug-Ins/VST3/Touchstone.vst3` and `~/Library/Audio/Plug-Ins/Components/Touchstone.component`. (If pluginval is missing, `scripts/get_pluginval.sh` first.) Gate: prints `ALL HOST VALIDATION PASSED`. Mono→mono and stereo→stereo; APVTS XML state round-trips.

### Phase 8 — Write the dated measurements artifact
On a fully green run, write the canonical record:
```
./scripts/run_measurements.sh docs/measurements-$(date +%Y-%m-%d).json
```
This is the new baseline. If a SPECS threshold itself moved (a guarantee was tightened/retired by design), update `docs/SPECS.md` in the same change and quote the new operative number verbatim — never silently. Commit per the family-repo-independent project convention only if the user asks.

### Notify
Per James's autonomous-mode cadence: work the build loop silently, then send exactly one notification on done (all gates green, new JSON written) or on a genuine hard-block (missing toolchain/dependency, or a threshold that cannot be met at this CPU budget and is a real topology ceiling, not a defect) — iMessage 919-524-4459 + jbaile07@me.com, with a path, not a shrug.

## Invariants

- **The null test is the proof.** No mode ships on listening or on the curve "looking right." `opt_vs_scalar` to the spec threshold (≈ −90…−110 dBFS) is the objective bar; the harness exit code is the gate.
- **Never loosen a threshold to pass.** If the float path can't null to spec, the kernel is wrong — fix `MathOps.h`/the topology, not `tests/`. Loosening a documented guarantee requires an explicit design decision recorded in SPECS.md with the new number quoted verbatim.
- **No regression.** Every figure in the new `measurements-<date>.json` must be ≥ the last green run for the modes you didn't intend to change. Compare against `docs/measurements-2026-06-18.json` (188/0).
- **Zero allocation in `process()`; denormals flushed identically in plugin and harness.** A design that allocates on the audio thread or behaves differently between Engine instantiations fails by construction.
- **Operative thresholds are quoted verbatim from SPECS.md**, with a location cite — no paraphrasing of the guarantee numbers. Load-bearing claims about pass/fail carry their epistemic + source tag.
- **Build until right.** The build→measure→fix loop runs as long as the gate is red and the path is non-destructive; this is the authorized mode, not an exception. A hard blocker is surfaced with a diagnosis (what failed, what was tried, what was determined) — never environment-blame.
