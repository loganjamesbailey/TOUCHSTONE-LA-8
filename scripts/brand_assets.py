#!/usr/bin/env python3
# Touchstone brand asset generator — the single source of truth.
# Run with a python that has fontTools (see scripts/build note re: venv):
#   python3 scripts/brand_assets.py
# Regenerates: site/assets/touchstone-{mark,logo-light,logo-dark,
#              wordmark-light,wordmark-dark}.svg
#
# Geometry: golden-ratio cascade from the 100-unit stone (a_n = 100/phi^n):
#   a1 bar width / a2 wave height zone / a5 bar+wave stroke / a6 gap.
# Corners: stone r12 (macOS-window style, straight sides) per art
# direction 2026-06-12. Wave: plain phi-cascade bell, width = bar width.

import math, os, sys, urllib.request

PHI = (1 + 5**0.5) / 2
S = 100.0
a1, a2, a3, a4, a5, a6 = [S / PHI**n for n in range(1, 7)]

BLACK, PAPER = "#0B0B0D", "#F5F4EF"
GREEN, BLUE, PINK = "#3DFB7E", "#35A6FF", "#FF2E9D"
R_STONE = 12

OUT = os.path.join(os.path.dirname(__file__), '..', 'site', 'assets')

# ---------------- the mark ----------------
m = (S - a1) / 2
barY = m
strokeW = a5

# Wave: phi-cascade bell — width = bar width (a1), apex height a2, no
# ornament. Matched stroke weights give identical margins on all sides.
def wave_path():
    apexY = barY + a5 + a6 + strokeW / 2
    baseY = apexY + a2
    xl, xr = m, S - m
    return (f"M{xl:.3f},{baseY:.3f} "
            f"C{xl+12:.3f},{baseY:.3f} {50-12:.3f},{apexY:.3f} 50,{apexY:.3f} "
            f"C{50+12:.3f},{apexY:.3f} {xr-12:.3f},{baseY:.3f} {xr:.3f},{baseY:.3f}")

def mark(keyline=False):
    kl = f' stroke="{BLUE}" stroke-width="1.2"' if keyline else ''
    return (f'<rect x="0" y="0" width="100" height="100" rx="{R_STONE}" fill="{BLACK}"{kl}/>'
            f'<rect x="{m:.3f}" y="{barY:.3f}" width="{a1:.3f}" height="{a5:.3f}" '
            f'rx="{a5/2:.3f}" fill="{BLUE}"/>'
            f'<path d="{wave_path()}" fill="none" stroke="{GREEN}" '
            f'stroke-width="{strokeW:.3f}" stroke-linecap="round"/>')

# ---------------- the wordmark (Jura Light outlines) ----------------
JURA = '/tmp/jura.ttf'
JURA_URL = 'https://github.com/google/fonts/raw/main/ofl/jura/Jura%5Bwght%5D.ttf'

def load_glyphs():
    if not os.path.exists(JURA):
        urllib.request.urlretrieve(JURA_URL, JURA)
    from fontTools import ttLib
    from fontTools.varLib.instancer import instantiateVariableFont
    from fontTools.pens.svgPathPen import SVGPathPen
    font = ttLib.TTFont(JURA)
    instantiateVariableFont(font, {"wght": 300})
    gs, cmap = font.getGlyphSet(), font.getBestCmap()
    capH = font['OS/2'].sCapHeight
    word, tracking = "TOUCHSTONE", int(0.14 * font['head'].unitsPerEm)
    glyphs, total = [], 0
    for ch in word:
        g = gs[cmap[ord(ch)]]
        pen = SVGPathPen(gs)
        g.draw(pen)
        glyphs.append({'ch': ch, 'path': pen.getCommands(), 'adv': g.width})
        total += g.width
    total += tracking * (len(word) - 1)
    return glyphs, total, tracking, capH

def build_word(glyphs, total, tracking, capH, scale, baselineY, ink, o1c, o2c):
    out, x, oi = [], 0.0, 0
    capS = capH * scale
    for gl in glyphs:
        tx = x * scale
        if gl['ch'] == 'O':
            color = o1c if oi == 0 else o2c
            oi += 1
            cx = tx + (gl['adv'] * scale) / 2
            out.append(f'<circle cx="{cx:.3f}" cy="{baselineY - capS/2:.3f}" '
                       f'r="{capS * 1.06 / 2:.3f}" fill="{color}"/>')
        else:
            out.append(f'<g transform="translate({tx:.3f},{baselineY:.3f}) '
                       f'scale({scale:.6f},{-scale:.6f})">'
                       f'<path d="{gl["path"]}" fill="{ink}"/></g>')
        x += gl['adv'] + tracking
    return ''.join(out)

def write(name, body, w, h):
    path = os.path.join(OUT, name)
    with open(path, 'w') as f:
        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 -1 {w} {h:.2f}">{body}</svg>')
    print("wrote", name)

def main():
    glyphs, total, tracking, capH = load_glyphs()

    # standalone wordmark
    W = 400.0
    sc = W / total
    capS = capH * sc
    pad = capS * 0.25
    baseY = pad + capS * 1.06/2 + capS/2
    H = baseY + pad
    write('touchstone-wordmark-light.svg',
          build_word(glyphs, total, tracking, capH, sc, baseY, BLACK, GREEN, PINK), 400, H)
    write('touchstone-wordmark-dark.svg',
          build_word(glyphs, total, tracking, capH, sc, baseY, PAPER, GREEN, PINK), 400, H)

    # mark + lockups
    write('touchstone-mark.svg', mark(), 100, 101)
    s2 = 100.0 / total
    base2 = 100 + a4 + capH * s2
    H2 = base2 + capH * s2 * 0.12
    write('touchstone-logo-light.svg',
          mark() + build_word(glyphs, total, tracking, capH, s2, base2, BLACK, GREEN, PINK), 100, H2 + 1)
    write('touchstone-logo-dark.svg',
          mark(True) + build_word(glyphs, total, tracking, capH, s2, base2, PAPER, GREEN, PINK), 100, H2 + 1)

if __name__ == '__main__':
    main()
