#!/usr/bin/env python3
"""Canonical audio measurement for the Touchstone audition set.

One methodology for every file so they are directly comparable:
  - LUFS-I (integrated), LRA (loudness range), True Peak (dBTP) via ffmpeg
    ebur128 (ITU-R BS.1770-4 / EBU R128 — the standard).
  - Sample peak, RMS, crest factor, DC offset, stereo correlation + L/R
    balance, noise floor, spectral band energies, spectral centroid,
    F0 estimate via numpy/scipy.

Usage: measure.py <file.wav>   ->  one JSON object on stdout.
"""
import sys, json, subprocess, re
import numpy as np
import soundfile as sf

BANDS = [("sub",20,60),("low",60,120),("lowmid",120,400),("mid",400,2000),
         ("highmid",2000,6000),("high",6000,12000),("air",12000,20000)]

def ebur128(path):
    out = subprocess.run(["ffmpeg","-hide_banner","-nostats","-i",path,
                          "-filter_complex","ebur128=peak=true","-f","null","-"],
                         capture_output=True, text=True).stderr
    # Isolate the final "Summary:" block — per-frame lines also contain I:/LRA:
    # and would otherwise be grabbed (showing the -70 LUFS gate floor).
    idx = out.rfind("Summary:")
    blk = out[idx:] if idx >= 0 else out
    def grab(label):
        m = re.search(label + r"\s*([-\d.]+)", blk)
        return float(m.group(1)) if m else None
    I   = grab(r"\bI:")
    LRA = grab(r"\bLRA:")
    peaks = re.findall(r"Peak:\s*([-\d.]+)\s*dBFS", blk)
    tp = float(peaks[-1]) if peaks else None
    return I, LRA, tp

def band_db(x, fs):
    n = len(x); X = np.abs(np.fft.rfft(x*np.hanning(n)))**2; f = np.fft.rfftfreq(n,1/fs)
    total = X.sum()+1e-30
    res = {}
    for name,lo,hi in BANDS:
        m=(f>=lo)&(f<hi)
        res[name]=round(10*np.log10(X[m].sum()/total+1e-30),2)  # dB rel. broadband
    centroid = float((f*X).sum()/total)
    return res, round(centroid,1)

def est_f0(x, fs):
    # autocorrelation F0 over voiced frames (80-500 Hz), median
    w=int(0.04*fs); h=w//2; f0s=[]
    for s in range(0,len(x)-w,h):
        seg=x[s:s+w]
        if np.sqrt(np.mean(seg**2))<10**(-45/20): continue
        seg=seg-seg.mean(); ac=np.correlate(seg,seg,'full')[w-1:]
        lo=int(fs/500); hi=int(fs/80)
        if hi>=len(ac): continue
        peak=lo+np.argmax(ac[lo:hi])
        if ac[peak]>0.3*ac[0]: f0s.append(fs/peak)
    return round(float(np.median(f0s)),1) if f0s else None

def noise_floor_db(x, fs):
    w=int(0.05*fs); h=w
    r=[20*np.log10(np.sqrt(np.mean(x[s:s+w]**2))+1e-12) for s in range(0,len(x)-w,h)]
    r=[v for v in r if v>-120]
    return round(float(np.percentile(r,5)),1) if r else None

def main():
    path=sys.argv[1]
    x,fs=sf.read(path, always_2d=True)  # (n, ch) float64
    nch=x.shape[1]; mono=x.mean(1)
    peak=20*np.log10(np.max(np.abs(x))+1e-12)
    rms=20*np.log10(np.sqrt(np.mean(mono**2))+1e-12)
    I,LRA,tp=ebur128(path)
    bands,centroid=band_db(mono,fs)
    res={
      "file":path.split("/")[-1],
      "sr":fs,"ch":nch,"dur_s":round(len(mono)/fs,2),
      "lufs_i":I,"lra_lu":LRA,"true_peak_dbtp":tp,
      "sample_peak_dbfs":round(float(peak),2),
      "rms_dbfs":round(float(rms),2),
      "crest_db":round(float(peak-rms),2),
      "dc_offset":round(float(np.mean(mono)),6),
      "noise_floor_dbfs":noise_floor_db(mono,fs),
      "f0_median_hz":est_f0(mono,fs),
      "spectral_centroid_hz":centroid,
      "band_db_rel": bands,
    }
    if nch==2:
        L,R=x[:,0],x[:,1]
        c=np.corrcoef(L,R)[0,1] if np.std(L)>0 and np.std(R)>0 else 1.0
        res["stereo_corr"]=round(float(c),3)
        res["lr_balance_db"]=round(float(20*np.log10((np.sqrt(np.mean(R**2))+1e-12)/(np.sqrt(np.mean(L**2))+1e-12))),2)
    print(json.dumps(res))

if __name__=="__main__":
    main()
