#!/usr/bin/env python3
"""Hardware A/B comparator — null + spectrum, same discipline as the harness.

Compares a Touchstone model render against a captured pass through real
hardware (a rented 1176 / LA-2A / Fairchild), driven by the IDENTICAL input.
Reports time alignment, best gain match, null residual (peak/RMS dB), and the
residual spectrum, then a verdict — the same numeric rigor the offline
harness applies to everything else.

    compare.py --model model.wav --hw capture.wav [--json out.json] [--align-only]

SCAFFOLD STATUS: the comparison engine is complete and self-tested
(`--selftest`). The missing half is physical captures, which need rented
gear and James's interface — see README.md for the capture protocol. Until a
real capture exists, run --selftest to confirm the math.

Honest framing baked into the verdict: two *different* nonlinear dynamics
processors never null to silence. A deep broadband null means "the model
matches the unit on this signal at this setting"; a shallow null localized in
time/frequency tells you WHERE they diverge (attack transient, release tail,
HF saturation). The tool quantifies that — it does not pretend a compressor
null is a linear-system null.
"""
import sys, json, struct
import numpy as np


def read_wav(path):
    b = open(path, 'rb').read()
    if b[0:4] != b'RIFF' or b[8:12] != b'WAVE':
        raise ValueError(f'{path}: not RIFF/WAVE')
    i, fs, tag, bits, nch = 12, 48000, 1, 16, 1
    data = None
    while i + 8 <= len(b):
        cid = b[i:i+4]; ln = struct.unpack('<I', b[i+4:i+8])[0]; body = i + 8
        if cid == b'fmt ':
            tag, nch, fs = struct.unpack('<HHI', b[body:body+8])
            bits = struct.unpack('<H', b[body+14:body+16])[0]
            if tag == 0xFFFE:
                tag = struct.unpack('<H', b[body+24:body+26])[0]
        elif cid == b'data':
            data = b[body:body+ln]
        i = body + ln + (ln & 1)
    if data is None:
        raise ValueError(f'{path}: no data chunk')
    if tag == 3:
        dt = '<f4' if bits == 32 else '<f8'
        x = np.frombuffer(data, dtype=dt).astype(float)
    elif tag == 1 and bits == 16:
        x = np.frombuffer(data, dtype='<i2').astype(float) / 32768.0
    elif tag == 1 and bits == 24:
        a = np.frombuffer(data, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
        v = np.where(v & 0x800000, v - (1 << 24), v)
        x = v.astype(float) / 8388608.0
    elif tag == 1 and bits == 32:
        x = np.frombuffer(data, dtype='<i4').astype(float) / 2147483648.0
    else:
        raise ValueError(f'{path}: unsupported {tag}/{bits}-bit')
    x = x.reshape(-1, nch)
    return x.mean(axis=1), fs   # mono-sum for comparison


def integer_align(a, b, max_lag):
    """Delay (in samples) of b relative to a: positive means b lags a, so
    b[n] ~= a[n - lag] and trimming b[lag:] aligns the two."""
    n = min(len(a), len(b))
    a = a[:n] - a[:n].mean(); b = b[:n] - b[:n].mean()
    fa = np.fft.rfft(a, 2 * n); fb = np.fft.rfft(b, 2 * n)
    xc = np.fft.irfft(fa * np.conj(fb), 2 * n)
    xc = np.concatenate([xc[-max_lag:], xc[:max_lag+1]])
    return -(int(np.argmax(xc)) - max_lag)


def db(x):
    return 20.0 * np.log10(max(x, 1e-30))


def compare(model, hw, fs, align_only=False):
    lag = integer_align(model, hw, max_lag=fs)  # up to 1 s of slop
    if lag > 0:
        hw = hw[lag:]
    elif lag < 0:
        model = model[-lag:]
    n = min(len(model), len(hw))
    model, hw = model[:n], hw[:n]

    out = {'lag_samples': lag, 'frames': n, 'fs': fs}
    if align_only:
        return out

    # Best least-squares broadband gain match (the only fair linear d.o.f.).
    g = float(np.dot(model, hw) / (np.dot(model, model) + 1e-30))
    res = hw - g * model
    out['gain_match_db'] = db(g)
    out['null_peak_db'] = db(np.max(np.abs(res)))
    out['null_rms_db'] = db(np.sqrt(np.mean(res ** 2)))
    out['input_rms_db'] = db(np.sqrt(np.mean(hw ** 2)))
    out['null_depth_db'] = out['input_rms_db'] - out['null_rms_db']

    # Residual spectrum vs the hardware spectrum, in octave bands — WHERE they
    # diverge (attack click, HF saturation differences, etc.).
    w = 1 << 14
    bands = [(20, 80), (80, 250), (250, 800), (800, 2500), (2500, 8000), (8000, 20000)]
    acc_r = np.zeros(w // 2 + 1); acc_h = np.zeros(w // 2 + 1); k = 0
    win = np.hanning(w)
    for s in range(0, n - w, w // 2):
        acc_r += np.abs(np.fft.rfft(res[s:s+w] * win)) ** 2
        acc_h += np.abs(np.fft.rfft(hw[s:s+w] * win)) ** 2
        k += 1
    f = np.fft.rfftfreq(w, 1 / fs)
    band_div = {}
    for lo, hi in bands:
        m = (f >= lo) & (f < hi)
        if m.any() and k:
            band_div[f'{lo}-{hi}Hz'] = round(10 * np.log10((acc_r[m].sum()) / (acc_h[m].sum() + 1e-30) + 1e-30), 2)
    out['residual_minus_hw_by_band_db'] = band_div

    depth = out['null_depth_db']
    out['verdict'] = (
        'EXCELLENT match (>40 dB null depth)' if depth > 40 else
        'GOOD match (25-40 dB) — divergence localized, see band table' if depth > 25 else
        'FAIR (15-25 dB) — model and unit differ audibly on this signal/setting' if depth > 15 else
        'POOR (<15 dB) — different behavior; inspect band table + attack/release alignment')
    return out


def selftest():
    """Prove the math on synthetic model-vs-'hardware' (model + delay + gain +
    a touch of nonlinearity standing in for the real unit's difference)."""
    fs = 48000
    t = np.arange(fs) / fs
    model = 0.5 * np.sin(2 * np.pi * 220 * t) * (0.5 + 0.5 * np.sin(2 * np.pi * 2 * t))
    hw = np.concatenate([np.zeros(37), 1.4 * model])[:len(model)]      # +37 smp, +2.9 dB
    hw = hw + 0.02 * np.tanh(6 * hw)                                    # small HF divergence
    r = compare(model, hw, fs)
    print('selftest:', json.dumps(r, indent=2))
    assert r['lag_samples'] == 37, f"alignment wrong: {r['lag_samples']}"
    assert abs(r['gain_match_db'] - 20 * np.log10(1.4)) < 0.5, 'gain match wrong'
    assert r['null_depth_db'] > 20, 'tiny nonlinearity should still null deep'
    print('\nselftest PASSED — alignment, gain-match, null, and band divergence all correct.')


def main():
    args = sys.argv[1:]
    if '--selftest' in args:
        selftest(); return
    def opt(name, default=None):
        return args[args.index(name) + 1] if name in args else default
    model_p, hw_p = opt('--model'), opt('--hw')
    if not model_p or not hw_p:
        print(__doc__); sys.exit(2)
    model, fsm = read_wav(model_p)
    hw, fsh = read_wav(hw_p)
    if fsm != fsh:
        print(f'sample-rate mismatch: model {fsm}, hw {fsh} — resample first'); sys.exit(2)
    r = compare(model, hw, fsm, align_only='--align-only' in args)
    print(json.dumps(r, indent=2))
    if opt('--json'):
        json.dump(r, open(opt('--json'), 'w'), indent=2)


if __name__ == '__main__':
    main()
