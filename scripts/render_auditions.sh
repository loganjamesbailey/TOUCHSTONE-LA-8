#!/bin/bash
# Render the labeled audition set for the listening gate. Files are named
# so they sort and A/B cleanly in LiveProfessor and Logic.
#
#   auditions/source/    the dry reference(s) you compare against
#   auditions/voice/      Voice, Intimacy {0,25,50,75,100} x 3 Mic Profiles
#   auditions/classics/   the five classics on the same source
#   auditions/hq_showcase/ FET & Opto base vs HQ (hear the 4x improvement)
#
# Source files (drop your REAL vocal here to replace the synthetic stand-in):
#   auditions/source/verse.wav    quiet/intimate take, mono or stereo, any rate
#   auditions/source/chorus.wav   pushed/loud take
# If those are absent, the deterministic synthetic stems are used and every
# file is tagged "synth" so you never mistake a placeholder for the real thing.

set -euo pipefail
cd "$(dirname "$0")/.."

RENDER=build/tools/render/touchstone_render
[ -x "$RENDER" ] || { echo "build the render tool first: cmake --build build --target touchstone_render"; exit 1; }

mkdir -p auditions/source auditions/voice auditions/classics auditions/hq_showcase

# Resolve sources: prefer a real take, fall back to the synthetic stand-in.
pick_source() {  # $1 = verse|chorus
    if [ -f "auditions/source/$1.wav" ]; then echo "auditions/source/$1.wav|";
    else
        [ -f "auditions/source/SOURCE_synth-$1.wav" ] || python3 tools/render/make_synth_vocal.py auditions/source >/dev/null
        echo "auditions/source/SOURCE_synth-$1.wav|synth-"
    fi
}

micname() { case "$1" in 0) echo dynClose ;; 1) echo condClose ;; 2) echo condFar ;; esac; }

for take in verse chorus; do
    IFS='|' read -r SRC TAG < <(pick_source "$take")
    echo "== $take  (source: $SRC, tag: '${TAG:-real}') =="

    # Voice: Intimacy sweep x Mic Profile.
    for mic in 0 1 2; do
        for int in 0 25 50 75 100; do
            ip=$(printf "%03d" "$int")
            "$RENDER" --in "$SRC" --mode voice --intimacy "$int" --mic "$mic" \
                --out "auditions/voice/${TAG}${take}_voice_$(micname "$mic")_int${ip}.wav" 2>/dev/null
        done
    done

    # Five classics, auto-makeup on for loudness-fair A/B, ~6-10 dB GR target.
    "$RENDER" --in "$SRC" --mode clean  --threshold -24 --ratio 3  --attack 12 --release 120 --knee 6 --automakeup --out "auditions/classics/${TAG}${take}_clean.wav"  2>/dev/null
    "$RENDER" --in "$SRC" --mode fet    --threshold -28 --ratio 4  --attack 5  --release 120 --automakeup            --out "auditions/classics/${TAG}${take}_fet.wav"    2>/dev/null
    "$RENDER" --in "$SRC" --mode opto   --threshold -26                                       --automakeup            --out "auditions/classics/${TAG}${take}_opto.wav"   2>/dev/null
    "$RENDER" --in "$SRC" --mode varimu --threshold -24 --ratio 3  --attack 20 --release 400 --automakeup            --out "auditions/classics/${TAG}${take}_varimu.wav" 2>/dev/null
    "$RENDER" --in "$SRC" --mode vca    --threshold -24 --ratio 4  --attack 10 --release 150 --knee 6 --automakeup    --out "auditions/classics/${TAG}${take}_vca.wav"    2>/dev/null
done

# HQ showcase: FET & Opto on the pushed take, base vs 4x HQ.
IFS='|' read -r CSRC CTAG < <(pick_source chorus)
for mode in fet opto; do
    "$RENDER" --in "$CSRC" --mode "$mode" --threshold -28 --ratio 8 --drive 6 --automakeup       --out "auditions/hq_showcase/${CTAG}chorus_${mode}_base.wav" 2>/dev/null
    "$RENDER" --in "$CSRC" --mode "$mode" --threshold -28 --ratio 8 --drive 6 --automakeup --hq  --out "auditions/hq_showcase/${CTAG}chorus_${mode}_hq.wav"   2>/dev/null
done

N=$(find auditions -name '*.wav' | wc -l | tr -d ' ')
echo
echo "Rendered audition set: $N WAVs under auditions/"
echo "  voice/      $(find auditions/voice -name '*.wav' | wc -l | tr -d ' ') files (Intimacy sweep x Mic Profile, verse + chorus)"
echo "  classics/   $(find auditions/classics -name '*.wav' | wc -l | tr -d ' ') files"
echo "  hq_showcase/$(find auditions/hq_showcase -name '*.wav' | wc -l | tr -d ' ') files (FET/Opto base vs HQ)"
[ -f auditions/source/verse.wav ] || echo "  NOTE: using SYNTHETIC source (files tagged 'synth-'). Drop real takes at auditions/source/{verse,chorus}.wav and re-run."
