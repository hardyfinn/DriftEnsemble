// DriftEngine.hpp
// The timing brain of Drift Ensemble, kept free of any VCV Rack code so it can
// be tested on its own.
//
// Model (after Hennig et al. 2011, PLoS ONE 6(10): e26457):
// a human player's timing deviations from the beat are Gaussian "1/f^beta"
// noise. Being a bit late now makes you likely to still be a bit late for a
// while, then you slowly drift back. beta = 0 is plain random jitter, beta = 1
// is the paper's typical human, beta = 2 is slow, wandering tempo drift.
//
// We build 1/f^beta noise in real time by mixing several smooth random
// signals (first-order low-pass noise, "AR(1)") whose time scales are spaced
// by a factor of 3. Weighting slow ones more or less sets beta. Because the
// internal states keep running when beta changes, Feel can be turned live
// without jumps.
//
// On top of the drift each player has a fixed Lean (always early or late),
// and the whole band shares one Swing amount.
//
// Togetherness has two modes:
//   Band   - everyone mixes in a shared drift, so the band leans into the same
//            push while each player keeps their own wobble.
//   Leader - players 2-6 follow player 1's drift. By default they react to the
//            leader's previous hit, the way a musician hears a push and adjusts
//            on the next note.
//
// Pulse count is exact: every player outputs exactly one hit per input clock,
// across clock stops, restarts and Reset, so the outputs can clock sequencers.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace drift {

static const int NUM_PLAYERS = 6;
// Pole 0 is white (no memory). Poles 1..7 remember 3, 9, ... 2187 steps.
static const int NUM_POLES = 8;
static const double MAX_SPREAD = 0.050; // seconds, limit on a player's combined spread

// Small, fast random generator (xoroshiro128+) with Gaussian output.
struct Rng {
	uint64_t s[2];
	explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ULL) { setSeed(seed); }
	void setSeed(uint64_t seed) {
		// splitmix64 to fill the state
		for (int i = 0; i < 2; i++) {
			seed += 0x9E3779B97F4A7C15ULL;
			uint64_t z = seed;
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
			s[i] = z ^ (z >> 31);
		}
	}
	static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
	uint64_t next() {
		uint64_t s0 = s[0], s1 = s[1];
		uint64_t r = s0 + s1;
		s1 ^= s0;
		s[0] = rotl(s0, 24) ^ s1 ^ (s1 << 16);
		s[1] = rotl(s1, 37);
		return r;
	}
	// Uniform in (0, 1]
	double uniform() { return ((next() >> 11) + 1) * (1.0 / 9007199254740992.0); }
	double normal() {
		double u1 = uniform(), u2 = uniform();
		return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
	}
};

// A bank of unit-variance AR(1) states, one per time scale, with its own
// random stream so each player's drift is independent of the others (and
// repeatable from a seed).
struct PoleBank {
	double state[NUM_POLES] = {};
	Rng rng;

	static double tau(int k) { return k == 0 ? 0.5 : std::pow(3.0, k); }
	static double rho(int k) { return k == 0 ? 0.0 : std::exp(-1.0 / tau(k)); }

	void step() {
		for (int k = 0; k < NUM_POLES; k++) {
			double r = rho(k);
			state[k] = r * state[k] + std::sqrt(1.0 - r * r) * rng.normal();
		}
	}
	void clear() {
		for (int k = 0; k < NUM_POLES; k++)
			state[k] = 0.0;
	}
	// Start each state at a random value from its long-run spread, so the
	// ensemble is "already playing" instead of starting perfectly tight.
	void randomize() {
		for (int k = 0; k < NUM_POLES; k++)
			state[k] = rng.normal();
	}
	double mix(const double* w) const {
		double x = 0.0;
		for (int k = 0; k < NUM_POLES; k++)
			x += w[k] * state[k];
		return x;
	}
};

// Mixing weights that turn the pole bank into unit-variance 1/f^beta noise.
// Each pole's variance share is proportional to tau^(beta - 1).
inline void betaWeights(double beta, double* w) {
	double sum = 0.0;
	for (int k = 0; k < NUM_POLES; k++) {
		double v = std::pow(PoleBank::tau(k), beta - 1.0);
		w[k] = v;
		sum += v;
	}
	for (int k = 0; k < NUM_POLES; k++)
		w[k] = std::sqrt(w[k] / sum);
}

