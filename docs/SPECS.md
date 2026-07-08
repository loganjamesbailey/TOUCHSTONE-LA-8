# Touchstone — Behavioral Specifications & Verified Guarantees

Every number in this document is enforced by the offline harness
(`build/tests/refcomp_tests`); the harness exit code is the gate. Run
`scripts/run_measurements.sh` to reproduce.

## Definition of "best" and its tradeoffs

A compressor is best when its gain computation is exactly what the design
intends — no numerical artifacts, no aliasing from its nonlinearities, no
phase/frequency side effects the user didn't ask for — and its topology
space covers the proven archetypes. Tradeoffs taken deliberately:

- **Transparency vs character** — separate modes, not a blend. Clean is
  mathematically exact; analog modes commit to their colorations.
- **Latency vs aliasing suppression** — the default path is zero-latency;
  the HQ toggle buys 10–60 dB more alias suppression for host-reported
  latency of exactly 43 samples (53 in FET/Opto, which run 4x — see
  below). Linear-phase FIR halfbands were chosen over polyphase-allpass
  IIR because IIR phase rotation near Nyquist is itself "unintended
  frequency warping"; the price is the higher latency, paid only when HQ
  is on.
- **Model accuracy vs CPU** — behavioral models (published time-constant
  topologies + static curves with ADAA), not SPICE/WDF circuit simulation
  or neural networks, which blow the real-time budget for sub-audible
  gains. **Honest ceiling:** without hardware on the bench, null-test
  fidelity against a physical 1176/LA-2A/670 is unverifiable. What is
  verified is conformance to published behavioral specs plus null-grade
  internal consistency.

## Architecture facts

- DSP core: JUCE-free, header-only, `refcomp::Engine<Sample, MathPolicy>`.
- `Engine<double, PreciseMath>` — scalar reference (libm, double through).
- `Engine<float, FastMath>` — shipping path: float audio path, **double
  control path** (detector → ballistics → gain computer) with polynomial
  exp2/log2 (exact-coefficient Taylor/atanh series, error ≤ 1e-12,
  verified against libm in `opt_vs_scalar/kernels`).
- Saturators: biased algebraic sigmoid `g(u) = u/√(1+u²)` with analytic
  antiderivative `√(1+u²)−1` — one hardware sqrt per sample, exact in both
  paths. Chosen over tanh after the tanh antiderivative (`log cosh`)
  measurably blew the 96k+HQ CPU budget (18.4x vs 25x realtime).
- ADAA is **residual-form**: `y = x + ADAA1(f(x) − x)`. Plain first-order
  ADAA is a 2-tap average in its linear limit (−11.7 dB at 20 kHz at
  48 k — measured before the fix); residual form passes the linear term
  bit-exactly (flat, zero delay) while the nonlinear residual keeps full
  ADAA alias suppression. Epsilon fallback 1e-6 (double), continuity
  verified < −120 dBFS.
- Detector antialiasing stack: ADAA rectifier (envelope passes exactly;
  ripple-harmonic folds suppressed ~50 dB, verified in isolation) →
  2-pole 5 kHz detector LP (8 kHz FET) → 0.5 ms dB-domain ripple smoother
  (Clean/Opto) → ballistics → 2–3-pole 10–14 kHz smoothing of the linear
  gain. RMS modes use the exact division-free ADAA squarer
  `(x₁² + x₁x₀ + x₀²)/3`.
- HQ: Kaiser-designed linear-phase FIR halfband pair (≥110 dB stopband,
  transition 20→28 kHz at 48 k, design computed at prepare time, never
  hardcoded). Round trip exactly 43 base-rate samples at any rate; the dry
  leg passes an identical resampler pair so mix never combs.
- HQ in FET/Opto runs **4x**: these are feedback topologies (the detector
  taps the previous output sample), so sidechain and signal path are one
  serial loop — the whole loop is oversampled by a second cascaded
  halfband stage (110 dB, transition 5/24 of the 4x rate, N = 39: only
  spurs folding into the stage-1 passband need full attenuation; the rest
  is killed by the stage-1 downsampler). The stage-2 round trip is 19
  samples at 2fs, padded +1 to an integer 10 base samples → 53 total.
  The dry leg takes the matching pure 20-sample delay at 2fs instead of
  re-filtering (the stage-2 pair is passband-flat to ~1e-5 dB; mix-leg
  flatness verified at 50%). Latency is therefore mode-dependent under
  HQ and the host is re-notified on mode changes.
