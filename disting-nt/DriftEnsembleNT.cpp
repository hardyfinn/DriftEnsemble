// Drift Ensemble for the Expert Sleepers disting NT
//
// One steady 16th-note clock in, six "players" out. Each player drifts
// against the clock the way human musicians do, following the 1/f timing model
// of Hennig et al. 2011 (PLoS ONE 6(10): e26457). Same engine as the VCV Rack
// module, rewritten in single-precision float and without the maths library,
// so it only needs the disting NT plug-in API.
//
// Parameters
//   Ensemble page : Clock source, Togetherness, Output shape
//   Spread page   : Spread 1-6 (ms, standard deviation of the drift)
//   Feel page     : Feel 1-6 (noise colour beta, 0 = jitter, 1 = human, 2 = wander)
//   Routing page  : Clock in, Reset in, Player 1-6 outs, Error 1-6 outs
//
// Clock source "CV" listens to the Clock in bus. "MIDI" follows MIDI clock
// (e.g. from Live over USB): every 6 MIDI clocks is one 16th, Start pulls
// everyone back to the grid, Stop silences the players.
// Every parameter can be CV-mapped with the disting NT's own mapping system,
// so the module needs no dedicated CV inputs.

#include <new>
#include <stddef.h>
#include <distingnt/api.h>

static const int NUM_PLAYERS = 6;
static const int NUM_POLES = 8;

// ---------------------------------------------------------------------------
// Small maths helpers (no libm needed)

// e^x for the modest range used here, accurate to about 1e-6
static float fastExp(float x)
{
	if (x < -80.f) return 0.f;
	if (x > 80.f) x = 80.f;
	float t = x * 1.4426950409f; // x / ln 2
	int n = (int)(t < 0.f ? t - 1.f : t);
	float f = t - (float)n; // 0..1
	// 2^f, polynomial
	float p = 1.f + f * (0.6931471806f + f * (0.2402265070f + f * (0.0555041087f + f * (0.0096181291f + f * 0.0013333558f))));
	union { float fl; int32_t i; } u;
	u.i = (n + 127) << 23; // 2^n
	return p * u.fl;
}

static inline float fsqrt(float x)
{
#ifdef __arm__
	float r;
	__asm__("vsqrt.f32 %0, %1" : "=t"(r) : "t"(x)); // hardware square root
	return r;
#else
	return __builtin_sqrtf(x); // for testing on a computer
#endif
}

static inline float fclamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// xoroshiro128+ random numbers
struct Rng {
	uint64_t s[2];
	void seed(uint64_t seed)
	{
		for (int i = 0; i < 2; i++) {
			seed += 0x9E3779B97F4A7C15ULL;
			uint64_t z = seed;
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
			s[i] = z ^ (z >> 31);
		}
	}
	static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
	uint64_t next()
	{
		uint64_t s0 = s[0], s1 = s[1];
		uint64_t r = s0 + s1;
		s1 ^= s0;
		s[0] = rotl(s0, 24) ^ s1 ^ (s1 << 16);
		s[1] = rotl(s1, 37);
		return r;
	}
	float uniform() { return (float)(next() >> 40) * (1.f / 16777216.f); }
	// Gaussian (mean 0, SD 1) from the sum of 12 uniforms: plenty for timing
	float normal()
	{
		float s = 0.f;
		for (int i = 0; i < 12; i++) s += uniform();
		return s - 6.f;
	}
};

// Each pole is a smooth random signal remembering about tau steps.
// Pole 0 has no memory; the others remember 3, 9, 27 ... 2187 steps.
static const float kRho[NUM_POLES] = { 0.f, 0.716531311f, 0.894839317f, 0.963640444f, 0.987730216f, 0.995893230f, 0.998629198f, 0.999542857f };
static const float kKick[NUM_POLES] = { 1.f, 0.697554930f, 0.446388393f, 0.267202347f, 0.156169843f, 0.090535492f, 0.052342376f, 0.030233702f };
static const float kLnTau[NUM_POLES] = { -0.693147181f, 1.098612289f, 2.197224577f, 3.295836866f, 4.394449155f, 5.493061443f, 6.591673732f, 7.690286021f };