// A player's (or the band's) drift as a sequence indexed by step number, so
// step n always gets the same value whatever order things are asked for in.
// That keeps seeds repeatable and lets followers look up the leader's hits.
struct Stream {
	PoleBank bank;
	int64_t index = 0;         // step number the bank's state belongs to
	double ring[16] = {};      // recent unit-variance values, by step number

	void restart(int64_t fromStep) {
		bank.clear();
		index = fromStep - 1;
		for (int k = 0; k < 16; k++)
			ring[k] = 0.0;
	}
	// Unit drift for step n. While frozen the state stops moving.
	double at(int64_t n, const double* w, bool frozen) {
		while (index < n) {
			if (!frozen)
				bank.step();
			index++;
			ring[index & 15] = std::min(std::max(bank.mix(w), -4.0), 4.0);
		}
		if (n <= index - 16)
			n = index - 15;
		return ring[n & 15];
	}
};

struct Engine {
	enum OutputMode { TRIGGER, GATE };

	// Settings, written by the module
	double laneSpread[NUM_PLAYERS]; // seconds, from each player's Spread knob
	double lean[NUM_PLAYERS];       // seconds, negative = early
	double masterSpread = 0.0;      // seconds, added to every lane
	double beta = 1.0;              // Feel, 0..2
	double togetherness = 0.3;      // 0..1
	bool leaderMode = false;
	bool reactToPrevious = true;    // leader mode: follow the leader's last hit
	double swing = 0.5;             // 0.5 = straight, 0.75 = full swing
	bool frozen = false;
	int seed = 0;                   // 0 = different every time, 1-32 = repeatable
	int outputMode = TRIGGER;       // an OutputMode
	double triggerLength = 0.005;   // seconds

	// Outputs, read by the module every sample
	bool out[NUM_PLAYERS] = {};
	bool fired[NUM_PLAYERS] = {};   // true for one sample when a hit happens
	double offsetMs[NUM_PLAYERS] = {}; // offset of the last hit (drift + lean), ms

	// Internal state
	Stream own[NUM_PLAYERS];
	Stream shared;
	double weights[NUM_POLES];
	double weightsBeta = -1.0;
	double now = 0.0;          // seconds since start
	double lastTick = -1.0;    // time of the last clock tick
	double period = 0.0;       // smoothed clock period, 0 = unknown
	int64_t tick = 0;          // number of clock ticks so far (index of the last one)
	int64_t swingOrigin = 1;   // step index that counts as a downbeat
	bool seededStart = false;
	int64_t nextStep[NUM_PLAYERS];
	double nextOffset[NUM_PLAYERS];  // drift + lean for the next hit, seconds
	double nextDrift[NUM_PLAYERS];   // drift part only
	double lastHit[NUM_PLAYERS];
	double gateEnd[NUM_PLAYERS];

	Engine() {
		uint64_t base = 0x1234567ULL;
		for (int i = 0; i < NUM_PLAYERS; i++) {
			laneSpread[i] = 0.02;
			lean[i] = 0.0;
			own[i].bank.rng.setSeed(base + i);
			nextStep[i] = 1;
			nextOffset[i] = 0.0;
			nextDrift[i] = 0.0;
			lastHit[i] = -1e9;
			gateEnd[i] = -1.0;
		}
		shared.bank.rng.setSeed(base + 99);
		betaWeights(beta, weights);
		weightsBeta = beta;
	}

	// Give every random stream a fresh starting point. seed 0 = random.
	void reseed(uint64_t s) {
		for (int i = 0; i < NUM_PLAYERS; i++)
			own[i].bank.rng.setSeed(s * 1000003ULL + i);
		shared.bank.rng.setSeed(s * 1000003ULL + 99);
	}

	void randomizeStates() {
		for (int i = 0; i < NUM_PLAYERS; i++)
			own[i].bank.randomize();
		shared.bank.randomize();
	}

	// Pull everyone's drift back to zero (it then grows again). Players keep
	// their place in the count, so no hit is gained or lost. With a seed set,
	// the drift that follows is the same every time.
	void reset() {
		// The tick that goes with this reset is the downbeat. A reset usually
		// arrives together with, or just after, a clock tick.
		bool justTicked = lastTick >= 0.0 && period > 0.0 && now - lastTick < 0.25 * period;
		restartDrift(justTicked ? tick : tick + 1);
	}

