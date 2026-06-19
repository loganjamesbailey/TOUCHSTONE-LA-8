#!/usr/bin/env python3
"""Synthetic vocal-like stems for proving the render/audition pipeline.

These are NOT a substitute for a real vocal — they are a deterministic
stand-in so the renderer and audition matrix can be built and verified
before James drops in a real take. A real stem produces far more honest
auditions (formants, consonants, breath, real dynamics). See
auditions/README after rendering for where to put the real file.

Generates two mono 48 kHz files:
  verse  — quiet, intimate, slow vibrato, dark tilt, ~-24 dBFS
  chorus — pushed, brighter (more 2-4 kHz "effort"), louder, ~-9 dBFS
with a glottal-pulse-like excitation (band-limited impulse train through
a few formant resonances) so source-filter tooling has something real to
bite on.
"""
import numpy as np, struct, sys, os

FS = 48000

def formant_filter(x, freqs, bws, fs):
    y = np.zeros_like(x)
    for f, bw in zip(freqs, bws):
        r = np.exp(-np.pi * bw / fs)
        th = 2 * np.pi * f / fs
        a1, a2 = -2 * r * np.cos(th), r * r
        b0 = (1 - r) * np.sqrt(1 - 2 * r * np.cos(2 * th) + r * r)
        o = np.zeros_like(x); z1 = z2 = 0.0
        for n in range(len(x)):
            v = b0 * x[n] - a1 * z1 - a2 * z2
            o[n] = v; z2 = z1; z1 = v
        y += o
    return y

def glottal_train(dur, f0_base, vib_hz, vib_cents, fs, closure_env):
    # closure_env: per-sample array in [0,1]; higher = sharper closure = more
    # HF "press"/effort. Voice mode acts on the *time variation* of this
    # relative to its learned baseline, so it must move within the take.
    n = int(dur * fs)
    t = np.arange(n) / fs
    f0 = f0_base * 2 ** ((vib_cents / 1200) * np.sin(2 * np.pi * vib_hz * t))
    phase = np.cumsum(f0 / fs)
    frac = np.mod(phase, 1.0)
    op = 0.6  # open quotient
    g = np.where(frac < op,
                 0.5 * (1 - np.cos(np.pi * frac / op)),
                 np.cos(np.pi * (frac - op) / (1 - op) * (0.5 + 0.5 * (1 - closure_env))))
    return np.diff(np.concatenate([[0.0], g]))  # flow derivative: closure spike

def build(kind):
    # Multi-phrase take with intra-take EFFORT and PROXIMITY excursions, so
    # Voice's adaptive baseline learns the mean and Intimacy/Mic act on the
    # swings — the only way a synthetic stem can demonstrate the effect.
    if kind == "verse":
        dur, f0, vib, level = 6.0, 165.0, 5.0, -24.0
        formants = [(500, 80), (1500, 90), (2500, 120)]
        closure_mean, closure_swing = 0.35, 0.30   # mostly intimate, mild pushes
        prox_swing = 5.0                            # dB of LF lean-in
    else:
        dur, f0, vib, level = 6.0, 220.0, 6.0, -9.0
        formants = [(650, 90), (1900, 110), (3200, 150), (4200, 180)]
        closure_mean, closure_swing = 0.6, 0.35     # pushed, big effort excursions
        prox_swing = 8.0

    n = int(dur * FS); t = np.arange(n) / FS
    # Effort excursion: slow LFO (different rate than swells) -> shout-band swing.
    closure_env = np.clip(closure_mean + closure_swing * np.sin(2 * np.pi * 0.28 * t + 0.7), 0.02, 0.98)
    src = glottal_train(dur, f0, vib, 30, FS, closure_env)
    y = formant_filter(src, [f for f, _ in formants], [b for _, b in formants], FS)

    # Proximity excursion: the singer moving in/out -> time-varying LF shelf
    # (130 Hz one-pole), which is exactly what Mic Profile stabilizes.
    prox_db = prox_swing * np.sin(2 * np.pi * 0.17 * t)         # +/- prox_swing dB
    r = np.exp(-2 * np.pi * 130.0 / FS)
    lf = np.zeros(n); z = 0.0
    for i in range(n):
        z = (1 - r) * y[i] + r * z
        lf[i] = z
    y = y + lf * (10 ** (prox_db / 20) - 1.0)

    # Phrase envelope + slow level swells so the rider has something to do.
    env = np.minimum(1.0, t / 0.15) * np.minimum(1.0, (dur - t) / 0.3)
    env *= 0.65 + 0.35 * np.sin(2 * np.pi * 0.45 * t + 1.3)
    y *= np.clip(env, 0.0, None)
    y /= (np.max(np.abs(y)) + 1e-12)
    y *= 10 ** (level / 20)
    return y.astype(np.float64)

def write_wav(path, x, fs=FS):
    data = x.astype('<f4').tobytes()
    with open(path, 'wb') as f:
        f.write(b'RIFF'); f.write(struct.pack('<I', 36 + len(data))); f.write(b'WAVE')
        f.write(b'fmt '); f.write(struct.pack('<IHHIIHH', 16, 3, 1, fs, fs * 4, 4, 32))
        f.write(b'data'); f.write(struct.pack('<I', len(data))); f.write(data)

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(out, exist_ok=True)
    for kind in ('verse', 'chorus'):
        p = os.path.join(out, f'SOURCE_synth-{kind}.wav')
        write_wav(p, build(kind))
        print('wrote', p)
