#!/usr/bin/env python3
# Builds the HTML user guide (DriftEnsemble-Guide.html) with a labelled panel
# drawing made from res/Ensemble.svg.
#   python3 res/make_guide.py [output.html]
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "DriftEnsemble-Guide.html")

W, H = 91.44, 128.5
R = [47 + i * 13.5 for i in range(6)]
C1, C2, C3 = 63.0, 74.5, 85.5

# (number, x, y, name) for the callouts on the panel drawing
CALLOUTS = [
    (1, 10, 27, "Clock in"), (2, 22, 27, "Reset in"), (3, 59, 26.75, "Display"),
    (4, 8, R[0], "Spread"), (5, 19.5, R[0], "Lean"), (6, 31, R[0], "Swing (player)"),
    (7, 39.5, R[0], "Hit light"), (8, 49.5, R[0], "Out"),
    (9, C1, R[0], "Together"), (10, C3, R[0], "Leader"),
    (11, C1, R[1], "Spread ±"), (12, C3, R[1], "Seed"),
    (13, C1, R[2], "Feel"), (14, C3, R[2], "Anchor"),
    (15, C1, R[3], "Swing (master)"), (16, C3, R[3], "Push"),
    (17, C1, R[4], "Energy"), (18, C1, R[5], "Freeze"),
]


def panel_svg():
    s = open(os.path.join(HERE, "Ensemble.svg")).read()
    c = []

    def jack(x, y):
        c.append(f'<circle cx="{x}" cy="{y}" r="4.1" fill="#cfcfcf" stroke="#666" stroke-width=".3"/><circle cx="{x}" cy="{y}" r="1.7" fill="#111"/>')

    def knob(x, y, r=4.0):
        c.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#1b1b1b"/><rect x="{x - .3}" y="{y - r}" width=".6" height="{r * .6}" fill="#fff"/>')

    def bezel(x, y):
        c.append(f'<circle cx="{x}" cy="{y}" r="4.0" fill="#333"/><circle cx="{x}" cy="{y}" r="2.8" fill="#eee"/>')

    jack(10, 27); jack(22, 27)
    for y in R:
        knob(8, y); knob(19.5, y); knob(31, y)
        c.append(f'<circle cx="39.5" cy="{y}" r="1.5" fill="#ffe14a"/>')
        jack(49.5, y)
    knob(C1, R[0]); jack(C2, R[0]); bezel(C3, R[0])
    knob(C1, R[1]); jack(C2, R[1]); knob(C3, R[1], 3.0)
    knob(C1, R[2]); jack(C2, R[2]); knob(C3, R[2])
    knob(C1, R[3]); jack(C2, R[3]); knob(C3, R[3])
    knob(C1, R[4]); jack(C2, R[4])
    bezel(C1, R[5]); jack(C2, R[5])
    for k, o in enumerate([-12, -4, 3, 9, -20, 15]):
        y = 18.5 + 16.5 / 6 * (k + 0.5)
        c.append(f'<circle cx="{59 + o * 26 / 50:.2f}" cy="{y:.2f}" r="0.85" fill="#E4E052"/>')
    for x, y in [(7.5, 1.6), (W - 7.5, 1.6), (7.5, 126.9), (W - 7.5, 126.9)]:
        c.append(f'<circle cx="{x}" cy="{y}" r="1.7" fill="#b5b5b5"/>')
    # numbered badges, offset up-left of each control
    for n, x, y, _ in CALLOUTS:
        bx, by = x - 4.2, y - 4.2
        if n == 3:
            bx, by = 31.2, 20.6
        elif n in (1, 2):
            by = y + 4.2
        c.append(f'<g class="badge"><circle cx="{bx}" cy="{by}" r="2.1" fill="#1d1d1f" stroke="#fff" stroke-width=".35"/>'
                 f'<text x="{bx}" y="{by + 0.85}" text-anchor="middle" font-size="2.4" font-weight="700" fill="#fff" '
                 f'font-family="Inter, system-ui, sans-serif">{n}</text></g>')
    s = s.replace("</svg>", "".join(c) + "</svg>")
    s = re.sub(r'<svg xmlns="http://www.w3.org/2000/svg" width="[^"]+" height="[^"]+"',
               '<svg xmlns="http://www.w3.org/2000/svg" class="panel" role="img" aria-label="Drift Ensemble panel with numbered controls"', s, count=1)
    return s