struct PoleBank {
	float state[NUM_POLES];
	void clear() { for (int k = 0; k < NUM_POLES; k++) state[k] = 0.f; }
	void randomize(Rng& rng) { for (int k = 0; k < NUM_POLES; k++) state[k] = rng.normal(); }
	void step(Rng& rng) { for (int k = 0; k < NUM_POLES; k++) state[k] = kRho[k] * state[k] + kKick[k] * rng.normal(); }
};

// Mixing weights for 1/f^beta noise: pole variance share ~ tau^(beta-1)
static void betaWeights(float beta, float* w)
{
	float sum = 0.f;
	for (int k = 0; k < NUM_POLES; k++) {
		w[k] = fastExp((beta - 1.f) * kLnTau[k]);
		sum += w[k];
	}
	for (int k = 0; k < NUM_POLES; k++)
		w[k] = fsqrt(w[k] / sum);
}

// ---------------------------------------------------------------------------
// Parameters

enum {
	kParamSource,
	kParamTogether,
	kParamShape,
	kParamSpread1,
	kParamFeel1 = kParamSpread1 + NUM_PLAYERS,
	kParamClockIn = kParamFeel1 + NUM_PLAYERS,
	kParamResetIn,
	kParamOut1,  // each output is followed by its mode parameter
	kParamErr1 = kParamOut1 + 2 * NUM_PLAYERS,
	kNumParams = kParamErr1 + 2 * NUM_PLAYERS,
};

static const char* const shapeStrings[] = { "Trigger", "Gate", NULL };
static const char* const sourceStrings[] = { "CV", "MIDI", NULL };

#define SPREAD(n) { .name = "Spread " #n, .min = 0, .max = 800, .def = 200, .unit = kNT_unitMs, .scaling = kNT_scaling10, .enumStrings = NULL },
#define FEEL(n) { .name = "Feel " #n, .min = 0, .max = 200, .def = 100, .unit = kNT_unitHasStrings, .scaling = 0, .enumStrings = NULL },

static const _NT_parameter parameters[] = {
	{ .name = "Clock source", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = sourceStrings },
	{ .name = "Togetherness", .min = 0, .max = 100, .def = 30, .unit = kNT_unitPercent, .scaling = 0, .enumStrings = NULL },
	{ .name = "Output shape", .min = 0, .max = 1, .def = 0, .unit = kNT_unitEnum, .scaling = 0, .enumStrings = shapeStrings },
	SPREAD(1) SPREAD(2) SPREAD(3) SPREAD(4) SPREAD(5) SPREAD(6)
	FEEL(1) FEEL(2) FEEL(3) FEEL(4) FEEL(5) FEEL(6)
	NT_PARAMETER_CV_INPUT("Clock in", 1, 1)
	NT_PARAMETER_CV_INPUT("Reset in", 0, 2)
	// Player clocks default to outputs 1-6 (busses 13-18)
	NT_PARAMETER_CV_OUTPUT_WITH_MODE("Player 1 out", 0, 13)
	NT_PARAMETER_CV_OUTPUT_WITH_MODE("Player 2 out", 0, 14)
	NT_PARAMETER_CV_OUTPUT_WITH_MODE("Player 3 out", 0, 15)
	NT_PARAMETER_CV_OUTPUT_WITH_MODE("Player 4 out", 0, 16)
	NT_PARAMETER_CV_OUTPUT_WITH_MODE("Player 5 out", 0, 17)
	NT_PARAMETER_CV_OUTPUT_WITH_MODE("Player 6 out", 0, 18)
	// Error outputs are off by default (there are only 8 physical outputs)
	NT_PARAMETER_CV_OUTPUT_WITH_MODE("Error 1 out", 0, 0)
	NT_PARAMETER_CV_OUTPUT_WITH_MODE("Error 2 out", 0, 0)
	NT_PARAMETER_CV_OUTPUT_WITH_MODE("Error 3 out", 0, 0)
	NT_PARAMETER_CV_OUTPUT_WITH_MODE("Error 4 out", 0, 0)
	NT_PARAMETER_CV_OUTPUT_WITH_MODE("Error 5 out", 0, 0)
	NT_PARAMETER_CV_OUTPUT_WITH_MODE("Error 6 out", 0, 0)
};
static_assert(ARRAY_SIZE(parameters) == kNumParams, "parameter count");

