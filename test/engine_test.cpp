// Offline tests for the Drift Ensemble engine. Build and run:
//   c++ -O2 -std=c++11 -o engine_test test/engine_test.cpp && ./engine_test
// "./engine_test dump beta spreadMs togetherness steps" prints every hit for
// test/analyze.py instead.
#include "../src/DriftEngine.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <map>

using namespace drift;

static const double SR = 48000.0, DT = 1.0 / SR;
static const long SPT = 5538; // samples per 16th at 130 BPM

struct Hit { int player; int64_t step; double time; };

// Clock pattern: list of [startStep, endStep) runs, each followed by a gap.
struct Runner {
	Engine e;
	std::vector<Hit> hits;
	std::map<int64_t, double> tickTime; // step index -> clock time
	long sample = 0;
	int64_t ticks = 0;

	void run(long samples, bool clockOn, long resetAtSample = -1) {
		for (long s = 0; s < samples; s++, sample++) {
			bool edge = clockOn && (s % SPT) == 0;
			bool reset = (s == resetAtSample);
			if (edge) { ticks++; tickTime[ticks] = e.now; }
			e.process(DT, edge, reset);
			for (int i = 0; i < NUM_PLAYERS; i++)
				if (e.fired[i])
					hits.push_back({i, e.nextStep[i] - 1, e.now - DT});
		}
	}
	int count(int p) const {
		int c = 0;
		for (auto& h : hits) if (h.player == p) c++;
		return c;
	}
	// Offsets (ms) of player p's hits from their own clock tick
	std::vector<double> offsets(int p, int64_t fromStep = 0) const {
		std::vector<double> o;
		for (auto& h : hits) {
			if (h.player != p || h.step < fromStep) continue;
			auto it = tickTime.find(h.step);
			if (it != tickTime.end()) o.push_back((h.time - it->second) * 1000.0);
		}
		return o;
	}
};

static double mean(const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return s / v.size(); }
static double sd(const std::vector<double>& v) { double m = mean(v), s = 0; for (double x : v) s += (x - m) * (x - m); return std::sqrt(s / v.size()); }
static double corr(const std::vector<double>& a, const std::vector<double>& b) {
	size_t n = std::min(a.size(), b.size());
	double ma = 0, mb = 0; for (size_t i = 0; i < n; i++) { ma += a[i]; mb += b[i]; } ma /= n; mb /= n;
	double sab = 0, saa = 0, sbb = 0;
	for (size_t i = 0; i < n; i++) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
	return sab / std::sqrt(saa * sbb);
}

static int failures = 0;
static void check(bool ok, const char* what) {
	printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) failures++;
}

static void setAll(Engine& e, double spreadMs, double beta) {
	for (int i = 0; i < NUM_PLAYERS; i++) { e.laneSpread[i] = spreadMs / 1000.0; e.lean[i] = 0; }
	e.beta = beta;
}

