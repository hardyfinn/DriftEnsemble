// Runs the disting NT plug-in on a computer with a fake host, feeding a steady
// 16th clock at 130 BPM, and prints each player's hits like test/engine_test.
//   c++ -std=c++11 -I<api>/include test/host_test.cpp -o host_test
//   ./host_test [beta] [spreadMs] [togetherness%] [steps] [shape] [midi]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../DriftEnsembleNT.cpp"

const _NT_globals NT_globals = { 48000, 128, nullptr, 0, 0, 0 };
uint32_t NT_getCpuCycleCount(void) { return 12345; }
int NT_floatToString(char* b, float v, int dp) { return sprintf(b, "%.*f", dp, v); }
void NT_drawText(int, int, const char*, int, _NT_textAlignment, _NT_textSize) {}
void NT_drawShapeI(_NT_shape, int, int, int, int, int) {}
void NT_drawShapeF(_NT_shape, float, float, float, float, float) {}

int main(int argc, char** argv)
{
	float beta = argc > 1 ? atof(argv[1]) : 1.f;
	float spreadMs = argc > 2 ? atof(argv[2]) : 20.f;
	int tog = argc > 3 ? atoi(argv[3]) : 30;
	int steps = argc > 4 ? atoi(argv[4]) : 8192;
	int shape = argc > 5 ? atoi(argv[5]) : 0;
	bool midi = argc > 6 && !strcmp(argv[6], "midi");

	_NT_algorithmRequirements req;
	calculateRequirements(req, nullptr);
	std::vector<uint8_t> sram(req.sram + 64);
	_NT_algorithmMemoryPtrs ptrs = { sram.data(), nullptr, nullptr, nullptr };
	_NT_algorithm* alg = construct(ptrs, req, nullptr);
	std::vector<int16_t> v(req.numParameters);
	for (uint32_t p = 0; p < req.numParameters; p++) v[p] = parameters[p].def;
	for (int i = 0; i < NUM_PLAYERS; i++) {
		v[kParamSpread1 + i] = (int16_t)(spreadMs * 10);
		v[kParamFeel1 + i] = (int16_t)(beta * 100);
		v[kParamErr1 + 2 * i] = 21 + i;       // error outs on aux busses
		v[kParamOut1 + 2 * i + 1] = 1;        // replace mode
		v[kParamErr1 + 2 * i + 1] = 1;
	}
	v[kParamTogether] = tog;
	v[kParamShape] = shape;
	v[kParamSource] = midi ? 1 : 0;
	alg->v = v.data();
	for (uint32_t p = 0; p < req.numParameters; p++) parameterChanged(alg, p);


	const int block = 24;
	const long spt = (long)(60.0 / 130.0 / 4.0 * 48000 + 0.5);
	const long total = spt * (steps + 2);
	std::vector<float> bus(kNT_lastBus * block);
	bool prev[NUM_PLAYERS] = {};
	long clock = 0, s = 0, highSamples = 0;
	while (s < total) {
		std::fill(bus.begin(), bus.end(), 0.f);
		for (int n = 0; n < block; n++) {
			long t = s + n;
			bus[0 * block + n] = (t % spt) < 48 ? 5.f : 0.f; // clock on input 1
		}
		if (midi) {
			// MIDI clock (24 ppqn) arrives between blocks, as on the real host
			if (s == 0) midiRealtime(alg, 0xFA);
			for (int n = 0; n < block; n++)
				if (((s + n) * 6) % spt < 6) midiRealtime(alg, 0xF8);
			std::fill(bus.begin(), bus.begin() + block, 0.f); // no CV clock
		}
		step(alg, bus.data(), block / 4);
		for (int n = 0; n < block; n++) {
			long t = s + n;
			if (t % spt == 0) clock++;
			for (int i = 0; i < NUM_PLAYERS; i++) {
				float g = bus[(12 + i) * block + n];
				if (i == 0 && g > 1.f) highSamples++;
				bool h = g > 1.f;
				if (h && !prev[i]) {
					float e = bus[(20 + i) * block + n];
					printf("%d %.6f %.4f %ld\n", i, t / 48000.0, e * 10.f, clock);
				}
				prev[i] = h;
			}
		}
		s += block;
	}
	fprintf(stderr, "player 1 average gate length: %.2f ms (step = %.1f ms)\n",
		highSamples / 48.0 / (double)steps, spt / 48.0);
}