static const uint8_t pageEnsemble[] = { kParamSource, kParamTogether, kParamShape };
static const uint8_t pageSpread[] = { kParamSpread1, kParamSpread1 + 1, kParamSpread1 + 2, kParamSpread1 + 3, kParamSpread1 + 4, kParamSpread1 + 5 };
static const uint8_t pageFeel[] = { kParamFeel1, kParamFeel1 + 1, kParamFeel1 + 2, kParamFeel1 + 3, kParamFeel1 + 4, kParamFeel1 + 5 };
static const uint8_t pageRouting[] = {
	kParamClockIn, kParamResetIn,
	kParamOut1 + 0, kParamOut1 + 1, kParamOut1 + 2, kParamOut1 + 3, kParamOut1 + 4, kParamOut1 + 5,
	kParamOut1 + 6, kParamOut1 + 7, kParamOut1 + 8, kParamOut1 + 9, kParamOut1 + 10, kParamOut1 + 11,
	kParamErr1 + 0, kParamErr1 + 1, kParamErr1 + 2, kParamErr1 + 3, kParamErr1 + 4, kParamErr1 + 5,
	kParamErr1 + 6, kParamErr1 + 7, kParamErr1 + 8, kParamErr1 + 9, kParamErr1 + 10, kParamErr1 + 11,
};

static const _NT_parameterPage pages[] = {
	{ .name = "Ensemble", .numParams = ARRAY_SIZE(pageEnsemble), .group = 0, .unused = {}, .params = pageEnsemble },
	{ .name = "Spread", .numParams = ARRAY_SIZE(pageSpread), .group = 1, .unused = {}, .params = pageSpread },
	{ .name = "Feel", .numParams = ARRAY_SIZE(pageFeel), .group = 1, .unused = {}, .params = pageFeel },
	{ .name = "Routing", .numParams = ARRAY_SIZE(pageRouting), .group = 0, .unused = {}, .params = pageRouting },
};

static const _NT_parameterPages parameterPages = {
	.numPages = ARRAY_SIZE(pages),
	.pages = pages,
};

// ---------------------------------------------------------------------------
// The algorithm

static const float GATE_VOLTS = 5.f;
static const float TRIGGER_SECONDS = 0.005f;

struct DriftEnsemble : _NT_algorithm {
	Rng rng;
	PoleBank own[NUM_PLAYERS];
	PoleBank shared;
	float weights[NUM_PLAYERS][NUM_POLES];
	float spread[NUM_PLAYERS]; // seconds
	float togetherness;

	// Time is counted in samples (int64, so it never runs out of precision)
	int64_t now;
	int64_t lastTick;     // sample of the last clock tick, -1 = none yet
	float period;         // smoothed clock period in samples, 0 = unknown
	int64_t tick;         // count of clock ticks
	bool running;
	bool resetPending;
	bool clockHigh, resetHigh;

	// MIDI clock: set between audio blocks, acted on at the start of the next
	volatile uint8_t midiTicksPending;
	volatile bool midiStartPending, midiStopPending;
	uint8_t midiCount; // MIDI clocks since the last 16th

	int64_t nextStep[NUM_PLAYERS];
	float nextOffset[NUM_PLAYERS];  // samples
	int64_t lastHit[NUM_PLAYERS];
	int64_t gateEnd[NUM_PLAYERS];
	float offsetMs[NUM_PLAYERS];    // held error of the last hit
	int64_t flashUntil[NUM_PLAYERS];
};

static void calculateRequirements(_NT_algorithmRequirements& req, const int32_t* specifications)
{
	req.numParameters = ARRAY_SIZE(parameters);
	req.sram = sizeof(DriftEnsemble);
	req.dram = 0;
	req.dtc = 0;
	req.itc = 0;
}