int main(int argc, char** argv) {
	if (argc > 1 && !strcmp(argv[1], "dump")) {
		Runner r;
		setAll(r.e, argc > 3 ? atof(argv[3]) : 20, argc > 2 ? atof(argv[2]) : 1.0);
		r.e.togetherness = argc > 4 ? atof(argv[4]) : 0.3;
		int steps = argc > 5 ? atoi(argv[5]) : 8192;
		r.run(SPT * steps, true);
		for (auto& h : r.hits) printf("%d %.6f 0 %lld\n", h.player, h.time, (long long) h.step);
		return 0;
	}

	// 1. Drift size follows Spread, and master Spread adds to it (clamped 0..50)
	{
		Runner r; setAll(r.e, 20, 1.0); r.e.togetherness = 0;
		r.e.laneSpread[1] = 0.010; r.e.masterSpread = 0.005;
		r.e.laneSpread[2] = 0.010; // with master +5: 15 ms
		r.e.laneSpread[4] = 0.050; // 50 + 5 > 50: capped at 50
		r.run(SPT * 16000, true);
		Runner r2; setAll(r2.e, 3, 1.0); r2.e.masterSpread = -0.005; // 3 - 5 < 0: no drift
		r2.run(SPT * 2000, true);
		double s0 = sd(r.offsets(0, 2000)), s2 = sd(r.offsets(2, 2000)), s3 = sd(r2.offsets(3, 100)), s4 = sd(r.offsets(4, 2000));
		printf("      SD: lane 20+5 -> %.1f ms, 10+5 -> %.1f ms, 3-5 -> %.2f ms, 50+5 -> %.1f ms\n", s0, s2, s3, s4);
		check(s0 > 18 && s0 < 32, "Spread 20 + master 5 gives about 25 ms");
		check(s2 > 11 && s2 < 19, "Spread 10 + master 5 gives about 15 ms");
		check(s3 < 0.1, "combined Spread never goes below 0 ms");
		check(s4 > 38 && s4 < 58, "combined Spread is capped at 50 ms");
	}
	// 2. Lean shifts the average
	{
		Runner r; setAll(r.e, 5, 1.0);
		r.e.lean[0] = -0.016; r.e.lean[1] = 0.012;
		r.run(SPT * 4000, true);
		double m0 = mean(r.offsets(0, 100)), m1 = mean(r.offsets(1, 100));
		printf("      mean offset: lean -16 -> %.1f ms, lean +12 -> %.1f ms\n", m0, m1);
		check(std::fabs(m0 + 16) < 3 && std::fabs(m1 - 12) < 3, "Lean moves a player early or late");
	}
	// 3. Swing delays every second 16th
	{
		Runner r; setAll(r.e, 0, 1.0); r.e.swing = 0.66;
		r.run(SPT * 200, true);
		std::vector<double> on, off;
		for (auto& h : r.hits) if (h.player == 0 && h.step >= 4 && r.tickTime.count(h.step)) (((h.step - 1) % 2) ? off : on).push_back((h.time - r.tickTime[h.step]) * 1000.0);
		double expect = (0.66 - 0.5) * 2 * SPT / SR * 1000.0;
		printf("      on-beats %.2f ms, off-beats %.2f ms (expected %.2f)\n", mean(on), mean(off), expect);
		check(std::fabs(mean(on)) < 0.1 && std::fabs(mean(off) - expect) < 0.1, "Swing delays only the off-beat 16ths");
	}
	// 4. Leader mode: followers copy the leader's previous hit
	{
		Runner r; setAll(r.e, 20, 1.0); r.e.leaderMode = true; r.e.togetherness = 1.0;
		r.run(SPT * 6000, true);
		auto L = r.offsets(0, 100), F = r.offsets(3, 100);
		std::vector<double> Lprev(L.begin(), L.end() - 1), Fnext(F.begin() + 1, F.begin() + L.size());
		double cPrev = corr(Lprev, Fnext);
		printf("      follower vs leader's previous hit: correlation %.3f\n", cPrev);
		check(cPrev > 0.98, "at 100% a follower copies the leader's previous hit");
		Runner r2; setAll(r2.e, 20, 0.0); r2.e.leaderMode = true; r2.e.togetherness = 0.0;
		r2.run(SPT * 6000, true);
		double c0 = corr(r2.offsets(0, 100), r2.offsets(3, 100));
		check(std::fabs(c0) < 0.1, "at 0% followers ignore the leader");
	}
	// 5. Freeze holds every offset
	{
		Runner r; setAll(r.e, 20, 1.0);
		r.run(SPT * 500, true);
		r.e.frozen = true;
		size_t from = r.hits.size();
		r.run(SPT * 200, true);
		double lo = 1e9, hi = -1e9;
		for (size_t k = from + 30; k < r.hits.size(); k++)
			if (r.hits[k].player == 2 && r.tickTime.count(r.hits[k].step)) {
				double o = (r.hits[k].time - r.tickTime[r.hits[k].step]) * 1000.0;
				lo = std::min(lo, o); hi = std::max(hi, o);
			}
		printf("      frozen offsets range %.3f ms\n", hi - lo);
		check(hi - lo < 0.05, "Freeze holds each player's offset");
	}
	// 6. Seed: same seed + Reset gives the same drift again
	{
		auto take = [](int seed, int warmup) {
			Runner r; setAll(r.e, 20, 1.0); r.e.seed = seed;
			r.run(SPT * warmup, true);
			r.run(SPT * 300, true, 3); // reset just after a tick
			std::vector<double> o;
			for (auto& h : r.hits) if (h.player == 4 && h.time > r.tickTime[warmup + 1] + 0.01) o.push_back((h.time - r.tickTime[h.step]) * 1000.0);
			return o;
		};
		auto a = take(7, 400), b = take(7, 1234), c = take(8, 400);
		size_t n = std::min(a.size(), b.size());
		double maxd = 0; for (size_t k = 0; k < n; k++) maxd = std::max(maxd, std::fabs(a[k] - b[k]));
		printf("      seed 7 twice: max difference %.4f ms over %zu hits\n", maxd, n);
		check(maxd < 0.05 && n > 200, "a fixed seed repeats the same drift after Reset");
		check(std::fabs(a[50] - c[50]) > 0.01, "a different seed gives different drift");
	}
	// 7. Exact pulse count across stops, restarts and resets
	{
		Runner r; setAll(r.e, 50, 1.5); r.e.masterSpread = 0.02;
		r.e.lean[0] = -0.025; r.e.lean[1] = 0.025; r.e.swing = 0.7; r.e.togetherness = 0.5;
		srand(1);
		bool ok = true;
		for (int seg = 0; seg < 40; seg++) {
			long len = SPT * (3 + rand() % 60);
			r.e.leaderMode = seg % 3 == 1;
			r.e.frozen = seg % 7 == 3;
			r.run(len, true, seg % 4 == 2 ? SPT * 2 + 37 : -1);
			r.run(SPT * (1 + rand() % 12), false); // clock stopped
			for (int i = 0; i < NUM_PLAYERS; i++) {
				long diff = (long) r.count(i) - (long) r.ticks;
				if (diff < 0 || diff > 1) ok = false; // at most one early hit waiting
			}
		}
		r.run(SPT * 20, true);
		r.run(SPT * 20, false);
		bool exact = true;
		for (int i = 0; i < NUM_PLAYERS; i++) {
			printf("      player %d: %d hits for %lld clock ticks\n", i + 1, r.count(i), (long long) r.ticks);
			// steps played in order, none skipped or repeated
			int64_t expect = 1;
			for (auto& h : r.hits) if (h.player == i) { if (h.step != expect) exact = false; expect++; }
		}
		check(ok, "during stops a player is never behind, and at most one early hit ahead");
		check(exact, "every player plays every step once, in order (no skips, no doubles)");
	}
	printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
	return failures ? 1 : 0;
}