def ctrl(n, name, kind, rng, default, body):
    meta = f'<span class="chip">{kind}</span>'
    if rng:
        meta += f'<span class="chip">Range: {rng}</span>'
    if default:
        meta += f'<span class="chip">Default: {default}</span>'
    return f'''
<article class="ctrl" id="c{n}">
  <h3><span class="num">{n}</span>{name}</h3>
  <div class="meta">{meta}</div>
  {body}
</article>'''


CONTROLS_TOP = [
    ctrl(1, "Clock in", "Input", "rising edge above 1 V", None, """
<p>The steady 16th-note clock that drives all six players. Each rising edge is one step. Send it from Live (the Host Clock module when Rack Pro runs inside Live), a clock module, or any sequencer's 16th output.</p>
<ul>
<li><b>Tempo is learned from the clock.</b> The module measures the time between ticks and predicts the next one, so players can hit <em>before</em> the clock with no added latency. Small jitter is smoothed out; a tempo jump of more than 15% is followed straight away.</li>
<li><b>First ticks:</b> until two ticks have arrived the tempo is unknown, so players hit exactly on the tick. Drift starts from the second step.</li>
<li><b>Stopping and starting:</b> if no tick arrives for 2.5 steps the clock counts as stopped. When it restarts, the old tempo is used for the first tick and the new one from the second.</li>
</ul>"""),
    ctrl(2, "Reset in", "Input", "rising edge above 1 V", None, """
<p>Pulls every player's drift back to zero. The drift then grows again by itself, so the band "regroups" and gradually loosens.</p>
<ul>
<li><b>Marks the downbeat.</b> The step that goes with the reset becomes step 1 of the bar for Swing, Anchor and Push. If the reset arrives within a quarter step after a clock tick (the usual case when reset and clock come from the same sequencer), that tick is the downbeat; otherwise the next tick is.</li>
<li><b>Never adds or drops pulses.</b> Players keep their place in the count, so sequencers driven from the outputs stay in step.</li>
<li>With a <a href="#c12">Seed</a> set, Reset also restarts the drift from that seed, so the same feel plays again.</li>
<li>With <a href="#menu">Loosen up after Reset</a> on, Energy ramps up again from zero.</li>
<li>The right-click item <b>Pull everyone back to the grid</b> does the same as a Reset pulse.</li>
</ul>"""),
    ctrl(3, "Drift display", "Screen", None, None, """
<p>Six lanes, one per player, top to bottom. The centre line is the clock; each dot is where that player's last hit landed: left of centre is early, right is late. The faint lines mark &plusmn;25 ms and the edges are &plusmn;50 ms.</p>
<p>A dot flashes bright and large for a moment on each hit. In Leader mode, player 1's dot turns pink. The display shows the total offset of each hit: drift plus Lean, Push and Anchor (Swing is not shown, since it is a deliberate delay of the off-beats).</p>"""),
]

CONTROLS_PLAYER = [
    ctrl(4, "Spread", "Knob, one per player", "0 to 50 ms", "0 ms", """
<p>How far this player wanders from the beat: the typical size of its timing drift (technically the standard deviation of the 1/f noise). The knob is curved to give more room to small, subtle values: <b>noon is 15 ms</b>, about what the paper measured for a good drummer.</p>
<p>The player's actual spread is <b>this knob + <a href="#c11">Spread &plusmn;</a></b>, kept between 0 and 50 ms, then scaled by <a href="#c17">Energy</a>. With this knob at 0 and the master at 0, the player is perfectly on the grid (apart from Lean, Swing and Push).</p>
<p class="tip">About 4% of hits land further than twice the Spread from the beat; drift is limited to four times the Spread so no single hit goes wild.</p>"""),
    ctrl(5, "Lean", "Knob, one per player", "&minus;35 to +35 ms", "0 ms (centre)", """
<p>A fixed push or lay-back. Negative values make the player always early, positive values always late, and the drift wobbles around that point instead of around the beat.</p>
<p class="tip">The paper found drummers sit on average about 16 ms <em>ahead</em> of a click. Try hats at &minus;8 to &minus;15 ms and a snare laid back at +10 to +20 ms. Lean is not scaled by Energy, but it is pulled in by <a href="#c14">Anchor</a> on the downbeat.</p>"""),
    ctrl(6, "Swing (per player)", "Knob, one per player", "50% to 75%", "50% (straight)", """
<p>Delays every second 16th of this player. 50% is straight; 66% is a triplet feel; 75% puts the off-beat 16th halfway to the next one.</p>
<p>It <b>adds to the <a href="#c15">master Swing</a></b>: the amount above 50% from both knobs is summed, and the total is capped at 75%. For example, player 55% with master 60% plays at 65%. The off-beats are counted from the downbeat set by the first clock or by Reset.</p>"""),
    ctrl(7, "Hit light", "Light, one per player", None, None, """
<p>Flashes yellow on each of the player's hits. In Leader mode, player 1's light flashes pink to show it is leading. The number under the light is the player.</p>"""),
    ctrl(8, "Out", "Output, one per player", "0 or 10 V", "5 ms trigger", """
<p>The player's drifting clock: exactly one pulse for every input clock tick, in order. By default a 5 ms trigger; choose <b>Gate (half a step)</b> in the right-click menu for modules that want longer gates.</p>"""),
]