	// Drift starts again from zero at step 'from' (and from the seed, if set)
	void restartDrift(int64_t from) {
		if (seed > 0)
			reseed((uint64_t) seed);
		for (int i = 0; i < NUM_PLAYERS; i++)
			own[i].restart(from);
		shared.restart(from);
		swingOrigin = from;
		// Re-plan hits from that step on; hits already owed keep their timing
		for (int i = 0; i < NUM_PLAYERS; i++)
			if (nextStep[i] >= from)
				drawNext(i);
	}

	double effectiveSpread(int i) const {
		return std::min(std::max(laneSpread[i] + masterSpread, 0.0), MAX_SPREAD);
	}

	// Work out player i's drift (seconds) for its next step
	void drawNext(int i) {
		if (weightsBeta != beta) {
			betaWeights(beta, weights);
			weightsBeta = beta;
		}
		int64_t n = nextStep[i];
		double g = std::min(std::max(togetherness, 0.0), 1.0);
		double u = own[i].at(n, weights, frozen);
		double s = effectiveSpread(i);
		double d;
		if (leaderMode && i > 0) {
			// The leader's drift on its previous step (or this one)
			int64_t m = reactToPrevious ? n - 1 : n;
			double L = effectiveSpread(0) * own[0].at(m, weights, frozen);
			d = std::sqrt(1.0 - g) * s * u + std::sqrt(g) * L;
		}
		else if (leaderMode) {
			d = s * u;
		}
		else {
			double b = shared.at(n, weights, frozen);
			d = s * (std::sqrt(1.0 - g) * u + std::sqrt(g) * b);
		}
		nextDrift[i] = d;
		nextOffset[i] = d + lean[i];
	}

	double gridTime(int64_t n) const { return lastTick + double(n - tick) * period; }

	void clockTick() {
		if (lastTick >= 0.0) {
			double p = now - lastTick;
			bool restart = period > 0.0 && p > 2.5 * period;
			// After a stop, keep the old tempo until the next tick shows the new one
			if (!restart && p > 0.001) {
				if (period <= 0.0 || std::fabs(p - period) > 0.15 * period)
					period = p; // tempo jump: follow at once
				else
					period += 0.3 * (p - period); // steady: smooth out jitter
			}
		}
		lastTick = now;
		tick++;
		if (!seededStart) {
			// Very first tick: it's the downbeat, and a fixed seed starts the
			// same way every time
			seededStart = true;
			if (seed > 0) {
				restartDrift(tick);
			}
			else {
				swingOrigin = tick;
				for (int i = 0; i < NUM_PLAYERS; i++)
					drawNext(i);
			}
		}
	}

	void hit(int i) {
		lastHit[i] = now;
		fired[i] = true;
		offsetMs[i] = nextOffset[i] * 1000.0;
		double len = outputMode == GATE && period > 0.0 ? 0.5 * period : triggerLength;
		gateEnd[i] = now + len;
		nextStep[i]++;
		drawNext(i);
	}

	// Is it time for player i to play its next step?
	bool due(int i) const {
		int64_t n = nextStep[i];
		if (n > tick + 1)
			return false; // never more than one step ahead of the real clock
		if (period <= 0.0)
			return n <= tick; // tempo unknown: play exactly on the tick

		bool offBeat = ((n - swingOrigin) % 2 + 2) % 2 == 1;
		double swingDelay = offBeat ? (swing - 0.5) * 2.0 * period : 0.0;
		// Limits: up to 0.9 of a step early, up to 4 steps late
		double off = std::min(std::max(nextOffset[i], -0.9 * period), 4.0 * period);
		double shift = swingDelay + off;
		if (n == tick + 1 && shift >= 0.0)
			return false; // a late hit for a tick that hasn't arrived: wait for it
		double t = gridTime(n) + shift;
		// Keep at least a quarter step between two hits of one player
		t = std::max(t, lastHit[i] + 0.25 * period);
		return now >= t;
	}

	// Call once per sample with the clock and reset edges for this sample.
	void process(double dt, bool clockEdge, bool resetEdge) {
		for (int i = 0; i < NUM_PLAYERS; i++)
			fired[i] = false;
		if (resetEdge)
			reset();
		if (clockEdge)
			clockTick();

		for (int i = 0; i < NUM_PLAYERS; i++)
			if (due(i))
				hit(i);

		for (int i = 0; i < NUM_PLAYERS; i++)
			out[i] = now < gateEnd[i];

		now += dt;
	}
};

} // namespace drift
