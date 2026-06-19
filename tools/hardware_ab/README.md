# Hardware A/B harness (scaffold)

If you rent a real 1176 / LA-2A / Fairchild 670, this lets a captured pass
through the unit be null- and spectrum-compared against the matching
Touchstone model with the same numeric discipline as the offline harness.

**Status: comparison engine complete and self-tested; awaiting physical
captures.** Run `python3 compare.py --selftest` to confirm the math
(alignment, gain-match, null depth, per-band divergence). The only missing
piece is real captures — that needs the rented unit and your interface.

## What you can and can't expect from a compressor null

Two *different* nonlinear dynamics processors never null to digital silence,
and that is not a failure. A deep broadband null (>40 dB) means the model
matches the unit on that signal at that setting. A shallower null tells you
*where* they diverge — the per-band residual table and the time alignment
localize it to the attack transient, the release tail, or HF saturation.
The tool quantifies the divergence; it does not pretend the match is linear.

## Capture protocol

1. **Calibrate the loop.** Interface out → unit in → interface in. Set unit
   to bypass/unity if it has one; send a −18 dBFS 1 kHz tone and trim the
   return so the bypassed loop reads −18 dBFS back. Note the trim — that is
   your reference gain. Disable any interface DSP, keep 48 kHz/24-bit.
2. **Drive both with the identical file.** Use one of the `auditions/source`
   stems plus the test set below. Print (record) the unit's output to a WAV.
   Render the model with `touchstone_render` set to match the unit's
   front-panel settings as closely as the controls allow.
3. **Test set** (record each through the unit; render each through the model):
   - 1 kHz tone at −24, −18, −12, −6 dBFS (static curve / knee).
   - 20 Hz→20 kHz log sweep at −12 dBFS (frequency-dependent behavior).
   - A fast-transient burst train (attack/release ballistics).
   - A real vocal stem (program-material behavior).
   Capture at several front-panel settings (light / medium / hard GR).
4. **Compare:**
   ```
   python3 compare.py --model render_fet_medium.wav --hw capture_fet_medium.wav --json result.json
   ```
   It auto-aligns (up to 1 s of slop), least-squares gain-matches, and
   reports null peak/RMS, null depth, and the residual-vs-hardware spectrum
   by octave band, plus a verdict.

## Reading the result

- `lag_samples` — capture latency through your loop; sanity-check it's stable.
- `gain_match_db` — the single linear d.o.f. removed before nulling.
- `null_depth_db` — input RMS minus residual RMS. The headline number.
- `residual_minus_hw_by_band_db` — per band, how loud the divergence is
  relative to the signal there. The most-positive band is where the model
  and the unit disagree most — that's the next thing to tune in the topology.

## Turning this into a real validator

Once captures exist, wire `compare.py` outputs into a small case in the
harness (or a `scripts/validate_hardware.sh`) that asserts a minimum null
depth per mode/setting, so hardware fidelity becomes a gated, repeatable
number like everything else — not a one-off listen. That step is deferred
until there's gear on the bench; the analysis half is built and proven here.