CONTROLS_MASTER = [
    ctrl(9, "Together + CV", "Knob + CV input", "0 to 100%", "30%", """
<p>How much the players drift <em>together</em>. What "together" means depends on the <a href="#c10">Leader</a> button:</p>
<ul>
<li><b>Band mode</b> (Leader off): each player mixes its own drift with one shared "band" drift. At 0% everyone drifts independently; at 100% the whole band pushes and drags as one, still each by its own Spread.</li>
<li><b>Leader mode</b> (Leader on): players 2 to 6 follow player 1. At 0% they ignore the leader; at 100% they copy the leader's drift exactly (plus their own Lean and Swing).</li>
</ul>
<p>CV: &plusmn;10 V sweeps the whole range, added to the knob.</p>"""),
    ctrl(10, "Leader", "Latching button with light", "off / on", "off (Band mode)", """
<p>Switches <a href="#c9">Together</a> into Leader mode, with player 1 always leading. Its light is on in Leader mode, and player 1's hit light and display dot turn pink.</p>
<p>By default followers react to the leader's <b>previous</b> hit, like a musician who hears a push and adjusts on the next note; this gives a subtle chasing effect. Turn off <b>Leader mode: react to the leader's previous hit</b> in the right-click menu to make them follow the same hit instead.</p>
<p class="tip">Put your kick or hats on player 1 so the rest of the kit locks to it.</p>"""),
    ctrl(11, "Spread ± and CV", "Knob + CV input", "&minus;20 to +20 ms", "0 ms", """
<p>A master offset added to every player's own <a href="#c4">Spread</a>. Turn it up to loosen the whole band, down to tighten it. Each player's result is kept between 0 and 50 ms, so a player with Spread 10 ms and master &minus;15 ms is simply on the grid.</p>
<p>CV: 1 V = 4 ms, so &plusmn;5 V covers the whole range.</p>"""),
    ctrl(12, "Seed", "Small knob (stepped)", "0 to 32", "0", """
<p>Makes the drift repeatable. At <b>0</b> the drift is new every time. At <b>1 to 32</b>, the very first clock and every Reset restart the drift from that seed, so with the same settings and tempo the band plays exactly the same feel again. Handy when you find a take you like and want it back for the recording.</p>"""),
    ctrl(13, "Feel + CV", "Knob + CV input", "0 to 2 (shown as beta)", "1.00 (human)", """
<p>The character of the drift for the whole band: <em>how</em> the timing moves over time (Spread sets how far). Technically it is the exponent beta of 1/f<sup>beta</sup> noise.</p>
<table class="zones">
<tr><th>Value</th><th>Name</th><th>What you hear</th></tr>
<tr><td>0 to 0.4</td><td>jitter</td><td>Every hit random, no memory. Busy and nervous, like a basic humanizer.</td></tr>
<tr><td>0.4 to 0.8</td><td>nervous</td><td>Mostly jitter, with short pushes and drags over a few hits.</td></tr>
<tr><td>0.8 to 1.3</td><td>human</td><td>The paper's zone: late tends to stay late for a while, then drifts back. Listeners preferred it and heard it as tighter.</td></tr>
<tr><td>1.3 to 1.7</td><td>loose</td><td>Slow swells take over: leaning ahead or behind for a bar or two.</td></tr>
<tr><td>1.7 to 2</td><td>wander</td><td>Smooth tempo drift over many bars, like playing without a click.</td></tr>
</table>
<p>Feel can be turned live without jumps. CV: &plusmn;10 V sweeps the whole range.</p>"""),
    ctrl(14, "Anchor", "Knob", "0 to 100%", "0%", """
<p>Real bands loosen between beats but lock up on the "one". Anchor shrinks every player's drift <em>and</em> Lean on the <b>first 16th of each bar</b>: at 100% everyone lands exactly on the downbeat together, at 50% they land half as far off as they otherwise would.</p>
<p>The bar length is set in the right-click menu (default 16 steps = one 4/4 bar of 16ths), counted from the first clock or the last Reset.</p>"""),
    ctrl(15, "Swing (master) + CV", "Knob + CV input", "50% to 75%", "50% (straight)", """
<p>Swing for the whole band: delays every second 16th. Each player's own <a href="#c6">Swing</a> adds to it, with the total capped at 75%. CV: &plusmn;10 V sweeps the whole range.</p>"""),
    ctrl(16, "Push", "Knob", "0 to 30 ms", "0 ms", """
<p>Musicians rush slightly into a fill or a new section. Push makes every player gradually play earlier through the <b>last bar of each phrase</b>, reaching the full amount on the final 16th, then the new phrase starts back on the beat.</p>
<p>The phrase length is set in the right-click menu (2, 4, 8 or 16 bars; default 8). Push is not scaled by Energy.</p>"""),
    ctrl(17, "Energy + CV", "Knob + CV input", "0 to 100%", "100%", """
<p>Scales all drift: 0% is a perfectly tight band (Lean, Swing and Push still apply), 100% is the full Spread. Patch an envelope, LFO or a fader from Live to the CV input to tighten verses and loosen breakdowns: an arrangement tool against loop fatigue.</p>
<p>With <b>Loosen up after Reset</b> in the right-click menu, Energy also ramps from 0 up to the knob's value over 8, 16, 32 or 64 bars after each Reset, like a jam getting looser. CV: &plusmn;10 V sweeps the whole range.</p>"""),
    ctrl(18, "Freeze button + gate", "Latching button with light + gate input", "off / on (gate: high above 1 V)", "off", """
<p>Holds every player's current offset: the drift stops moving, so each player keeps hitting with the same timing until released, then carries on from there. The button latches; the gate input freezes while it is high. Either one freezes, and the light shows when frozen.</p>
<p class="tip">Use it to hold a feel you like for a section, or gate it from a sequencer to freeze the groove on chosen bars.</p>"""),
]