static void resetDrift(DriftEnsemble* a)
{
	for (int i = 0; i < NUM_PLAYERS; i++)
		a->own[i].clear();
	a->shared.clear();
	a->resetPending = true;
}

static _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req, const int32_t* specifications)
{
	DriftEnsemble* a = new (ptrs.sram) DriftEnsemble();
	a->parameters = parameters;
	a->parameterPages = &parameterPages;
	a->rng.seed(0x5EED0000ULL ^ NT_getCpuCycleCount());
	for (int i = 0; i < NUM_PLAYERS; i++) {
		a->own[i].randomize(a->rng);
		a->spread[i] = 0.02f;
		betaWeights(1.f, a->weights[i]);
		a->nextStep[i] = 0;
		a->nextOffset[i] = 0.f;
		a->lastHit[i] = -(1LL << 40);
		a->gateEnd[i] = 0;
		a->offsetMs[i] = 0.f;
		a->flashUntil[i] = 0;
	}
	a->shared.randomize(a->rng);
	a->togetherness = 0.3f;
	a->now = 0;
	a->lastTick = -1;
	a->period = 0.f;
	a->tick = 0;
	a->running = false;
	a->resetPending = false;
	a->clockHigh = a->resetHigh = false;
	a->midiTicksPending = 0;
	a->midiStartPending = a->midiStopPending = false;
	a->midiCount = 0;
	return a;
}

static void parameterChanged(_NT_algorithm* self, int p)
{
	DriftEnsemble* a = (DriftEnsemble*)self;
	if (p == kParamTogether)
		a->togetherness = a->v[kParamTogether] * 0.01f;
	else if (p >= kParamSpread1 && p < kParamSpread1 + NUM_PLAYERS)
		a->spread[p - kParamSpread1] = a->v[p] * 0.0001f; // tenths of ms -> seconds
	else if (p >= kParamFeel1 && p < kParamFeel1 + NUM_PLAYERS)
		betaWeights(a->v[p] * 0.01f, a->weights[p - kParamFeel1]);
}

static int parameterString(_NT_algorithm* self, int p, int v, char* buff)
{
	if (p < kParamFeel1 || p >= kParamFeel1 + NUM_PLAYERS)
		return 0;
	int len = NT_floatToString(buff, v * 0.01f, 2);
	const char* word = v < 40 ? " jitter" : v < 80 ? " nervous" : v < 130 ? " human" : v < 170 ? " loose" : " wander";
	while (*word) buff[len++] = *word++;
	buff[len] = 0;
	return len;
}

// Advance this player's drift and return its next offset, in samples
static float drawOffset(DriftEnsemble* a, int i)
{
	a->own[i].step(a->rng);
	const float* w = a->weights[i];
	float x = 0.f, y = 0.f;
	for (int k = 0; k < NUM_POLES; k++) {
		x += w[k] * a->own[i].state[k];
		y += w[k] * a->shared.state[k];
	}
	float g = fclamp(a->togetherness, 0.f, 1.f);
	float unit = fclamp(fsqrt(1.f - g) * x + fsqrt(g) * y, -4.f, 4.f);
	return a->spread[i] * unit * (float)NT_globals.sampleRate;
}

static void hit(DriftEnsemble* a, int i, float offsetSamples)
{
	const float sr = (float)NT_globals.sampleRate;
	a->lastHit[i] = a->now;
	a->offsetMs[i] = offsetSamples * 1000.f / sr;
	int32_t len = a->v[kParamShape] == 1 ? (int32_t)(0.5f * a->period) : (int32_t)(TRIGGER_SECONDS * sr);
	a->gateEnd[i] = a->now + (len < 1 ? 1 : len);
	a->flashUntil[i] = a->now + (int32_t)(0.04f * sr);
}

