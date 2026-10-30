# Drift Ensemble

A VCV Rack 2 module: one steady 16th-note clock in, six "players" out. Each
player drifts against the clock the way human musicians do, following the
1/f timing model from Hennig et al., "The Nature and Perception of Fluctuations
in Human Musical Rhythms", PLoS ONE 6(10): e26457 (2011).

## Controls

| Control | What it does |
|---|---|
| **Clock** in | Steady 16th-note clock (from Live, CV Tools, a clock module...) |
| **Reset** in | Pulls every player back onto the grid; the drift then grows back |
| **Togetherness** + CV | 0% = players drift independently, 100% = the band drifts as one |
| **Spread** (per player) + CV | How far the player wanders from the clock: 0 to 80 ms (noon = 20 ms, about a real drummer) |
| **Feel** (per player) + CV | Colour of the drift (the exponent beta, 0 to 2). Left = random jitter, noon = the paper's human 1/f, right = slow tempo wander |
| **Out** (per player) | The player's clock (trigger, or half-step gate from the right-click menu) |
| **Err** (per player) | The player's timing error on its last hit, 1 V per 10 ms, positive = late. Held until the next hit |

CV inputs: 10 V sweeps the whole knob range.

Players can hit *before* the clock: the module predicts the next clock tick
from the measured tempo, so there is no added latency. After a tempo jump,
players re-lock within one tick. If the clock stops for more than 2.5 steps the
players stop too, and all start together on the next tick.

## Building

GitHub Actions builds the Apple Silicon version on every push (see
`.github/workflows/build.yml`). Locally: download the Rack SDK and run
`make dist RACK_DIR=/path/to/Rack-SDK`.

The panel is drawn by `res/make_panel.py` (needs `pip install fonttools`).

The timing engine (`src/DriftEngine.hpp`) has no Rack dependencies and can be
tested offline with `test/engine_test.cpp` and `test/analyze.py`.