- Resampler FIRs run as ascending dot products over contiguous history
  buffers (taps stored reversed) — auto-vectorizable; this paid for the
  4x stage: every HQ mode got faster than the ring-buffer build it
  replaced.
- Zero allocation in `process()`; all buffers sized in `prepare()`.
  Denormals flushed (FPCR FZ) identically in plugin and harness.

## Per-mode models

| Mode | Detector | Curve | Ballistics | Saturator (s, b) |
|---|---|---|---|---|
| Clean | peak (ADAA rect) | quadratic-knee feedforward | smooth decoupled (Giannoulis) | none |
| FET | feedback (prev output), peak | loop-gain k = ratio−1, knee 24/R dB | two-capacitor: fast = dialed, slow charges 80 ms / releases 8x | 1.3, 0.25 (All: 2.0, 0.5) |
| Opto | feedback, peak | k = 2.5, 10 dB knee | T4 cell: 10 ms attack; release s1 60 ms (55%), s2 0.5–4 s by light history (2 s memory) | 0.5, 0.1 |
| Vari-Mu | RMS 10 ms | progressive r(o) = 1+(rMax−1)·o/(o+12) | branching one-pole, attack 2–100 ms, release 0.1–5 s | 0.95, 0.16 + LF drive term |
| VCA | RMS 5 ms | literal ratio/knee | branching one-pole, literal | 0.45, 0 |
| Voice | see below | rider law (below) | slew-limited bidirectional | none |
| Optimal | peak (true sample) | hard ceiling g ≤ c_n | receding-horizon QP (MPC) | none |

### Voice mode (outcome-defined vocal dynamics)

Not a classic compressor: three quantities are held instead of one.

- **Level — corridor rider.** Threshold = target; gain moves the vocal
  TOWARD the target, bidirectionally:
  `out = in + clamp((target − in)·(1 − 1/ratio), −24, +9)` dB,
  slew-limited (Attack → downward 40–4000 dB/s, Release → upward
  4–400 dB/s, log-mapped), plus a 1 ms/80 ms cap stage at target+Knee.
  A −50 dBFS gate freezes everything (silence is never lifted).
  Ratio ≤ 1.01 disengages EXACTLY (bit-exact passthrough at Intimacy 0,
  verified to a −600 dBFS residual).
- **Effort.** Vocal effort is spectral: E = 10·log10(shout 1.6–4.5 kHz /
  body 0.12–0.8 kHz band power), bands composed as hp→lp cascades
  (subtraction bands leak ~−30 dB and were measured to compress a 16 dB
  contrast to 3.5 dB). E is referenced to an 8 s adaptive baseline
  (initialized after a 100 ms detector warm-up, gated). The deviation,
  scaled by Intimacy, drives counter-tilt (±8 dB at a 1.2 kHz pivot),
  shout-formant taming (3 kHz, −10..+5 dB) and breath lift (7 kHz shelf).
- **Distance.** P = 10·log10(LF ≤120 Hz / body) vs its own adaptive
  baseline; deviations (the singer MOVING — static rig tonality is
  learned away by design) drive a 130 Hz shelf scaled by the Mic Profile
  (dynamic-close 0.8 / condenser-close 0.5 / condenser-far 0.2).
- All dynamic filters are fixed-frequency, variable-gain (dynamic-EQ
  form), so unity is bit-exact and nothing is recomputed per sample.

Voice guarantees (harness-enforced, both rates): level variance of a
±12 dB wandering source reduced ≥85% with rider-law conformance ≤1 dB
(measured 0.02 dB); effort contrast of equal-RMS dark/bright takes
(Δslope 5 dB/oct) reduced ≥40% at Intimacy +100 (measured 47%); lean-in
proximity shift reduced ≥40% on a close-dynamic profile (measured
75%/82%); aliasing ≤ −90/−110 dBc (measured −110/−126); optimized-vs-
scalar null ≤ −110 dBFS (measured −143); CPU within the global budgets
(255x at 48k, 30x at 96k+HQ).