MENU = """
<table class="menu">
<tr><th>Item</th><th>Choices</th><th>What it does</th></tr>
<tr><td>Greyscale panel</td><td>on / off</td><td>Switches to a grey version of the same panel. Saved with the patch.</td></tr>
<tr><td>Output shape</td><td>Trigger (5 ms) / Gate (half a step)</td><td>Pulse length on all six outputs.</td></tr>
<tr><td>Leader mode: react to the leader's previous hit</td><td>on (default) / off</td><td>Followers respond to the leader's previous hit, or to the same hit.</td></tr>
<tr><td>Bar length (for Anchor and Push)</td><td>12, 16 (default), 24, 32 steps</td><td>How many 16ths make a bar. 12 suits 3/4 or 12/8.</td></tr>
<tr><td>Phrase length (for Push)</td><td>2, 4, 8 (default), 16 bars</td><td>Push rushes through the last bar of each phrase.</td></tr>
<tr><td>Loosen up after Reset</td><td>off (default), 8, 16, 32, 64 bars</td><td>Energy ramps from 0 to its knob value over that many bars after each Reset.</td></tr>
<tr><td>Pull everyone back to the grid</td><td>action</td><td>Same as a pulse at the Reset input.</td></tr>
</table>
<p>Standard Rack items also work: <b>Initialize</b> returns every knob and menu setting to its default, <b>Randomize</b> randomizes the knobs and scrambles the drift.</p>"""

