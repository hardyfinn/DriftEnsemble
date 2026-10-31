// Ensemble.cpp
// The Drift Ensemble module: one steady clock in, six "players" out.
// All the timing maths lives in DriftEngine.hpp; this file connects it to
// knobs, jacks and lights.
#include "plugin.hpp"
#include "DriftEngine.hpp"

using drift::NUM_PLAYERS;

// Spread knob 0..1 maps to 0..50 ms on a curve that puts noon at 15 ms:
// ms = 50 * x^k with 0.5^k = 0.3. Double-click resets to 20 ms.
static const float MAX_SPREAD_MS = 50.f;
static const float SPREAD_CURVE = std::log(0.3f) / std::log(0.5f);
static const float SPREAD_DEFAULT = std::pow(20.f / MAX_SPREAD_MS, 1.f / SPREAD_CURVE);
static const float MAX_LEAN_MS = 25.f;
static const float MAX_MASTER_SPREAD_MS = 20.f;
static const int MAX_SEED = 32;

static float spreadKnobToMs(float x) {
	return MAX_SPREAD_MS * std::pow(clamp(x, 0.f, 1.f), SPREAD_CURVE);
}

struct SpreadQuantity : ParamQuantity {
	float getDisplayValue() override { return spreadKnobToMs(getValue()); }
	void setDisplayValue(float ms) override {
		setValue(std::pow(clamp(ms / MAX_SPREAD_MS, 0.f, 1.f), 1.f / SPREAD_CURVE));
	}
};

// Feel knob 0..1 shows the noise exponent beta, 0..2
struct FeelQuantity : ParamQuantity {
	float getDisplayValue() override { return getValue() * 2.f; }
	void setDisplayValue(float b) override { setValue(clamp(b / 2.f, 0.f, 1.f)); }
	std::string getDisplayValueString() override {
		float b = getDisplayValue();
		const char* word = b < 0.4f ? "jitter" : b < 0.8f ? "nervous" : b < 1.3f ? "human" : b < 1.7f ? "loose" : "wander";
		return string::f("%.2f (%s)", b, word);
	}
};

struct SeedQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		int s = (int) std::round(getValue());
		return s == 0 ? "off (new drift every time)" : string::f("%d", s);
	}
};

struct Ensemble : Module {
	enum ParamId {
		ENUMS(SPREAD_PARAM, NUM_PLAYERS),
		ENUMS(LEAN_PARAM, NUM_PLAYERS),
		TOGETHER_PARAM,
		LEADER_PARAM,
		SEED_PARAM,
		MASTER_SPREAD_PARAM,
		FEEL_PARAM,
		SWING_PARAM,
		FREEZE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CLOCK_INPUT,
		RESET_INPUT,
		FREEZE_INPUT,
		TOGETHER_INPUT,
		MASTER_SPREAD_INPUT,
		FEEL_INPUT,
		SWING_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		ENUMS(CLOCK_OUTPUT, NUM_PLAYERS),
		OUTPUTS_LEN
	};
	enum LightId {
		ENUMS(HIT_LIGHT, NUM_PLAYERS * 3), // RGB
		LEADER_LIGHT,
		FREEZE_LIGHT,
		LIGHTS_LEN
	};

	drift::Engine engine;
	dsp::SchmittTrigger clockTrigger, resetTrigger;
	dsp::ClockDivider settingsDivider;
	dsp::PulseGenerator lightPulse[NUM_PLAYERS];

