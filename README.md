# Drift Ensemble

A VCV Rack 2 module: one steady 16th-note clock in, six "players" out. Each
player drifts against the clock the way human musicians do, following the
1/f timing model from Hennig et al., "The Nature and Perception of Fluctuations
in Human Musical Rhythms", PLoS ONE 6(10): e26457 (2011).

## Controls

| Control | What it does |
|---|---|
| **Clock** in | Steady 16th-note clock (from Live, CV Tools, a clock module...) |
| **Reset** in | Pulls every player's drift back to zero (it then grows again). Also marks the downbeat for Swing |
| **Freeze** button + gate | Holds every player's current offset until released |
| **Spread** (per player) | How far the player wanders: 0 to 50 ms, noon = 15 ms, double-click = 20 ms |
| **Lean** (per player) | Fixed push or lay-back, -25 ms (early) to +25 ms (late). Drift wobbles around it |
| **Out** (per player) | The player's clock (trigger, or half-step gate from the right-click menu) |
| **Together** + CV | Band mode: 0% = players drift independently, 100% = the band drifts as one. Leader mode: how closely players 2-6 follow player 1 |
| **Leader** button | Switches Together to Leader mode; player 1 leads (its light turns pink). By default followers react to the leader's previous hit (right-click to change) |
| **Seed** | 0 = new drift every time. 1-32 = Reset (or the first clock) replays exactly the same drift |
| **Spread ±** + CV | Master offset added to every player's Spread, -20 to +20 ms. Each player stays within 0-50 ms |
| **Feel** + CV | Colour of the drift for the whole band (beta 0 to 2): jitter, nervous, human (noon), loose, wander |
| **Swing** + CV | Delays every second 16th: 50% (straight) to 75% |

CV inputs: 10 V sweeps the whole knob range.

**Sequencer-safe:** every player outputs exactly one pulse per input clock, in
order, through clock stops, restarts and Reset. A player running early may play
its next pulse just before the clock stops; it then skips that pulse when the
clock restarts, so counts always line up.

Players can hit *before* the clock: the module predicts the next clock tick
from the measured tempo, so there is no added latency.

## Building

GitHub Actions builds the Apple Silicon version on every push (see
`.github/workflows/build.yml`). Locally: download the Rack SDK and run
`make dist RACK_DIR=/path/to/Rack-SDK`.

The panel is drawn by `res/make_panel.py` (needs `pip install fonttools`).

The timing engine (`src/DriftEngine.hpp`) has no Rack dependencies. `test/engine_test.cpp`
checks it offline (drift size, Lean, Swing, Leader, Freeze, Seed, exact pulse count).