IDEAS = """
<div class="cards">
<div class="card"><h4>Detroit groove</h4><p>Kick on player 1 with Spread 0. Hats on players 2 and 3 with Spread at noon (15 ms) and Lean &minus;10 ms, snare with Lean +15 ms. Feel 1.0, Together 40%, Anchor 70%, master Swing 56%. Tight on the one, alive in between.</p></div>
<div class="card"><h4>Autechre-style phasing</h4><p>Feel 1.8 (wander), Together 0%, Spread 30 to 50 ms on two percussion players driving their own sequencers. They drift slowly against the kick and come back, without ever losing their place in the pattern.</p></div>
<div class="card"><h4>Follow the drummer</h4><p>Leader on, Together 80%. Player 1 has a large Spread and drives the hats; the others trail it one hit behind, like a band locking to its drummer.</p></div>
<div class="card"><h4>Arrangement with Energy</h4><p>Loosen up after Reset over 32 bars, Push 12 ms with 8-bar phrases. Reset at the start of each section: the band starts tight, loosens as the section develops, and rushes into the next one.</p></div>
</div>"""


def build():
    controls = "".join(CONTROLS_TOP) + '<h2 id="players">Player controls (one row per player)</h2>' + "".join(CONTROLS_PLAYER) \
        + '<h2 id="master">Master section</h2>' + "".join(CONTROLS_MASTER)
    legend = "".join(f'<li><a href="#c{n}"><span class="num sm">{n}</span>{name}</a></li>' for n, _, _, name in CALLOUTS)
    html = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Drift Ensemble Guide</title>
