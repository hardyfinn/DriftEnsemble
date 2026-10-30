# Drift Ensemble for disting NT

The same six-player human clock as the VCV Rack module, as a disting NT
C++ plug-in.

## Install

1. Copy `DriftEnsembleNT.o` to the NT's SD card, into `programs/plug-ins/`.
2. Put the card back (or reboot the NT), then add the algorithm "Drift Ensemble".

Needs firmware with plug-in API v14 or later (update to the latest firmware).

## Parameters

| Page | Parameter | What it does |
|---|---|---|
| Ensemble | Clock source | CV = Clock in bus; MIDI = MIDI clock (e.g. Live over USB). Every 6 MIDI clocks = one 16th, Start pulls everyone back to the grid, Stop silences the players |
| Ensemble | Togetherness | 0% = players drift independently, 100% = the band drifts as one |
| Ensemble | Output shape | Trigger (5 ms) or Gate (half a step) |
| Spread | Spread 1-6 | How far each player wanders, 0-80 ms (20 ms ≈ a real drummer) |
| Feel | Feel 1-6 | Drift colour 0-2: jitter, nervous, human (1.00), loose, wander |
| Routing | Clock in | CV clock, 16ths (default input 1) |
| Routing | Reset in | Pulls everyone back to the grid (default input 2) |
| Routing | Player 1-6 out | 5 V clocks (default outputs 1-6) |
| Routing | Error 1-6 out | Timing error of each player's last hit, 1 V per 10 ms, + = late (off by default) |

Any parameter can be put under CV or MIDI control with the NT's own mapping.

The screen shows six lanes: the centre line is the clock, each dot is that
player's last hit (left = early, right = late), flashing on each hit.

## Building

    git clone https://github.com/expertsleepersltd/distingNT_API
    make NT_API_PATH=path/to/distingNT_API

`test/host_test.cpp` runs the plug-in on a computer with a fake host.