static void clockTick(DriftEnsemble* a)
{
	if (a->lastTick >= 0) {
		float p = (float)(int32_t)(a->now - a->lastTick);
		if (p > 8.f) {
			float d = p - a->period;
			if (a->period <= 0.f || (d < 0.f ? -d : d) > 0.15f * a->period)
				a->period = p;            // tempo jump: follow at once
			else
				a->period += 0.3f * d;    // steady: smooth out jitter
		}
	}
	a->lastTick = a->now;
	a->tick++;
	a->shared.step(a->rng); // the band's shared drift moves once per step

	if (a->period <= 0.f)
		return; // need two ticks to know the tempo

	if (!a->running || a->resetPending) {
		// Downbeat: everyone hits together, then drifts from there
		a->running = true;
		a->resetPending = false;
		for (int i = 0; i < NUM_PLAYERS; i++) {
			hit(a, i, 0.f);
			a->nextStep[i] = a->tick + 1;
			a->nextOffset[i] = drawOffset(a, i);
		}
	}
}

// MIDI realtime bytes: 0xF8 clock, 0xFA start, 0xFB continue, 0xFC stop
static void midiRealtime(_NT_algorithm* self, uint8_t byte)
{
	DriftEnsemble* a = (DriftEnsemble*)self;
	switch (byte) {
	case 0xF8:
		// 24 MIDI clocks per quarter note = 6 per 16th
		if (a->midiCount == 0 && a->midiTicksPending < 255)
			a->midiTicksPending++;
		a->midiCount = (a->midiCount + 1) % 6;
		break;
	case 0xFA:
		a->midiCount = 0;
		a->midiStartPending = true;
		break;
	case 0xFC:
		a->midiStopPending = true;
		break;
	}
}

static inline float* bus(float* busFrames, int b, int numFrames)
{
	return b > 0 ? busFrames + (b - 1) * numFrames : NULL;
}

static void step(_NT_algorithm* self, float* busFrames, int numFramesBy4)
{
	DriftEnsemble* a = (DriftEnsemble*)self;
	const int numFrames = numFramesBy4 * 4;
	const float* clockIn = bus(busFrames, a->v[kParamClockIn], numFrames);
	const float* resetIn = bus(busFrames, a->v[kParamResetIn], numFrames);
	float* out[NUM_PLAYERS];
	float* err[NUM_PLAYERS];
	bool outReplace[NUM_PLAYERS], errReplace[NUM_PLAYERS];
	for (int i = 0; i < NUM_PLAYERS; i++) {
		out[i] = bus(busFrames, a->v[kParamOut1 + 2 * i], numFrames);
		outReplace[i] = a->v[kParamOut1 + 2 * i + 1];
		err[i] = bus(busFrames, a->v[kParamErr1 + 2 * i], numFrames);
		errReplace[i] = a->v[kParamErr1 + 2 * i + 1];
	}

	const bool useMidi = a->v[kParamSource] == 1;
	bool midiTick = false;
	if (useMidi) {
		if (a->midiStopPending) {
			a->midiStopPending = false;
			a->running = false;
			a->lastTick = -1; // forget the tempo; relearn it after Start
			a->period = 0.f;
		}
		if (a->midiStartPending) {
			a->midiStartPending = false;
			resetDrift(a);
		}
		if (a->midiTicksPending) {
			a->midiTicksPending--;
			midiTick = true;
		}
	}

	for (int n = 0; n < numFrames; n++) {
		if (useMidi && n == 0 && midiTick)
			clockTick(a);
		// Edges (rising through 1 V)
		if (resetIn) {
			bool h = resetIn[n] > 1.f;
			if (h && !a->resetHigh)
				resetDrift(a);
			a->resetHigh = h;
		}
		if (clockIn && !useMidi) {
			bool h = clockIn[n] > 1.f;
			if (h && !a->clockHigh)
				clockTick(a);
			a->clockHigh = h;
		}

		// Clock stopped? Wait quietly for it to come back.
		if (a->running && a->period > 0.f && (float)(int32_t)(a->now - a->lastTick) > 2.5f * a->period)
			a->running = false;

		if (a->running) {
			for (int i = 0; i < NUM_PLAYERS; i++) {
				// Never more than 4 steps ahead or behind the band
				float maxLead = 4.f * a->period;
				float off = fclamp(a->nextOffset[i], -maxLead, maxLead);
				float t = (float)(int32_t)(a->lastTick - a->now) + (float)(int32_t)(a->nextStep[i] - a->tick) * a->period + off;
				// ...and at least a quarter step between two hits of one player
				int64_t sinceHit = a->now - a->lastHit[i];
				if (sinceHit > (1 << 30)) sinceHit = 1 << 30;
				float minT = 0.25f * a->period - (float)(int32_t)sinceHit;
				if (t < minT) t = minT;
				if (t <= 0.f) {
					hit(a, i, off);
					a->nextStep[i]++;
					a->nextOffset[i] = drawOffset(a, i);
				}
			}
		}

		for (int i = 0; i < NUM_PLAYERS; i++) {
			if (out[i]) {
				float g = a->now < a->gateEnd[i] ? GATE_VOLTS : 0.f;
				if (outReplace[i]) out[i][n] = g; else out[i][n] += g;
			}
			if (err[i]) {
				float e = fclamp(a->offsetMs[i] * 0.1f, -10.f, 10.f); // 1 V per 10 ms
				if (errReplace[i]) err[i][n] = e; else err[i][n] += e;
			}
		}
		a->now++;
	}
}