<style>
:root {{
  --bg: #f6f2ea; --surface: #ffffff; --ink: #24121b; --soft: #5d4b54; --line: #e3d9cc;
  --pink: #be3270; --pink-soft: #f6dbe7; --green: #9bc96b; --green-soft: #e6f2d9; --yellow: #e4e052;
  --chip: #efe7da; --code: #f1ebe1;
}}
@media (prefers-color-scheme: dark) {{
  :root:not([data-theme="light"]) {{
    --bg: #17121a; --surface: #211a24; --ink: #f2ecef; --soft: #b9aab2; --line: #3a2f3d;
    --pink: #f06aa4; --pink-soft: #3d1f2e; --green: #9bc96b; --green-soft: #22301a; --yellow: #e4e052;
    --chip: #2e2531; --code: #2a222d;
  }}
}}
:root[data-theme="dark"] {{
  --bg: #17121a; --surface: #211a24; --ink: #f2ecef; --soft: #b9aab2; --line: #3a2f3d;
  --pink: #f06aa4; --pink-soft: #3d1f2e; --green: #9bc96b; --green-soft: #22301a; --yellow: #e4e052;
  --chip: #2e2531; --code: #2a222d;
}}
* {{ box-sizing: border-box; }}
html {{ scroll-behavior: smooth; }}
body {{ margin: 0; background: var(--bg); color: var(--ink); font: 16px/1.6 Inter, system-ui, -apple-system, "Segoe UI", sans-serif; }}
a {{ color: var(--pink); }}
header.hero {{ background: #be3270; color: #f7f2e8; padding: 40px 16px 56px; position: relative; overflow: hidden; }}
header.hero::after {{ content: ""; position: absolute; left: 0; right: 0; bottom: 0; height: 18px;
  background: linear-gradient(135deg, var(--bg) 25%, transparent 25%) -9px 0/18px 18px, linear-gradient(225deg, var(--bg) 25%, transparent 25%) -9px 0/18px 18px; }}
.wrap {{ max-width: 1040px; margin: 0 auto; padding: 0 16px; }}
header.hero h1 {{ margin: 0; font-size: clamp(30px, 6vw, 52px); letter-spacing: .06em; line-height: 1.1; }}
header.hero p {{ margin: 10px 0 0; color: #e4e052; font-size: 18px; max-width: 640px; }}
nav.toc {{ display: flex; flex-wrap: wrap; gap: 8px; margin: 24px 0 8px; }}
nav.toc a {{ text-decoration: none; background: var(--surface); border: 1px solid var(--line); color: var(--ink); padding: 6px 12px; border-radius: 999px; font-size: 14px; }}
h2 {{ font-size: 26px; margin: 48px 0 12px; padding-top: 8px; border-top: 3px solid var(--pink); }}
h3 {{ margin: 0 0 6px; font-size: 20px; display: flex; align-items: center; gap: 10px; }}
h4 {{ margin: 0 0 6px; }}
p, li {{ max-width: 72ch; }}
.lead {{ font-size: 18px; }}
.layout {{ display: grid; grid-template-columns: minmax(0, 420px) minmax(0, 1fr); gap: 32px; align-items: start; }}
.panel {{ width: 100%; height: auto; border-radius: 6px; box-shadow: 0 6px 24px rgba(0,0,0,.25); display: block; }}
.legend {{ list-style: none; padding: 0; margin: 0; columns: 2; column-gap: 16px; }}
.legend li {{ margin: 0 0 6px; break-inside: avoid; }}
.legend a {{ text-decoration: none; color: var(--ink); display: inline-flex; align-items: center; gap: 8px; }}
.num {{ display: inline-flex; align-items: center; justify-content: center; min-width: 30px; height: 30px; border-radius: 50%;
  background: var(--ink); color: var(--bg); font-weight: 700; font-size: 15px; flex: none; }}
.num.sm {{ min-width: 24px; height: 24px; font-size: 12px; }}
.ctrl {{ background: var(--surface); border: 1px solid var(--line); border-left: 5px solid var(--pink); border-radius: 10px; padding: 18px 20px; margin: 14px 0; scroll-margin-top: 16px; }}
.meta {{ display: flex; flex-wrap: wrap; gap: 6px; margin: 4px 0 10px; }}
.chip {{ background: var(--chip); border-radius: 6px; padding: 2px 8px; font-size: 13px; color: var(--soft); }}
.tip {{ background: var(--green-soft); border-radius: 8px; padding: 8px 12px; }}
.tip::before {{ content: "Tip: "; font-weight: 700; }}
table {{ border-collapse: collapse; width: 100%; margin: 10px 0; font-size: 15px; }}
th, td {{ text-align: left; padding: 8px 10px; border-bottom: 1px solid var(--line); vertical-align: top; }}
th {{ color: var(--soft); font-weight: 600; }}
.tablewrap {{ overflow-x: auto; }}
.steps {{ counter-reset: s; list-style: none; padding: 0; }}
.steps li {{ counter-increment: s; position: relative; padding-left: 44px; margin: 0 0 12px; }}
.steps li::before {{ content: counter(s); position: absolute; left: 0; top: 0; width: 30px; height: 30px; border-radius: 50%;
  background: var(--pink); color: #fff; font-weight: 700; display: flex; align-items: center; justify-content: center; }}
.cards {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 14px; }}
.card {{ background: var(--surface); border: 1px solid var(--line); border-top: 5px solid var(--green); border-radius: 10px; padding: 16px; }}
.card p {{ margin: 0; font-size: 15px; }}
.callout {{ background: var(--pink-soft); border-radius: 10px; padding: 14px 18px; }}
footer {{ margin: 56px 0 40px; color: var(--soft); font-size: 14px; }}
@media (max-width: 760px) {{
  .layout {{ grid-template-columns: 1fr; }}
  .legend {{ columns: 1; }}
  .ctrl {{ padding: 14px; }}
}}
</style>
</head>
<body>
<header class="hero"><div class="wrap">
  <h1>DRIFT ENSEMBLE</h1>
  <p>Six human clocks for VCV Rack. User guide for version 2.2.</p>
</div></header>
<main class="wrap">
<nav class="toc" aria-label="Contents">
  <a href="#what">What it does</a><a href="#start">Quick start</a><a href="#panel">The panel</a>
  <a href="#players">Player controls</a><a href="#master">Master section</a><a href="#menu">Right-click menu</a>
  <a href="#seq">Driving sequencers</a><a href="#ideas">Patch ideas</a><a href="#science">The science</a>
</nav>

<h2 id="what">What it does</h2>
<p class="lead">Drift Ensemble takes one steady 16th-note clock and turns it into six "players" whose clocks drift against it the way human musicians do: a little early, a little late, in a way that has memory and feels alive rather than random.</p>
<p>Each player's timing is the sum of four things:</p>
<ul>
<li><b>Drift</b>: 1/f noise sized by <a href="#c4">Spread</a>, coloured by <a href="#c13">Feel</a>, shared between players by <a href="#c9">Together</a>, scaled by <a href="#c17">Energy</a>.</li>
<li><b>Lean</b>: a fixed early or late offset per player.</li>
<li><b>Swing</b>: a delay on every second 16th, per player plus master.</li>
<li><b>Arrangement shaping</b>: <a href="#c14">Anchor</a> tightens the downbeat, <a href="#c16">Push</a> rushes into each new phrase.</li>
</ul>
<p>Whatever you do, every output gives exactly one pulse per input clock, so you can clock sequencers from it without them falling out of step.</p>

<h2 id="start">Quick start</h2>
<ol class="steps">
<li>Patch a 16th-note clock into <a href="#c1">Clock</a>, and optionally your sequencer's reset or start pulse into <a href="#c2">Reset</a>.</li>
<li>Patch each <a href="#c8">Out</a> to a drum voice or to the clock input of a sequencer.</li>
<li>All player Spreads start at 0, so the band is on the grid. Turn up <a href="#c11">Spread &plusmn;</a> to loosen everyone, or turn individual <a href="#c4">Spread</a> knobs to noon (15 ms).</li>
<li>Leave <a href="#c13">Feel</a> at noon (human), then move <a href="#c9">Together</a> between apart and together and watch the <a href="#c3">display</a>.</li>
<li>Add <a href="#c5">Lean</a> and <a href="#c6">Swing</a> per player to give each part its own pocket, and <a href="#c14">Anchor</a> to keep the "one" tight.</li>
</ol>

<h2 id="panel">The panel</h2>
<div class="layout">
  <div>{panel_svg()}</div>
  <div>
    <p>Click a name to jump to its description. Every knob also shows its exact value when you hover over it in Rack.</p>
    <ul class="legend">{legend}</ul>
    <p class="callout"><b>CV inputs</b> (Together, Spread &plusmn;, Feel, Swing, Energy) add to their knob. &plusmn;10 V sweeps the whole range of the knob, so &plusmn;5 V covers it from the centre.</p>
  </div>
</div>

<h2 id="top">Top row</h2>
{controls}

<h2 id="menu">Right-click menu</h2>
<div class="tablewrap">{MENU}</div>

<h2 id="seq">Driving sequencers</h2>
<p>The module is built so its outputs can clock sequencers (Metropolix, Pam's, Rack sequencers) without drifting out of step:</p>
<ul>
<li>Each player plays every step once, in order: no skipped or doubled pulses, whatever the settings.</li>
<li>A player can run at most one step ahead of the clock (early hits), and up to four steps behind (very late drift). Late players always catch up.</li>
<li>Two hits of one player are always at least a quarter step apart, so fast catch-ups never blur into one pulse.</li>
<li>If the clock stops while a player is running early, that player may already have played its next pulse; it then skips it when the clock restarts, so counts always line up again.</li>
<li>Reset changes the drift only, never the count. Send the same reset to your sequencers and to Drift Ensemble's Reset input.</li>
</ul>
<p class="tip">With a large Spread or Lean at fast tempos, a player can be a whole step off. That's the sound of real drift; the sequencer it drives will still be on the right note, just at a human moment.</p>

<h2 id="ideas">Patch ideas</h2>
{IDEAS}

<h2 id="science">The science</h2>
<p>Hennig and colleagues (2011, <i>PLoS ONE</i> 6(10): e26457, "The Nature and Perception of Fluctuations in Human Musical Rhythms") recorded drummers and other musicians playing to a click. Their timing errors were not random from beat to beat: they followed <b>1/f noise</b>, with exponents beta between about 0.2 and 1.3 and a typical size of 12 to 21 ms. When they "humanized" a song with this kind of noise (beta 1, 15 ms), listeners preferred it to plain random jitter and even judged it more precise.</p>
<p>Drift Ensemble builds 1/f noise in real time by mixing eight smooth random signals whose memory runs from one step to about 2,000 steps; weighting the slow ones more or less sets beta (the <a href="#c13">Feel</a> knob). Togetherness, Leader mode, Lean, Swing, Anchor, Push and Energy are additions on top of the paper's model, to make it musical and useful for arrangement.</p>

<footer>Drift Ensemble 2.2 by Haer. Source: github.com/hardyfinn/DriftEnsemble</footer>
</main>
</body>
</html>
"""
    with open(OUT, "w") as fh:
        fh.write(html)
    print("wrote", OUT)


build()