	Ensemble() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int i = 0; i < NUM_PLAYERS; i++) {
			std::string p = string::f("Player %d ", i + 1);
			configParam<SpreadQuantity>(SPREAD_PARAM + i, 0.f, 1.f, SPREAD_DEFAULT, p + "spread", " ms");
			configParam(LEAN_PARAM + i, -MAX_LEAN_MS, MAX_LEAN_MS, 0.f, p + "lean (- early, + late)", " ms");
			configOutput(CLOCK_OUTPUT + i, p + "clock");
			configLight(HIT_LIGHT + 3 * i, p + "hit");
		}
		configParam(TOGETHER_PARAM, 0.f, 1.f, 0.3f, "Togetherness", "%", 0.f, 100.f);
		configSwitch(LEADER_PARAM, 0.f, 1.f, 0.f, "Togetherness mode", {"Band (shared feel)", "Leader (follow player 1)"});
		configParam<SeedQuantity>(SEED_PARAM, 0.f, (float) MAX_SEED, 0.f, "Seed");
		getParamQuantity(SEED_PARAM)->snapEnabled = true;
		configParam(MASTER_SPREAD_PARAM, -MAX_MASTER_SPREAD_MS, MAX_MASTER_SPREAD_MS, 0.f, "Master spread (added to every player)", " ms");
		configParam<FeelQuantity>(FEEL_PARAM, 0.f, 1.f, 0.5f, "Feel");
		configParam(SWING_PARAM, 0.f, 1.f, 0.f, "Swing", "%", 0.f, 25.f, 50.f);
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze", {"Off", "On"});
		configInput(CLOCK_INPUT, "Clock (16ths)");
		configInput(RESET_INPUT, "Reset (pull everyone back to the grid)");
		configInput(FREEZE_INPUT, "Freeze gate (high = frozen)");
		configInput(TOGETHER_INPUT, "Togetherness CV");
		configInput(MASTER_SPREAD_INPUT, "Master spread CV");
		configInput(FEEL_INPUT, "Feel CV");
		configInput(SWING_INPUT, "Swing CV");
		configLight(LEADER_LIGHT, "Leader mode");
		configLight(FREEZE_LIGHT, "Frozen");
		settingsDivider.setDivision(32);
		engine.reseed(random::u64());
		engine.randomizeStates();
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		engine.reactToPrevious = true;
		engine.outputMode = drift::Engine::TRIGGER;
		engine.reset();
	}

	void onRandomize(const RandomizeEvent& e) override {
		Module::onRandomize(e);
		engine.randomizeStates();
	}

	// Read knobs + CV. 10 V of CV sweeps the whole knob range.
	void updateSettings() {
		for (int i = 0; i < NUM_PLAYERS; i++) {
			engine.laneSpread[i] = spreadKnobToMs(params[SPREAD_PARAM + i].getValue()) / 1000.0;
			engine.lean[i] = params[LEAN_PARAM + i].getValue() / 1000.0;
		}
		float ms = params[MASTER_SPREAD_PARAM].getValue() + inputs[MASTER_SPREAD_INPUT].getVoltage() * (2.f * MAX_MASTER_SPREAD_MS / 10.f);
		engine.masterSpread = clamp(ms, -MAX_MASTER_SPREAD_MS, MAX_MASTER_SPREAD_MS) / 1000.0;
		engine.beta = 2.0 * clamp(params[FEEL_PARAM].getValue() + inputs[FEEL_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		engine.togetherness = clamp(params[TOGETHER_PARAM].getValue() + inputs[TOGETHER_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		engine.swing = 0.5 + 0.25 * clamp(params[SWING_PARAM].getValue() + inputs[SWING_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		engine.leaderMode = params[LEADER_PARAM].getValue() > 0.5f;
		engine.seed = (int) std::round(params[SEED_PARAM].getValue());
		engine.frozen = params[FREEZE_PARAM].getValue() > 0.5f || inputs[FREEZE_INPUT].getVoltage() >= 1.f;
	}

	void process(const ProcessArgs& args) override {
		if (settingsDivider.process())
			updateSettings();

		bool clockEdge = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		bool resetEdge = resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		engine.process(args.sampleTime, clockEdge, resetEdge);

		for (int i = 0; i < NUM_PLAYERS; i++) {
			outputs[CLOCK_OUTPUT + i].setVoltage(engine.out[i] ? 10.f : 0.f);
			if (engine.fired[i])
				lightPulse[i].trigger(0.03f);
			float b = lightPulse[i].process(args.sampleTime) ? 1.f : 0.f;
			// Yellow hit light; pink for the leader in Leader mode
			bool leader = engine.leaderMode && i == 0;
			lights[HIT_LIGHT + 3 * i + 0].setBrightnessSmooth(b, args.sampleTime);
			lights[HIT_LIGHT + 3 * i + 1].setBrightnessSmooth(b * (leader ? 0.15f : 0.8f), args.sampleTime);
			lights[HIT_LIGHT + 3 * i + 2].setBrightnessSmooth(b * (leader ? 0.6f : 0.f), args.sampleTime);
		}
		lights[LEADER_LIGHT].setBrightness(engine.leaderMode ? 1.f : 0.f);
		lights[FREEZE_LIGHT].setBrightness(engine.frozen ? 1.f : 0.f);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "gateMode", json_boolean(engine.outputMode == drift::Engine::GATE));
		json_object_set_new(root, "reactToPrevious", json_boolean(engine.reactToPrevious));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* g = json_object_get(root, "gateMode");
		if (g)
			engine.outputMode = json_boolean_value(g) ? drift::Engine::GATE : drift::Engine::TRIGGER;
		json_t* r = json_object_get(root, "reactToPrevious");
		if (r)
			engine.reactToPrevious = json_boolean_value(r);
	}
};

struct EnsembleWidget : ModuleWidget {
	EnsembleWidget(Ensemble* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Ensemble.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Positions in mm, matching the panel artwork (see res/make_panel.py)
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.0, 27.0)), module, Ensemble::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.0, 27.0)), module, Ensemble::RESET_INPUT));
		addParam(createLightParamCentered<VCVLightBezelLatch<WhiteLight>>(mm2px(Vec(53.0, 27.0)), module, Ensemble::FREEZE_PARAM, Ensemble::FREEZE_LIGHT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(65.0, 27.0)), module, Ensemble::FREEZE_INPUT));

		// Player lanes
		for (int i = 0; i < NUM_PLAYERS; i++) {
			float y = 47.0 + i * 13.5;
			addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(8.0, y)), module, Ensemble::SPREAD_PARAM + i));
			addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(19.5, y)), module, Ensemble::LEAN_PARAM + i));
			addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(mm2px(Vec(28.0, y)), module, Ensemble::HIT_LIGHT + 3 * i));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(38.0, y)), module, Ensemble::CLOCK_OUTPUT + i));
		}

		// Master column
		const float kx = 53.0, jx = 65.0;
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(kx, 47.0)), module, Ensemble::TOGETHER_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(jx, 47.0)), module, Ensemble::TOGETHER_INPUT));
		addParam(createLightParamCentered<VCVLightBezelLatch<WhiteLight>>(mm2px(Vec(kx, 60.5)), module, Ensemble::LEADER_PARAM, Ensemble::LEADER_LIGHT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(jx, 60.5)), module, Ensemble::SEED_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(kx, 74.0)), module, Ensemble::MASTER_SPREAD_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(jx, 74.0)), module, Ensemble::MASTER_SPREAD_INPUT));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(kx, 87.5)), module, Ensemble::FEEL_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(jx, 87.5)), module, Ensemble::FEEL_INPUT));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(kx, 101.0)), module, Ensemble::SWING_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(jx, 101.0)), module, Ensemble::SWING_INPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Ensemble* module = getModule<Ensemble>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Output shape",
			{"Trigger (5 ms)", "Gate (half a step)"},
			&module->engine.outputMode));
		menu->addChild(createBoolPtrMenuItem("Leader mode: react to the leader's previous hit", "",
			&module->engine.reactToPrevious));
		menu->addChild(createMenuItem("Pull everyone back to the grid", "",
			[=]() { module->engine.reset(); }));
	}
};

Model* modelEnsemble = createModel<Ensemble, EnsembleWidget>("Ensemble");