// Display: six lanes, the centre line is the clock, the dot is each player's
// last hit (left = early, right = late). Dots flash on each hit.
static bool draw(_NT_algorithm* self)
{
	DriftEnsemble* a = (DriftEnsemble*)self;
	const int cx = 140;
	const float pxPerMs = 1.5f;
	NT_drawText(0, 20, "DRIFT", 15);
	NT_drawText(0, 30, "ENSEMBLE", 15);
	NT_drawText(0, 46, "early", 6, kNT_textLeft, kNT_textTiny);
	NT_drawText(0, 54, "<  > late", 6, kNT_textLeft, kNT_textTiny);
	NT_drawShapeI(kNT_line, cx, 14, cx, 63, 5);
	for (int i = 0; i < NUM_PLAYERS; i++) {
		int y = 17 + i * 8;
		char num[2] = { (char)('1' + i), 0 };
		NT_drawText(60, y + 3, num, 8, kNT_textLeft, kNT_textTiny);
		NT_drawShapeI(kNT_line, 68, y, 252, y, 2);
		float x = cx + fclamp(a->offsetMs[i] * pxPerMs, -70.f, 110.f);
		bool lit = a->now < a->flashUntil[i];
		NT_drawShapeF(kNT_rectangle, x - 2.f, y - 2.f, x + 2.f, y + 2.f, lit ? 15.f : 7.f);
	}
	return false;
}

static const _NT_factory factory = {
	.guid = NT_MULTICHAR('H', 'r', 'D', 'e'),
	.name = "Drift Ensemble",
	.description = "One clock in, six human players out (1/f timing drift)",
	.numSpecifications = 0,
	.specifications = NULL,
	.calculateStaticRequirements = NULL,
	.initialise = NULL,
	.calculateRequirements = calculateRequirements,
	.construct = construct,
	.parameterChanged = parameterChanged,
	.step = step,
	.draw = draw,
	.midiRealtime = midiRealtime,
	.midiMessage = NULL,
	.tags = kNT_tagUtility,
	.hasCustomUi = NULL,
	.customUi = NULL,
	.setupUi = NULL,
	.serialise = NULL,
	.deserialise = NULL,
	.midiSysEx = NULL,
	.parameterUiPrefix = NULL,
	.parameterString = parameterString,
};

_NT_DRAM_SECTION
uintptr_t pluginEntry(_NT_selector selector, uint32_t data)
{
	switch (selector) {
	case kNT_selector_version:
		return kNT_apiVersionCurrent;
	case kNT_selector_numFactories:
		return 1;
	case kNT_selector_factoryInfo:
		return (uintptr_t)((data == 0) ? &factory : NULL);
	}
	return 0;
}
