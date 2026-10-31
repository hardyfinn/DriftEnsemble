#!/usr/bin/env python3
# Draws res/Ensemble.svg (14 HP). VCV Rack can't draw SVG <text>, so every
# label is turned into outline paths with fontTools. Positions are in mm and
# must match src/Ensemble.cpp.
# Colours and zigzags come from Haer's reference photo (panel option C).
#   pip install fonttools
#   python3 res/make_panel.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W, H = 71.12, 128.5  # 14 HP

PINK = "#BE3270"
PINK_DK = "#7C1F4B"
YELLOW = "#E4E052"
GREEN = "#9BC96B"
GREEN_DK = "#7FAE55"
CREAM = "#F7F2E8"
PLUM = "#3A1028"
SOFT = "#4E6B34"

# Layout (mm), shared with src/Ensemble.cpp
TOP_Y = 27.0
ROW_Y = [47.0 + i * 13.5 for i in range(6)]
SPREAD_X, LEAN_X, LIGHT_X, OUT_X = 8.0, 19.5, 28.0, 38.0
KNOB_X, JACK_X = 53.0, 65.0
HEAD_Y = 40.0

fonts = {}


def text(s, x, y, size, font=FONT_MED, fill=PLUM, anchor="middle", spacing=0.04):
    """SVG path for string s; (x, y) is the baseline point in mm."""
    if font not in fonts:
        f = TTFont(font)
        fonts[font] = (f, f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    f, gs, cmap, upm = fonts[font]
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = sum(gs[n].width + track for n in names) - track
    x0 = x - width * scale / 2 if anchor == "middle" else (x - width * scale if anchor == "end" else x)
    pen = SVGPathPen(gs)
    cx = 0.0
    for n in names:
        gs[n].draw(TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, y)))
        cx += gs[n].width + track
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


def rect(x, y, w, h, fill, rx=0):
    return f'<rect x="{x:.3f}" y="{y:.3f}" width="{w:.3f}" height="{h:.3f}" rx="{rx}" fill="{fill}"/>'


def poly(pts, fill):
    return '<path fill="%s" d="M%s Z"/>' % (fill, " L".join(f"{x:.3f},{y:.3f}" for x, y in pts))


def zigzag_band(y, fill, edge, up=False, n=9):
    """Band from y to the panel edge with a zigzag edge in 'edge' colour.
    up=False: band at the top (0..y), zigzag pointing down; up=True: at the bottom."""
    out = []
    w = W / n
    s = -1 if up else 1
    out.append(rect(0, y, W, H - y, fill) if up else rect(0, 0, W, y, fill))
    for i in range(n):
        a = i * w
        out.append(poly([(a, y), (a + w / 2, y + s * 2.4), (a + w, y)], fill))
        out.append(poly([(a, y), (a + w / 2, y + s * 2.4), (a + w, y),
                         (a + w, y + s * 0.8), (a + w / 2, y + s * 3.2), (a, y + s * 0.8)], edge))
    return out


p = []
add = p.append
add(rect(0, 0, W, H, GREEN))

# Header and footer bands
p += zigzag_band(15.0, PINK, YELLOW)
p += zigzag_band(123.5, PINK, YELLOW, up=True)
add(text("DRIFT ENSEMBLE", W / 2, 8.8, 4.4, FONT_BOLD, CREAM, spacing=0.08))
add(text("six human clocks", W / 2, 12.8, 2.1, FONT_MED, YELLOW))
add(text("after Hennig et al. 2011", W / 2, 127.0, 1.7, FONT_MED, CREAM))

# Top row: clock, reset, freeze
add(text("CLOCK", 10.0, 21.8, 2.1, FONT_BOLD))
add(text("RESET", 22.0, 21.8, 2.1, FONT_BOLD))
add(text("FREEZE", (KNOB_X + JACK_X) / 2, 21.8, 2.1, FONT_BOLD))
add(text("16ths", 10.0, 33.6, 1.6, FONT_MED, SOFT))
add(text("button", KNOB_X, 33.6, 1.6, FONT_MED, SOFT))
add(text("gate", JACK_X, 33.6, 1.6, FONT_MED, SOFT))

# Players: column headers, output box, numbers, separators
add(rect(3.0, HEAD_Y - 3.6, W - 6.0, 0.3, PINK))
add(text("SPREAD", SPREAD_X, HEAD_Y, 2.0, FONT_BOLD))
add(text("LEAN", LEAN_X, HEAD_Y, 2.0, FONT_BOLD))
add(rect(OUT_X - 5.2, HEAD_Y - 3.2, 10.4, ROW_Y[-1] + 6.0 - (HEAD_Y - 3.2), PINK, 1.2))
add(text("OUT", OUT_X, HEAD_Y, 2.0, FONT_BOLD, CREAM))
for i, y in enumerate(ROW_Y):
    add(text(str(i + 1), LIGHT_X, y + 5.6, 2.5, FONT_BOLD, PINK_DK))
    if i < 5:
        add(rect(3.0, y + 6.55, OUT_X - 5.2 - 3.0, 0.25, GREEN_DK))
# Lean hint on the first row
add(text("early", LEAN_X - 4.6, ROW_Y[0] + 5.6, 1.4, FONT_MED, SOFT))
add(text("late", LEAN_X + 4.6, ROW_Y[0] + 5.6, 1.4, FONT_MED, SOFT))

# Divider between the players and the master column
add(rect(45.6, HEAD_Y - 3.2, 0.3, ROW_Y[-1] + 6.0 - (HEAD_Y - 3.2), PINK))

# Master column: one label over each row (knob + CV, or two small controls)
add(text("TOGETHER", (KNOB_X + JACK_X) / 2, HEAD_Y, 2.0, FONT_BOLD))
add(text("LEADER", KNOB_X, ROW_Y[1] - 6.0, 1.9, FONT_BOLD))
add(text("SEED", JACK_X, ROW_Y[1] - 6.0, 1.9, FONT_BOLD))
add(text("SPREAD ±", (KNOB_X + JACK_X) / 2, ROW_Y[2] - 6.0, 2.0, FONT_BOLD))
add(text("FEEL", (KNOB_X + JACK_X) / 2, ROW_Y[3] - 6.0, 2.0, FONT_BOLD))
add(text("SWING", (KNOB_X + JACK_X) / 2, ROW_Y[4] - 6.0, 2.0, FONT_BOLD))
# Bottom-right: a small block of the photo's triangle tiles as decoration
tx0, ty0, tw, th = 48.0, ROW_Y[5] - 4.4, 20.0, 8.8
n = 5
w = tw / n
for row, (bg, fg) in enumerate([(PINK, YELLOW), (YELLOW, PINK)]):
    y0 = ty0 + row * th / 2
    add(rect(tx0, y0, tw, th / 2, bg))
    for i in range(n):
        a = tx0 + i * w
        add(poly([(a, y0 + th / 2), (a + w / 2, y0), (a + w, y0 + th / 2)], fg))

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" '
       f'viewBox="0 0 {W} {H}">\n' + "\n".join(x for x in p if x) + "\n</svg>\n")
with open(os.path.join(HERE, "Ensemble.svg"), "w") as fh:
    fh.write(svg)
print("wrote Ensemble.svg")
