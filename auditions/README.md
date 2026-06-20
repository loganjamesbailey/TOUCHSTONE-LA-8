# Touchstone audition set — the listening gate

The harness validates everything measurable. The one validator it can't run
is your ears. This set makes that pass rigorous and repeatable instead of
vibes: a fixed, labeled corpus you A/B the same way every time.

## Render it

```
cmake --build build --target touchstone_render   # once
./scripts/render_auditions.sh
```

Output (46 WAVs, 32-bit float, latency-compensated so every file lines up
sample-accurate against the dry source):

```
auditions/source/        the dry reference(s)
auditions/voice/         Voice, Intimacy {0,25,50,75,100} x 3 Mic Profiles, verse + chorus  (30)
auditions/classics/      the five classics on the same source, auto-makeup on  (10)
auditions/hq_showcase/   FET & Opto, pushed take, base vs 4x HQ  (4)
```

## Use a REAL vocal (do this before trusting the verdict)

Files tagged `synth-` come from a deterministic synthetic stem
(`tools/render/make_synth_vocal.py`). It proves the pipeline and exercises
the engine, but it is NOT a real voice — no consonants, no breath, no real
formant motion. **Drop your own takes and re-run:**

```
auditions/source/verse.wav     a quiet, intimate take   (mono or stereo, any rate)
auditions/source/chorus.wav    a pushed, loud take
./scripts/render_auditions.sh  # picks up the real files automatically, drops the 'synth-' tag
```

One known limit of the synthetic stem: Voice is outcome-defined around
*deviations from a learned baseline* (static rig tonality is learned away by
design), so a stationary synthetic tone under-demonstrates the Mic Profile
and effort effects. The generator injects intra-take effort and proximity
excursions to compensate, and the Intimacy sweep does move correctly on it
(shout-band effort variation shrinks 5.6 → 4.1 dB across Intimacy 0 → 100,
measured), but a real voice with real lean-ins and real pushes shows the
Mic Profile difference far more clearly than the synthetic spread does.

## The A/B protocol (so the verdict means something)

1. **Loudness-match first.** Unmatched bypass is the main source of
   compressor self-deception — the louder file always "sounds better."
   `tools/audio_analysis/normalize.py <target_lufs> <ceiling_dbtp> <files>`
   does a linear-gain match (no limiting) to a common LUFS with a true-peak
   ceiling — run it on a render set before A/B so every file is equal-loud
   and nothing clips. `measure.py <file>` reports LUFS/true-peak/spectral.
2. **The headline comparisons:**
   - *Does Voice level without pumping?* — `source` vs `voice_*_int000`
     (Intimacy 0 = leveling only, no spectral counter-shaping).
   - *What does Intimacy do?* — sweep `int000 → int100` on one Mic Profile.
     Intimacy is a **warm-and-close** control: higher = rolled-off top,
     a touch more low-mid body, harshness tamed when the take pushes —
     articulation (≈1–2 kHz) is left intact so it stays present, not muffled.
     (Measured on a real vocal: centroid drops ~24%, top −3.5 dB, 0→100.)
   - *Mic Profile* — same Intimacy, swap `dynClose / condClose / condFar`.
     Listen for proximity boom being stabilized, strongest on dynClose.
   - *Classics character* — `clean` (invisible) vs `fet` (fast, aggressive)
     vs `opto` (slow, smooth) vs `varimu` (gentle, thick) vs `vca` (tight).
   - *HQ payoff (item 1)* — `hq_showcase/*_base` vs `*_hq` on FET/Opto. The
     4x path should sound the same but cleaner on hot transients; the
     measured aliasing improvement is FET −69→−80 dBc, Opto −85→−92 dBc.
3. **Blind if you can.** Have someone shuffle A/B, or randomize filenames.
4. **Write the verdict down.** A one-line note per comparison is the gate's
   output — that's what turns "sounds good" into a repeatable pass.

## Regenerating after a DSP change

Re-render and the files overwrite in place; diff against a prior set (or
just re-listen to the headline pairs) to confirm a change did what you
intended and nothing you didn't.
