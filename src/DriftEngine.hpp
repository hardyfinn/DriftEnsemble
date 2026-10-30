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
// "Togetherness" mixes in a set of states shared by all six players, so the
// band leans into the same push while each player keeps their own wobble.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace drift {

static const int NUM_PLAYERS = 6;
// Pole 0 is white (no memory). Poles 1..7 remember 3, 9, ... 2187 steps.
static const int NUM_POLES = 8;

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

// A bank of unit-variance AR(1) states, one per time scale.
struct PoleBank {
	double state[NUM_POLES] = {};

	static double tau(int k) { return k == 0 ? 0.5 : std::pow(3.0, k); }
	static double rho(int k) { return k == 0 ? 0.0 : std::exp(-1.0 / tau(k)); }

	void step(Rng& rng) {
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
	void randomize(Rng& rng) {
		for (int k = 0; k < NUM_POLES; k++)
			state[k] = rng.normal();
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

struct PlayerSettings {
	double spread = 0.02; // standard deviation of timing offset, seconds
	double beta = 1.0;    // noise colour, 0..2
};

struct Engine {
	enum OutputMode { TRIGGER, GATE };

	// Settings, written by the module every block
	PlayerSettings players[NUM_PLAYERS];
	double togetherness = 0.3; // 0..1
	int outputMode = TRIGGER; // an OutputMode
	double triggerLength = 0.005; // seconds

	// Outputs, read by the module every sample
	bool out[NUM_PLAYERS] = {};
	double offsetMs[NUM_PLAYERS] = {}; // held offset of the last hit, ms
	bool fired[NUM_PLAYERS] = {};      // true for one sample when a hit happens

	// Internal state
	Rng rng;
	PoleBank own[NUM_PLAYERS];
	PoleBank shared;
	double now = 0.0;          // seconds since start
	double lastTick = -1.0;    // time of last clock tick
	double period = 0.0;       // smoothed clock period, 0 = unknown
	int64_t tick = 0;          // index of last clock tick
	bool running = false;
	int64_t nextStep[NUM_PLAYERS] = {};
	double nextOffset[NUM_PLAYERS] = {};
	double lastHit[NUM_PLAYERS] = {};
	double gateEnd[NUM_PLAYERS] = {};
	bool resetPending = false;

	Engine() {
		for (int i = 0; i < NUM_PLAYERS; i++)
			lastHit[i] = -1e9;
	}

	// Pull everyone back onto the grid. Drift then grows again from zero.
	void reset() {
		for (int i = 0; i < NUM_PLAYERS; i++)
			own[i].clear();
		shared.clear();
		resetPending = true;
	}

	void randomizeStates() {
		for (int i = 0; i < NUM_PLAYERS; i++)
			own[i].randomize(rng);
		shared.randomize(rng);
	}

	double gridTime(int64_t n) const { return lastTick + double(n - tick) * period; }

	// Advance the drift states by one step and return this player's offset
	// in seconds for its next hit.
	double drawOffset(int i) {
		own[i].step(rng);
		double w[NUM_POLES];
		betaWeights(players[i].beta, w);
		double a = 0.0, b = 0.0;
		for (int k = 0; k < NUM_POLES; k++) {
			a += w[k] * own[i].state[k];
			b += w[k] * shared.state[k];
		}
		double g = std::min(std::max(togetherness, 0.0), 1.0);
		double unit = std::sqrt(1.0 - g) * a + std::sqrt(g) * b;
		// Keep extreme draws musical: never more than 4 standard deviations
		unit = std::min(std::max(unit, -4.0), 4.0);
		return players[i].spread * unit;
	}

	// Called on each rising edge of the clock input.
	void clockTick() {
		if (lastTick >= 0.0) {
			double p = now - lastTick;
			if (p > 0.001) {
				if (period <= 0.0 || std::fabs(p - period) > 0.15 * period)
					period = p; // tempo jump: follow at once
				else
					period += 0.3 * (p - period); // steady: smooth out jitter
			}
		}
		lastTick = now;
		tick++;

		// The shared "band" drift moves once per step
		shared.step(rng);

		if (period <= 0.0)
			return; // need two ticks to know the tempo

		if (!running || resetPending) {
			running = true;
			resetPending = false;
			// This tick is the downbeat: everyone hits now, together
			for (int i = 0; i < NUM_PLAYERS; i++) {
				hit(i, 0.0);
				nextStep[i] = tick + 1;
				nextOffset[i] = drawOffset(i);
			}
		}
	}

	void hit(int i, double offset) {
		lastHit[i] = now;
		fired[i] = true;
		offsetMs[i] = offset * 1000.0;
		gateEnd[i] = now + (outputMode == GATE ? 0.5 * period : triggerLength);
	}

	// Call once per sample with the clock and reset edges for this sample.
	void process(double dt, bool clockEdge, bool resetEdge) {
		for (int i = 0; i < NUM_PLAYERS; i++)
			fired[i] = false;
		if (resetEdge)
			reset();
		if (clockEdge)
			clockTick();

		// Clock stopped? Go quiet and wait for it to come back.
		if (running && period > 0.0 && now - lastTick > 2.5 * period)
			running = false;

		if (running) {
			for (int i = 0; i < NUM_PLAYERS; i++) {
				// A player can run ahead or fall behind the clock, but never by
				// more than 4 steps, so they stay part of the band.
				double maxLead = 4.0 * period;
				double off = std::min(std::max(nextOffset[i], -maxLead), maxLead);
				double t = gridTime(nextStep[i]) + off;
				// Keep at least a quarter step between two hits of one player
				t = std::max(t, lastHit[i] + 0.25 * period);
				if (now >= t) {
					hit(i, off);
					nextStep[i]++;
					nextOffset[i] = drawOffset(i);
				}
			}
		}

		for (int i = 0; i < NUM_PLAYERS; i++)
			out[i] = now < gateEnd[i];

		now += dt;
	}
};

} // namespace drift
