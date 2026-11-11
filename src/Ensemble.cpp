// Ensemble.cpp
// The Drift Ensemble module: one steady clock in, six "players" out.
// All the timing maths lives in DriftEngine.hpp; this file connects it to
// knobs, jacks, lights and the drift display.
#include "plugin.hpp"
#include "DriftEngine.hpp"

using drift::NUM_PLAYERS;

// Spread knob 0..1 maps to 0..50 ms on a curve that puts noon at 15 ms:
// ms = 50 * x^k with 0.5^k = 0.3. Default (double-click) is 0 ms.
static const float MAX_SPREAD_MS = 50.f;
static const float SPREAD_CURVE = std::log(0.3f) / std::log(0.5f);
static const float MAX_LEAN_MS = 35.f;
static const float MAX_MASTER_SPREAD_MS = 20.f;
static const float MAX_PUSH_MS = 30.f;
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
		ENUMS(LANE_SWING_PARAM, NUM_PLAYERS),
		TOGETHER_PARAM,
		LEADER_PARAM,
		SEED_PARAM,
		MASTER_SPREAD_PARAM,
		FEEL_PARAM,
		SWING_PARAM,
		ANCHOR_PARAM,
		PUSH_PARAM,
		ENERGY_PARAM,
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
		ENERGY_INPUT,
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
	bool greyscale = false;

	// For the display (read by the UI thread)
	float displayOffsetMs[NUM_PLAYERS] = {};
	float displayFlash[NUM_PLAYERS] = {};

	Ensemble() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int i = 0; i < NUM_PLAYERS; i++) {
			std::string p = string::f("Player %d ", i + 1);
			configParam<SpreadQuantity>(SPREAD_PARAM + i, 0.f, 1.f, 0.f, p + "spread", " ms");
			configParam(LEAN_PARAM + i, -MAX_LEAN_MS, MAX_LEAN_MS, 0.f, p + "lean (- early, + late)", " ms");
			configParam(LANE_SWING_PARAM + i, 0.f, 1.f, 0.f, p + "swing (added to master swing)", "%", 0.f, 25.f, 50.f);
			configOutput(CLOCK_OUTPUT + i, p + "clock");
			configLight(HIT_LIGHT + 3 * i, p + "hit");
		}
		configParam(TOGETHER_PARAM, 0.f, 1.f, 0.3f, "Togetherness", "%", 0.f, 100.f);
		configSwitch(LEADER_PARAM, 0.f, 1.f, 0.f, "Togetherness mode", {"Band (shared feel)", "Leader (follow player 1)"});
		configParam<SeedQuantity>(SEED_PARAM, 0.f, (float) MAX_SEED, 0.f, "Seed");
		getParamQuantity(SEED_PARAM)->snapEnabled = true;
		configParam(MASTER_SPREAD_PARAM, -MAX_MASTER_SPREAD_MS, MAX_MASTER_SPREAD_MS, 0.f, "Master spread (added to every player)", " ms");
		configParam<FeelQuantity>(FEEL_PARAM, 0.f, 1.f, 0.5f, "Feel");
		configParam(SWING_PARAM, 0.f, 1.f, 0.f, "Master swing", "%", 0.f, 25.f, 50.f);
		configParam(ANCHOR_PARAM, 0.f, 1.f, 0.f, "Anchor (tighten on each bar's first 16th)", "%", 0.f, 100.f);
		configParam(PUSH_PARAM, 0.f, MAX_PUSH_MS, 0.f, "Push (rush into each new phrase)", " ms");
		configParam(ENERGY_PARAM, 0.f, 1.f, 1.f, "Energy (scales all drift)", "%", 0.f, 100.f);
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze", {"Off", "On"});
		configInput(CLOCK_INPUT, "Clock (16ths)");
		configInput(RESET_INPUT, "Reset (pull everyone back to the grid, marks the downbeat)");
		configInput(FREEZE_INPUT, "Freeze gate (high = frozen)");
		configInput(TOGETHER_INPUT, "Togetherness CV");
		configInput(MASTER_SPREAD_INPUT, "Master spread CV");
		configInput(FEEL_INPUT, "Feel CV");
		configInput(SWING_INPUT, "Master swing CV");
		configInput(ENERGY_INPUT, "Energy CV");
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
		engine.barSteps = 16;
		engine.phraseBars = 8;
		engine.loosenBars = 0;
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
			engine.laneSwing[i] = 0.5 + 0.25 * params[LANE_SWING_PARAM + i].getValue();
		}
		float ms = params[MASTER_SPREAD_PARAM].getValue() + inputs[MASTER_SPREAD_INPUT].getVoltage() * (2.f * MAX_MASTER_SPREAD_MS / 10.f);
		engine.masterSpread = clamp(ms, -MAX_MASTER_SPREAD_MS, MAX_MASTER_SPREAD_MS) / 1000.0;
		engine.beta = 2.0 * clamp(params[FEEL_PARAM].getValue() + inputs[FEEL_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		engine.togetherness = clamp(params[TOGETHER_PARAM].getValue() + inputs[TOGETHER_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		engine.swing = 0.5 + 0.25 * clamp(params[SWING_PARAM].getValue() + inputs[SWING_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		engine.energy = clamp(params[ENERGY_PARAM].getValue() + inputs[ENERGY_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		engine.anchor = params[ANCHOR_PARAM].getValue();
		engine.push = params[PUSH_PARAM].getValue() / 1000.0;
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
			if (engine.fired[i]) {
				lightPulse[i].trigger(0.03f);
				displayOffsetMs[i] = (float) engine.offsetMs[i];
				displayFlash[i] = 0.08f;
			}
			if (displayFlash[i] > 0.f)
				displayFlash[i] -= args.sampleTime;
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
		json_object_set_new(root, "barSteps", json_integer(engine.barSteps));
		json_object_set_new(root, "phraseBars", json_integer(engine.phraseBars));
		json_object_set_new(root, "loosenBars", json_integer(engine.loosenBars));
		json_object_set_new(root, "greyscale", json_boolean(greyscale));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		if ((j = json_object_get(root, "gateMode")))
			engine.outputMode = json_boolean_value(j) ? drift::Engine::GATE : drift::Engine::TRIGGER;
		if ((j = json_object_get(root, "reactToPrevious")))
			engine.reactToPrevious = json_boolean_value(j);
		if ((j = json_object_get(root, "barSteps")))
			engine.barSteps = clamp((int) json_integer_value(j), 1, 64);
		if ((j = json_object_get(root, "phraseBars")))
			engine.phraseBars = clamp((int) json_integer_value(j), 2, 64);
		if ((j = json_object_get(root, "loosenBars")))
			engine.loosenBars = clamp((int) json_integer_value(j), 0, 256);
		if ((j = json_object_get(root, "greyscale")))
			greyscale = json_boolean_value(j);
	}
};

// Positions in mm, matching the panel artwork (see res/make_panel.py)
static const float TOP_Y = 27.f;
static const float ROW0_Y = 47.f, ROW_DY = 13.5f;
static const float SPREAD_X = 9.5f, LEAN_X = 24.f, LSWING_X = 38.5f, LIGHT_X = 47.5f, OUT_X = 57.5f;
static const float C1 = 75.f, C2 = 88.5f, C3 = 102.f;
static const Vec DISPLAY_POS = Vec(33.f, 18.5f), DISPLAY_SIZE = Vec(74.8f, 16.5f);

// Six lanes: the centre line is the clock, each dot is that player's last
// hit (left = early, right = late, full width = 50 ms), flashing on each hit.
struct DriftDisplay : TransparentWidget {
	Ensemble* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		float w = box.size.x, h = box.size.y;
		float cx = w / 2.f, laneH = h / NUM_PLAYERS;
		float scale = (w / 2.f - 4.f) / 50.f; // px per ms
		// Faint guides: centre line and +-25 ms
		nvgStrokeWidth(args.vg, 1.f);
		nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 70));
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, cx, 2.f);
		nvgLineTo(args.vg, cx, h - 2.f);
		nvgStroke(args.vg);
		nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 25));
		for (float g : {-25.f, 25.f}) {
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, cx + g * scale, 2.f);
			nvgLineTo(args.vg, cx + g * scale, h - 2.f);
			nvgStroke(args.vg);
		}
		for (int i = 0; i < NUM_PLAYERS; i++) {
			float y = laneH * (i + 0.5f);
			float off = module ? module->displayOffsetMs[i] : (i - 2.5f) * 6.f;
			bool flash = module && module->displayFlash[i] > 0.f;
			bool leader = module && module->engine.leaderMode && i == 0;
			float x = cx + clamp(off, -50.f, 50.f) * scale;
			NVGcolor c = leader ? nvgRGB(0xF0, 0x5A, 0xA0) : nvgRGB(0xE4, 0xE0, 0x52);
			if (!flash)
				c.a = 0.55f;
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, x, y, flash ? laneH * 0.42f : laneH * 0.3f);
			nvgFillColor(args.vg, c);
			nvgFill(args.vg);
		}
	}
};

