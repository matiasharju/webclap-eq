// Bundle "console-eq": British-console-style channel EQ

#include "webclap/entry.h"
#include "webclap/plugin.h"

#include "./console-eq.h"

namespace {

const char *offOn[] = {"Off", "On"};
const char *shelfBell[] = {"Shelf", "Bell"};
const char *outIn[] = {"Out", "In"};
const char *qModes[] = {"Constant", "Proportional"};

struct ConsoleEqPlugin : public webclap::Plugin {
	static const clap_plugin_descriptor * descriptor() {
		static const char *features[] = {
			CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
			CLAP_PLUGIN_FEATURE_EQUALIZER,
			CLAP_PLUGIN_FEATURE_FILTER,
			CLAP_PLUGIN_FEATURE_STEREO,
			nullptr
		};
		static const clap_plugin_descriptor descriptor{
			.clap_version=CLAP_VERSION_INIT,
			.id="io.github.matiasharju.webclap-plugins.console-eq",
			.name="Console EQ",
			.vendor="WebCLAP Plugins",
			.url="https://github.com/matiasharju/webclap-plugins",
			.manual_url="https://github.com/matiasharju/webclap-plugins",
			.support_url="https://github.com/matiasharju/webclap-plugins/issues",
			.version="0.1.0",
			.description="British-console-style channel EQ: filters plus four bands",
			.features=features
		};
		return &descriptor;
	}

	// Parameter order (indices for paramValue())
	enum {pHpfOn, pHpfFreq, pLpfOn, pLpfFreq,
		pHfGain, pHfFreq, pHfBell, pHmfGain, pHmfFreq, pHmfQ, pLmfGain, pLmfFreq, pLmfQ, pLfGain, pLfFreq, pLfBell,
		pEqIn, pQMode, pOutput};

	ConsoleEqPlugin(const clap_host *host) : Plugin(host, descriptor(), {
		// IDs must never change: saved projects and automation refer to them
		{.id=1, .key="hpfOn", .name="HPF", .unit="", .min=0, .max=1, .defaultValue=0, .labels=offOn, .module="Filters"},
		{.id=2, .key="hpfFreq", .name="HPF Freq", .unit="Hz", .min=16, .max=350, .defaultValue=80, .decimals=0, .module="Filters"},
		{.id=3, .key="lpfOn", .name="LPF", .unit="", .min=0, .max=1, .defaultValue=0, .labels=offOn, .module="Filters"},
		{.id=4, .key="lpfFreq", .name="LPF Freq", .unit="Hz", .min=3000, .max=22000, .defaultValue=12000, .decimals=0, .module="Filters"},
		{.id=10, .key="hfGain", .name="HF Gain", .unit="dB", .min=-15, .max=15, .defaultValue=0, .module="HF"},
		{.id=11, .key="hfFreq", .name="HF Freq", .unit="Hz", .min=1500, .max=16000, .defaultValue=8000, .decimals=0, .module="HF"},
		{.id=12, .key="hfBell", .name="HF Shape", .unit="", .min=0, .max=1, .defaultValue=0, .labels=shelfBell, .module="HF"},
		{.id=20, .key="hmfGain", .name="HMF Gain", .unit="dB", .min=-15, .max=15, .defaultValue=0, .module="HMF"},
		{.id=21, .key="hmfFreq", .name="HMF Freq", .unit="Hz", .min=600, .max=7000, .defaultValue=2500, .decimals=0, .module="HMF"},
		{.id=22, .key="hmfQ", .name="HMF Q", .unit="", .min=0.4, .max=4, .defaultValue=1, .decimals=2, .module="HMF"},
		{.id=30, .key="lmfGain", .name="LMF Gain", .unit="dB", .min=-15, .max=15, .defaultValue=0, .module="LMF"},
		{.id=31, .key="lmfFreq", .name="LMF Freq", .unit="Hz", .min=200, .max=2500, .defaultValue=600, .decimals=0, .module="LMF"},
		{.id=32, .key="lmfQ", .name="LMF Q", .unit="", .min=0.4, .max=4, .defaultValue=1, .decimals=2, .module="LMF"},
		{.id=40, .key="lfGain", .name="LF Gain", .unit="dB", .min=-15, .max=15, .defaultValue=0, .module="LF"},
		{.id=41, .key="lfFreq", .name="LF Freq", .unit="Hz", .min=30, .max=450, .defaultValue=100, .decimals=0, .module="LF"},
		{.id=42, .key="lfBell", .name="LF Shape", .unit="", .min=0, .max=1, .defaultValue=0, .labels=shelfBell, .module="LF"},
		{.id=50, .key="eqIn", .name="EQ In", .unit="", .min=0, .max=1, .defaultValue=1, .labels=outIn, .module="Master"},
		{.id=51, .key="qMode", .name="Q Mode", .unit="", .min=0, .max=1, .defaultValue=0, .labels=qModes, .module="Master"},
		{.id=52, .key="output", .name="Output", .unit="dB", .min=-15, .max=15, .defaultValue=0, .module="Master"},
	}) {
		uiWidth = 640;
		uiHeight = 420;
	}

	console_eq::ConsoleEq eq;

	console_eq::Settings settings() const {
		console_eq::Settings s;
		s.hpfOn = paramValue(pHpfOn) >= 0.5;
		s.hpfFreq = paramValue(pHpfFreq);
		s.lpfOn = paramValue(pLpfOn) >= 0.5;
		s.lpfFreq = paramValue(pLpfFreq);
		s.hfGain = paramValue(pHfGain);
		s.hfFreq = paramValue(pHfFreq);
		s.hfBell = paramValue(pHfBell) >= 0.5;
		s.hmfGain = paramValue(pHmfGain);
		s.hmfFreq = paramValue(pHmfFreq);
		s.hmfQ = paramValue(pHmfQ);
		s.lmfGain = paramValue(pLmfGain);
		s.lmfFreq = paramValue(pLmfFreq);
		s.lmfQ = paramValue(pLmfQ);
		s.lfGain = paramValue(pLfGain);
		s.lfFreq = paramValue(pLfFreq);
		s.lfBell = paramValue(pLfBell) >= 0.5;
		s.eqIn = paramValue(pEqIn) >= 0.5;
		s.proportionalQ = paramValue(pQMode) >= 0.5;
		s.outputDb = paramValue(pOutput);
		return s;
	}

	void prepare(double sampleRate, uint32_t maxFrames) override {
		eq.prepare(sampleRate);
	}
	void reset() override {
		eq.reset(settings());
	}
	void processAudio(const float *inL, const float *inR, float *outL, float *outR, uint32_t frames) override {
		eq.process(settings(), inL, inR, outL, outR, frames);
	}
};

} // namespace

WEBCLAP_BUNDLE(ConsoleEqPlugin)
