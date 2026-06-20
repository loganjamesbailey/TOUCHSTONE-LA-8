#!/usr/bin/env python3
"""Loudness-normalize WAVs to a common LUFS with a true-peak ceiling.

Linear gain only (no limiting/coloration) so an A/B compares the processors,
not a normalizer's limiter. Per file: gain = target_LUFS - measured_LUFS,
reduced if needed so true-peak stays <= ceiling. Makes audition files fairly
A/B-able and guarantees no inter-sample overs.

Usage: normalize.py <target_lufs> <ceiling_dbtp> <file.wav> [file.wav ...]
"""
import sys, subprocess, re
import numpy as np
import soundfile as sf

def ebur128(path):
    out = subprocess.run(["ffmpeg","-hide_banner","-nostats","-i",path,
                          "-filter_complex","ebur128=peak=true","-f","null","-"],
                         capture_output=True, text=True).stderr
    blk = out[out.rfind("Summary:"):]
    I  = float(re.search(r"\bI:\s*([-\d.]+)", blk).group(1))
    tp = float(re.findall(r"Peak:\s*([-\d.]+)\s*dBFS", blk)[-1])
    return I, tp

def main():
    target = float(sys.argv[1]); ceiling = float(sys.argv[2])
    for path in sys.argv[3:]:
        I, tp = ebur128(path)
        gain = target - I
        if tp + gain > ceiling:        # would breach the ceiling -> hold to it
            gain = ceiling - tp
        x, fs = sf.read(path, always_2d=True)
        x = x * (10 ** (gain / 20.0))
        sf.write(path, x.astype(np.float32), fs, subtype="FLOAT")
        newpk = 20*np.log10(np.max(np.abs(x))+1e-12)
        print(f"  {path.split('/')[-1]:34s} {I:6.1f} LUFS  gain {gain:+5.1f} dB -> peak {newpk:5.1f} dBFS")

if __name__ == "__main__":
    main()