struct EnsembleWidget : ModuleWidget {
	bool shownGrey = false;

	EnsembleWidget(Ensemble* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Ensemble.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.f, TOP_Y)), module, Ensemble::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(24.f, TOP_Y)), module, Ensemble::RESET_INPUT));

		DriftDisplay* display = createWidget<DriftDisplay>(mm2px(DISPLAY_POS));
		display->box.size = mm2px(DISPLAY_SIZE);
		display->module = module;
		addChild(display);

		// Player lanes
		for (int i = 0; i < NUM_PLAYERS; i++) {
			float y = ROW0_Y + i * ROW_DY;
			addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(SPREAD_X, y)), module, Ensemble::SPREAD_PARAM + i));
			addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(LEAN_X, y)), module, Ensemble::LEAN_PARAM + i));
			addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(LSWING_X, y)), module, Ensemble::LANE_SWING_PARAM + i));
			addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(mm2px(Vec(LIGHT_X, y)), module, Ensemble::HIT_LIGHT + 3 * i));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(OUT_X, y)), module, Ensemble::CLOCK_OUTPUT + i));
		}

		// Master section
		auto rowY = [](int r) { return ROW0_Y + r * ROW_DY; };
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(C1, rowY(0))), module, Ensemble::TOGETHER_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(C2, rowY(0))), module, Ensemble::TOGETHER_INPUT));
		addParam(createLightParamCentered<VCVLightBezelLatch<WhiteLight>>(mm2px(Vec(C3, rowY(0))), module, Ensemble::LEADER_PARAM, Ensemble::LEADER_LIGHT));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(C1, rowY(1))), module, Ensemble::MASTER_SPREAD_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(C2, rowY(1))), module, Ensemble::MASTER_SPREAD_INPUT));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(C3, rowY(1))), module, Ensemble::SEED_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(C1, rowY(2))), module, Ensemble::FEEL_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(C2, rowY(2))), module, Ensemble::FEEL_INPUT));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(C3, rowY(2))), module, Ensemble::ANCHOR_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(C1, rowY(3))), module, Ensemble::SWING_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(C2, rowY(3))), module, Ensemble::SWING_INPUT));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(C3, rowY(3))), module, Ensemble::PUSH_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(C1, rowY(4))), module, Ensemble::ENERGY_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(C2, rowY(4))), module, Ensemble::ENERGY_INPUT));
		addParam(createLightParamCentered<VCVLightBezelLatch<WhiteLight>>(mm2px(Vec(C1, rowY(5))), module, Ensemble::FREEZE_PARAM, Ensemble::FREEZE_LIGHT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(C2, rowY(5))), module, Ensemble::FREEZE_INPUT));
	}

	// Swap between the colour and greyscale panels
	void step() override {
		Ensemble* module = getModule<Ensemble>();
		if (module && module->greyscale != shownGrey) {
			shownGrey = module->greyscale;
			SvgPanel* panel = dynamic_cast<SvgPanel*>(getPanel());
			if (panel) {
				panel->setBackground(Svg::load(asset::plugin(pluginInstance,
					shownGrey ? "res/Ensemble-grey.svg" : "res/Ensemble.svg")));
				panel->fb->setDirty();
			}
		}
		ModuleWidget::step();
	}

	void appendContextMenu(Menu* menu) override {
		Ensemble* module = getModule<Ensemble>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Greyscale panel", "", &module->greyscale));
		menu->addChild(createIndexPtrSubmenuItem("Output shape",
			{"Trigger (5 ms)", "Gate (half a step)"},
			&module->engine.outputMode));
		menu->addChild(createBoolPtrMenuItem("Leader mode: react to the leader's previous hit", "",
			&module->engine.reactToPrevious));

		// Bar / phrase / loosen settings
		static const std::vector<int> barChoices = {12, 16, 24, 32};
		menu->addChild(createSubmenuItem("Bar length (for Anchor and Push)", string::f("%d steps", module->engine.barSteps), [=](Menu* sub) {
			for (int b : barChoices)
				sub->addChild(createCheckMenuItem(string::f("%d steps", b), "",
					[=]() { return module->engine.barSteps == b; },
					[=]() { module->engine.barSteps = b; }));
		}));
		static const std::vector<int> phraseChoices = {2, 4, 8, 16};
		menu->addChild(createSubmenuItem("Phrase length (for Push)", string::f("%d bars", module->engine.phraseBars), [=](Menu* sub) {
			for (int p : phraseChoices)
				sub->addChild(createCheckMenuItem(string::f("%d bars", p), "",
					[=]() { return module->engine.phraseBars == p; },
					[=]() { module->engine.phraseBars = p; }));
		}));
		static const std::vector<int> loosenChoices = {0, 8, 16, 32, 64};
		menu->addChild(createSubmenuItem("Loosen up after Reset",
			module->engine.loosenBars ? string::f("over %d bars", module->engine.loosenBars) : "off", [=](Menu* sub) {
			for (int l : loosenChoices)
				sub->addChild(createCheckMenuItem(l ? string::f("over %d bars", l) : "off", "",
					[=]() { return module->engine.loosenBars == l; },
					[=]() { module->engine.loosenBars = l; }));
		}));
		menu->addChild(createMenuItem("Pull everyone back to the grid", "",
			[=]() { module->engine.reset(); }));
	}
};

Model* modelEnsemble = createModel<Ensemble, EnsembleWidget>("Ensemble");
