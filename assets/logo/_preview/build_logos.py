#!/usr/bin/env python3
"""Build the VKM Engine logo set from the traced, straight-line polygons.

Source of truth: the hexagonal VKM monogram + "VKM ENGINE" wordmark, traced
from download.png (the original design sheet) and simplified to straight edges.
Pure black/white treatment: #141414 on light backgrounds, #FFFFFF reversed.

Re-run to regenerate every SVG/PNG deliverable.
"""
import json, os, cairosvg

HERE = os.path.dirname(os.path.abspath(__file__))
LOGO = os.path.dirname(HERE)

INK   = "#141414"   # near-black, on light backgrounds
PAPER = "#FFFFFF"   # white, reversed on dark backgrounds

mono = json.load(open(os.path.join(HERE, "polys.json")))         # {U,K,M} -> [[x,y],...]  straight-line monogram
word = json.load(open(os.path.join(HERE, "word_smooth.json")))   # ["M..C..Z", ...]  smooth Bezier wordmark glyphs

# Shift everything into a tidy local coordinate space (min corner near 20,20).
OFFX, OFFY = 435.0, 42.0
import re
def shift_poly(p):  return [[round(x - OFFX, 1), round(y - OFFY, 1)] for x, y in p]
def shift_d(d):     # translate every coordinate pair in a Bezier 'd' string
    return re.sub(r'(-?\d+\.?\d*),(-?\d+\.?\d*)',
                  lambda mo: f'{round(float(mo.group(1))-OFFX,2)},{round(float(mo.group(2))-OFFY,2)}', d)
mono_s = {k: shift_poly(v) for k, v in mono.items()}
word_s = [shift_d(d) for d in word]

def bbox_poly(polys):
    xs = [x for p in polys for x, y in p]; ys = [y for p in polys for x, y in p]
    return min(xs), min(ys), max(xs), max(ys)
def bbox_d(ds):     # rough bbox from all coordinate pairs in the 'd' strings (fine for layout)
    pts = [(float(a), float(b)) for d in ds for a, b in re.findall(r'(-?\d+\.?\d*),(-?\d+\.?\d*)', d)]
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)

def path(p):
    return '<path d="M ' + ' L '.join(f'{x},{y}' for x, y in p) + ' Z"/>'

mono_paths = ''.join(path(mono_s[k]) for k in ('U', 'K', 'M'))
word_paths = '<path fill-rule="evenodd" d="' + ''.join(word_s) + '"/>'

def svg(viewbox, body, fill):
    vb = ' '.join(f'{v:g}' for v in viewbox)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb}" '
            f'shape-rendering="geometricPrecision">\n'
            f'  <g fill="{fill}" fill-rule="evenodd">{body}</g>\n</svg>\n')

# ---- monogram-only square viewBox (centered, padded) ----
mx0, my0, mx1, my1 = bbox_poly(list(mono_s.values()))
side = max(mx1 - mx0, my1 - my0) * 1.16
cx, cy = (mx0 + mx1) / 2, (my0 + my1) / 2
mono_vb = (round(cx - side/2, 1), round(cy - side/2, 1), round(side, 1), round(side, 1))

# ---- full lockup viewBox (monogram + wordmark, padded) ----
wx0, wy0, wx1, wy1 = bbox_d(word_s)
fx0, fy0, fx1, fy1 = min(mx0, wx0), min(my0, wy0), max(mx1, wx1), max(my1, wy1)
pad = 18
full_vb = (round(fx0 - pad, 1), round(fy0 - pad, 1),
           round(fx1 - fx0 + 2*pad, 1), round(fy1 - fy0 + 2*pad, 1))

files = {
    "vkm_engine_logo.svg":       svg(full_vb, mono_paths + word_paths, INK),    # normal, light bg
    "vkm_engine_logo_mono.svg":  svg(full_vb, mono_paths + word_paths, PAPER),  # reversed, dark bg
    "vkm_engine_logo_small.svg": svg(mono_vb, mono_paths,              INK),    # monogram, light bg
}
hdr = {
    "vkm_engine_logo.svg":       "<!-- VKM Engine logo - full lockup (monogram + VKM ENGINE), straight-line vector. Ink #141414 for light backgrounds. -->\n",
    "vkm_engine_logo_mono.svg":  "<!-- VKM Engine logo - full lockup reversed (white), straight-line vector. For dark backgrounds. -->\n",
    "vkm_engine_logo_small.svg": "<!-- VKM Engine logo - small (VKM monogram only), straight-line vector. Ink #141414 for light backgrounds. -->\n",
}
for name, content in files.items():
    content = content.replace("\n", "\n", 1)
    content = '<?xml version="1.0" encoding="UTF-8"?>\n' + hdr[name] + content
    open(os.path.join(LOGO, name), "w").write(content)
    print("wrote", name)

# ---- white monogram SVG (transient, for the menu-bar mark PNG) ----
mark_svg = svg(mono_vb, mono_paths, PAPER)
open(os.path.join(HERE, "_mark_white.svg"), "w").write(mark_svg)

# ---- circular badge SVG (transient, for the window icon PNG): white monogram on ink disc ----
bcx, bcy = (mx0 + mx1) / 2, (my0 + my1) / 2
r = max(mx1 - mx0, my1 - my0) * 0.80
badge = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{bcx-r:g} {bcy-r:g} {2*r:g} {2*r:g}" '
         f'shape-rendering="geometricPrecision">\n'
         f'  <circle cx="{bcx:g}" cy="{bcy:g}" r="{r:g}" fill="{INK}"/>\n'
         f'  <g fill="{PAPER}" fill-rule="evenodd">{mono_paths}</g>\n</svg>\n')
open(os.path.join(HERE, "_badge.svg"), "w").write(badge)

# ---- rasterize PNG deliverables ----
def png(src, out, **kw):
    cairosvg.svg2png(url=os.path.join(LOGO if not src.startswith("_") else HERE, src),
                     write_to=os.path.join(LOGO, out), **kw)
    print("wrote", out)

png("vkm_engine_logo.svg",       "vkm_engine_logo.png",       output_width=1200)            # full, transparent
png("vkm_engine_logo_mono.svg",  "vkm_engine_logo_mono.png",  output_width=1200)            # full, reversed for dark
png("vkm_engine_logo_small.svg", "vkm_engine_logo_small.png", output_width=512, output_height=512)
cairosvg.svg2png(url=os.path.join(HERE, "_mark_white.svg"),
                 write_to=os.path.join(LOGO, "vkm_engine_mark.png"),
                 output_width=128, output_height=128)
print("wrote vkm_engine_mark.png")
cairosvg.svg2png(url=os.path.join(HERE, "_badge.svg"),
                 write_to=os.path.join(LOGO, "vkm_engine_icon.png"),
                 output_width=256, output_height=256)
print("wrote vkm_engine_icon.png")
print("done")