Knob remapping: FET maps Ratio to detents {4,8,12,20, ≥19 → All(32)} and
rescales Attack/Release log-fractionally into 20–800 µs / 50–1100 ms
(dual-TC analytic release t63 ≈ 1.7× the dialed fast stage). Opto ignores
Attack/Release (fixed T4 program). Vari-Mu rescales into 2–100 ms /
0.1–5 s. Minimum effective attack everywhere: 2 samples at the engine
rate. T4 slow-stage release uses linear coefficient interpolation between
the 0.5 s and 4 s coefficients (model definition; avoids per-sample exp).

### Optimal mode (lookahead MPC limiter)

A lookahead/feedforward limiter whose gain trajectory is the solution of a
receding-horizon quadratic program (model-predictive control), not a ballistics
recursion. Per sample the ceiling is `c_n = min(1, T_lin/|x|_peak)` (true sample
peak); the gain is

    g = argmin  Σ (g − c_n)² + λ·Σ(Δg)²    subject to    0 ≤ g ≤ c_n

solved by a tridiagonal active-set method (`GainSolverQP.h`) over a 3 ms window,
re-solving every ~1 ms and committing the lookahead. The box constraint is the
**hard peak guarantee**: |output| ≤ T_lin at every sample, by construction, even
when the fixed-iteration (8-sweep) solve has not fully converged — non-convergence
costs smoothness, never the ceiling.

`λ` (the Σ(Δg)² term) is **J_0, a modulation-energy / smoothness regularizer — a
conditioning penalty only, with NO perceptual claim attached**. It is driven by
Recovery (longer recovery → smoother gain). Grab Point = ceiling; Strength /
Ease-In / Reaction are reserved (the QP, not a ratio, defines the law). The mode
runs at base rate only (the gain is smooth, so the multiply needs no alias
suppression); HQ is ignored. Latency is a fixed lookahead of `round(3 ms · fs)`
base samples (144 at 48 k, 288 at 96 k), reported to the host and re-notified on
mode change; the dry path is delay-matched so mix < 100% (parallel) stays aligned.

Optimal guarantees (harness-enforced, `test_optimal`, both rates): hard ceiling
exact (peak-over-threshold 0.000 dB); block-size invariant (partitions
1/64/333/4096, base and parallel-mix); optimized-vs-scalar null ≤ −153 dBFS
(measured −156/−153, inside the strict tier); reported latency exact.
**Engineering result, not a perceptual claim:** at matched 4 dB mean gain
reduction on a dense 48-carrier complex, Optimal's 2–8 Hz gain-modulation depth
is 2.1 dB below the VCA broadband compressor (−35.3 vs −33.1 dB re 20·log₁₀ m) —
it pumps measurably less than an accepted broadband compressor at equal
reduction. Audibility of that difference is unestablished (the TMTF detection
floor for 2–8 Hz is ≈ −25 dB; both sit below it on this material); it ships as a
transparency refinement, not a perceptual proxy.

## Verified guarantees (harness-enforced, all pass as of 2026-06-12)

**Structural** — bit-identical repeatability, reset cleanliness, and
block-size invariance (partitions 1/64/333/4096) for all 5 modes × HQ
on/off × 48k/96k.

**Frequency response** — ±0.05 dB, 20 Hz–20 kHz, all modes, mix 100% and
50%, HQ on/off, both rates (worst measured ~0.004 dB). Latency: 0 samples
base (measured 0.000), 43 samples HQ (measured 43.000 by sinc-interpolated
cross-correlation, reported value matches), 53 samples HQ in FET/Opto
(measured 53.000 for both, reported value matches).

**Time constants** — Clean attack/release t63 within 10% of dialed; VCA
15% (incl. RMS lag); FET release 25% of the dual-TC analytic value, attack
monotonic and order-correct at µs scale; Opto two-stage shape (55%/45%
split, fast stage 60 ms, slow tail history-dependent: t63 after 5 s GR >
1.2× t63 after 0.3 s); Vari-Mu 25% of mapped values.

**Optimized vs scalar null** — Clean/VCA peak ≤ −110 dBFS; FET/Opto/VariMu
peak ≤ −90 dBFS, RMS ≤ −100 dBFS, over a 4-signal corpus × all modes ×
HQ × rates.

**Aliasing** (worst off-grid spur vs fundamental, single-tone 997/4441/
9973 Hz at −6 dBFS, threshold −30, ratio 12, +18 dB drive (+12 clean),
10 ms/150 ms — "musical-heavy", ≈25 dB GR):

