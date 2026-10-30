// Offline test: feed a steady 16th clock at 130 BPM and log every hit.
#include "../src/DriftEngine.hpp"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
	double beta = argc > 1 ? atof(argv[1]) : 1.0;
	double spreadMs = argc > 2 ? atof(argv[2]) : 20.0;
	double tog = argc > 3 ? atof(argv[3]) : 0.3;
	int steps = argc > 4 ? atoi(argv[4]) : 8192;
	drift::Engine e;
	e.togetherness = tog;
	for (int i = 0; i < 6; i++) { e.players[i].beta = beta; e.players[i].spread = spreadMs / 1000.0; }
	const double sr = 48000.0, dt = 1.0 / sr;
	const double T = 60.0 / 130.0 / 4.0;
	long spt = long(T * sr + 0.5);
	long total = spt * (steps + 2);
	int64_t clockCount = 0;
	for (long s = 0; s < total; s++) {
		bool edge = (s % spt) == 0;
		if (edge) clockCount++;
		e.process(dt, edge, false);
		for (int i = 0; i < 6; i++)
			if (e.fired[i]) printf("%d %.6f %.4f %lld\n", i, e.now - dt, e.offsetMs[i], (long long)clockCount);
	}
}
