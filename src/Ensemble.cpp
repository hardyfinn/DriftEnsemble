// Ensemble.cpp
// The Drift Ensemble module: one steady clock in, six "players" out.
// All the timing maths lives in DriftEngine.hpp; this file connects it to
// knobs, jacks and lights.
#include "plugin.hpp"
#include "DriftEngine.hpp"

using drift::NUM_PLAYERS;

// Spread knob 0..1 maps to 0..80 ms with a squared curve, so noon = 20 ms
// (close to the 15-21 ms the paper measured for real players).
static const float MAX_SPREAD_MS = 80.f;

struct SpreadQuantity : ParamQuantity {
	float getDisplayValue() override {
		float v = getValue();
		return v * v * MAX_SPREAD_MS;
	}
	void setDisplayValue(float ms) override {
		setValue(std::sqrt(clamp(ms / MAX_SPREAD_MS, 0.f, 1.f)));
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

struct Ensemble : Module {
	enum ParamId {
		ENUMS(SPREAD_PARAM, NUM_PLAYERS),
		ENUMS(FEEL_PARAM, NUM_PLAYERS),
		TOGETHER_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CLOCK_INPUT,
		RESET_INPUT,
		TOGETHER_INPUT,
		ENUMS(SPREAD_INPUT, NUM_PLAYERS),
		ENUMS(FEEL_INPUT, NUM_PLAYERS),
		INPUTS_LEN
	};
	enum OutputId {
		ENUMS(CLOCK_OUTPUT, NUM_PLAYERS),
		ENUMS(ERROR_OUTPUT, NUM_PLAYERS),
		OUTPUTS_LEN
	};
	enum LightId {
		ENUMS(HIT_LIGHT, NUM_PLAYERS),
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
			configParam<SpreadQuantity>(SPREAD_PARAM + i, 0.f, 1.f, 0.5f, p + "spread", " ms");
			configParam<FeelQuantity>(FEEL_PARAM + i, 0.f, 1.f, 0.5f, p + "feel");
			configInput(SPREAD_INPUT + i, p + "spread CV");
			configInput(FEEL_INPUT + i, p + "feel CV");
			configOutput(CLOCK_OUTPUT + i, p + "clock");
			configOutput(ERROR_OUTPUT + i, p + "timing error (1 V per 10 ms, + is late)");
			configLight(HIT_LIGHT + i, p + "hit");
		}
		configParam(TOGETHER_PARAM, 0.f, 1.f, 0.3f, "Togetherness", "%", 0.f, 100.f);
		configInput(CLOCK_INPUT, "Clock (16ths)");
		configInput(RESET_INPUT, "Reset (pull everyone back to the grid)");
		configInput(TOGETHER_INPUT, "Togetherness CV");
		settingsDivider.setDivision(32);
		engine.rng.setSeed(random::u64());
		engine.randomizeStates();
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		engine.reset();
	}

	void onRandomize(const RandomizeEvent& e) override {
		Module::onRandomize(e);
		engine.randomizeStates();
	}

	// Read knobs + CV (10 V of CV sweeps the whole knob range)
	void updateSettings() {
		for (int i = 0; i < NUM_PLAYERS; i++) {
			float s = clamp(params[SPREAD_PARAM + i].getValue() + inputs[SPREAD_INPUT + i].getVoltage() / 10.f, 0.f, 1.f);
			float f = clamp(params[FEEL_PARAM + i].getValue() + inputs[FEEL_INPUT + i].getVoltage() / 10.f, 0.f, 1.f);
			engine.players[i].spread = s * s * MAX_SPREAD_MS / 1000.0;
			engine.players[i].beta = f * 2.0;
		}
		engine.togetherness = clamp(params[TOGETHER_PARAM].getValue() + inputs[TOGETHER_INPUT].getVoltage() / 10.f, 0.f, 1.f);
	}

	void process(const ProcessArgs& args) override {
		if (settingsDivider.process())
			updateSettings();

		bool clockEdge = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		bool resetEdge = resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		engine.process(args.sampleTime, clockEdge, resetEdge);

		for (int i = 0; i < NUM_PLAYERS; i++) {
			outputs[CLOCK_OUTPUT + i].setVoltage(engine.out[i] ? 10.f : 0.f);
			outputs[ERROR_OUTPUT + i].setVoltage(clamp((float) engine.offsetMs[i] / 10.f, -10.f, 10.f));
			if (engine.fired[i])
				lightPulse[i].trigger(0.03f);
			lights[HIT_LIGHT + i].setBrightnessSmooth(lightPulse[i].process(args.sampleTime) ? 1.f : 0.f, args.sampleTime);
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "gateMode", json_boolean(engine.outputMode == drift::Engine::GATE));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* g = json_object_get(root, "gateMode");
		if (g)
			engine.outputMode = json_boolean_value(g) ? drift::Engine::GATE : drift::Engine::TRIGGER;
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
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(23.0, 27.0)), module, Ensemble::RESET_INPUT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(50.0, 27.0)), module, Ensemble::TOGETHER_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(66.0, 27.0)), module, Ensemble::TOGETHER_INPUT));

		const float colX[7] = {9.0, 19.5, 31.0, 41.5, 51.0, 61.0, 72.5};
		for (int i = 0; i < NUM_PLAYERS; i++) {
			float y = 47.0 + i * 13.5;
			addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(colX[0], y)), module, Ensemble::SPREAD_PARAM + i));
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(colX[1], y)), module, Ensemble::SPREAD_INPUT + i));
			addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(colX[2], y)), module, Ensemble::FEEL_PARAM + i));
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(colX[3], y)), module, Ensemble::FEEL_INPUT + i));
			addChild(createLightCentered<MediumLight<YellowLight>>(mm2px(Vec(colX[4], y)), module, Ensemble::HIT_LIGHT + i));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(colX[5], y)), module, Ensemble::CLOCK_OUTPUT + i));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(colX[6], y)), module, Ensemble::ERROR_OUTPUT + i));
		}
	}

	void appendContextMenu(Menu* menu) override {
		Ensemble* module = getModule<Ensemble>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Output shape",
			{"Trigger (5 ms)", "Gate (half a step)"},
			&module->engine.outputMode));
		menu->addChild(createMenuItem("Pull everyone back to the grid", "",
			[=]() { module->engine.reset(); }));
	}
};

Model* modelEnsemble = createModel<Ensemble, EnsembleWidget>("Ensemble");