| Mode | 48k base | 48k HQ | 96k base | 96k HQ | binding limit (base/HQ) |
|---|---|---|---|---|---|
| Clean | −97 | −114 | −114 | −134 | −90 / −110 |
| VCA | −102 | −150 | −150 | −173 | −70 / −90 |
| Vari-Mu | −86 | −167 | −153 | −162 | −70 / −90 |
| Opto | −73 | −92 | −84 | −91 | −70 / −88 |
| FET | −59 | −80 | −69 | −86 | −55 / −75 |

(FET/Opto HQ figures are the 4x cascade, measured 2026-06-12: 48k HQ
improved −70 → −79.6 and −85 → −92.0 dBc respectively. The observed gain
is ≈10 dB per doubling of the loop rate; reaching −90 dBc in FET at 48k
would need 8–16x, which the 96k+HQ CPU budget does not buy. That is the
real ceiling of this topology at this budget, not a defect.)

Deviations from the original −120 (clean) / −90 (analog HQ) aspirations
are physical, not implementation defects: sampled peak-hold ballistics
regenerate folding control-signal harmonics from detector ripple (the
mechanism was isolated by FFT of the control voltage: e.g. ripple
harmonic 12×1994 Hz at 72 Hz below Nyquist folding to 144 Hz), and the
FET carries its hardware-true 20–800 µs feedback attack, which makes its
gain trajectory deliberately nonbandlimited. RMS-detector modes (VCA,
Vari-Mu), which feed the ballistics a smooth level, hit −86…−173 dBc.
The "extreme" config (2-sample attack + 5 ms release) is additionally
measured and bounded at −40/−45 dBc as the documented physical ceiling of
any sampled compressor at those settings; HQ is the prescribed cure.

THD at 997 Hz under the same heavy drive is reported per mode in the
harness JSON (`thd_pct_997`).

**Analog Character (flaws)** — off: bit-exactly identical to an engine
that never enabled it. On: −90 dBFS RMS noise floor, ±0.1 dB channel
imbalance, 0.08 Hz ±0.3 dB threshold drift.

**CPU** (Engine<float,FastMath>, stereo, block 512, median of 5 × 20 s,
M1): binding budgets ≥66x realtime at 48k base (≤1.5% of one P-core,
chosen so a 32-track session stays under half a core) and ≥25x at 96k+HQ
(≤4%). All modes pass; full table in the harness JSON.

**Hosts** — auval passes; pluginval strictness 10 passes for AU and VST3.
Mono→mono and stereo→stereo buses. State round-trips via APVTS XML.

## Control language

Plain language primary, engineering term as the secondary marking — the
original hardware convention (LA-2A "Leveling Amplifier", broadcast
"Recovery Time"); the jargon is later drift. Parameter IDs never change;
only display names.

| ID | Display name | Engineering term (marking) |
|---|---|---|
| threshold | Grab Point | threshold (Voice GUI: Target) |
| ratio | Strength | ratio, detents 2:1/4:1/8:1/max |
| attack | Reaction | attack (grab speed) |
| release | Recovery | release |
| knee | Ease-In | knee (Voice GUI: Window) |
| makeup | Output | makeup gain |
| schpf | Listen Filter | sidechain HPF |

GUI spec additions: gain meter labeled in plain terms ("turning down N
dB"); loudness-matched bypass (A/B at equal loudness — unmatched bypass
is the main source of compressor self-deception); "Trigger Source"
reserved for the external sidechain (v2).

## Known limitations (v1)

- Generic JUCE editor (per scope); no custom GUI, presets, AAX, Windows.
- No external sidechain bus (internal detector HPF 20–500 Hz instead).
- Stereo detector link fixed at 100%.
- The Ratio knob's "Inf" detent was folded into the top of the 1–20 range
  (FET: ≥19 = all-buttons).
- Emulation accuracy is conformance to published behavioral specs, not a
  hardware null test (no units on the bench).
- Optimal mode v1: base rate only (HQ ignored); fixed 3 ms lookahead (not
  automatable, prepare-time constant); Strength/Ease-In/Reaction reserved; not
  included in the analog null/aliasing tables (it is feedforward with a smooth
  gain — its own gates live in `test_optimal`).
