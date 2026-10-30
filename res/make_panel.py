#!/usr/bin/env python3
# Draws res/Ensemble.svg. VCV Rack can't draw SVG <text>, so every label is
# turned into outline paths with fontTools. Positions are in mm and must match
# src/Ensemble.cpp.
#   pip install fonttools
#   python3 res/make_panel.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W, H = 81.28, 128.5  # 16 HP

BG = "#ECE6DA"
INK = "#1E1E24"
SOFT = "#7A7468"
ACCENT = "#C8502D"
OUTBOX = "#2A2A31"
OUTINK = "#ECE6DA"

fonts = {}


def text(s, x, y, size, font=FONT_MED, fill=INK, anchor="middle", spacing=0.04):
    """Return an SVG path for string s; (x, y) is the baseline point in mm."""
    if font not in fonts:
        f = TTFont(font)
        fonts[font] = (f, f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    f, gs, cmap, upm = fonts[font]
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = sum(gs[n].width + track for n in names) - track
    if anchor == "middle":
        x0 = x - width * scale / 2
    elif anchor == "end":
        x0 = x - width * scale
    else:
        x0 = x
    pen = SVGPathPen(gs)
    cx = 0.0
    for n in names:
        # flip y: font units go up, SVG goes down
        tp = TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, y))
        gs[n].draw(tp)
        cx += gs[n].width + track
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


parts = []
add = parts.append
add(f'<rect width="{W}" height="{H}" fill="{BG}"/>')

# Title
add(text("DRIFT ENSEMBLE", W / 2, 10.2, 5.0, FONT_BOLD, INK, spacing=0.08))
add(text("six human clocks", W / 2, 14.6, 2.4, FONT_MED, SOFT))
add(f'<rect x="6" y="16.6" width="{W - 12}" height="0.35" fill="{ACCENT}"/>')

# Top row: clock, reset, togetherness
add(text("CLOCK", 10.0, 21.2, 2.2, FONT_BOLD))
add(text("RESET", 23.0, 21.2, 2.2, FONT_BOLD))
add(text("TOGETHERNESS", 50.0, 20.4, 2.2, FONT_BOLD))
add(text("CV", 66.0, 21.2, 2.2, FONT_BOLD))
add(text("apart", 43.2, 34.6, 1.8, FONT_MED, SOFT))
add(text("together", 57.0, 34.6, 1.8, FONT_MED, SOFT))

# Column headers for the players
colX = [9.0, 19.5, 31.0, 41.5, 51.0, 61.0, 72.5]
hy = 40.0
add(f'<rect x="3.5" y="{hy - 3.6}" width="{W - 7}" height="0.25" fill="{SOFT}"/>')
for x, label in zip(colX, ["SPREAD", "CV", "FEEL", "CV", "", "OUT", "ERR"]):
    if label:
        add(text(label, x, hy, 2.2, FONT_BOLD, OUTINK if x > 55 else INK))

# Output area: dark box behind outputs (VCV convention)
add(f'<rect x="55.2" y="{hy - 3.2}" width="23.0" height="{H - hy - 5.5}" rx="1.2" fill="{OUTBOX}"/>')
# redraw output headers on top of the box
add(text("OUT", colX[5], hy, 2.2, FONT_BOLD, OUTINK))
add(text("ERR", colX[6], hy, 2.2, FONT_BOLD, OUTINK))

for i in range(6):
    y = 47.0 + i * 13.5
    # player number under the light
    add(text(str(i + 1), colX[4], y + 5.6, 2.6, FONT_BOLD, ACCENT))
    if i < 5:
        add(f'<rect x="3.5" y="{y + 6.55}" width="51" height="0.2" fill="#CFC7B8"/>')

add(text("after Hennig et al. 2011", W / 2, 124.6, 1.8, FONT_MED, SOFT))

svg = (
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" '
    f'viewBox="0 0 {W} {H}">\n' + "\n".join(parts) + "\n</svg>\n"
)
with open(os.path.join(HERE, "Ensemble.svg"), "w") as fh:
    fh.write(svg)
print("wrote Ensemble.svg")
