// Bundle "webclap-eq-airwindows": Airwindows plugins ported to WebCLAP

#include "webclap/plugin.h"

#include "./baxandall2.h"

#include <cstring>

namespace {

struct Baxandall2Plugin : public webclap::Plugin {
	static const clap_plugin_descriptor * descriptor() {
		static const char *features[] = {
			CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
			CLAP_PLUGIN_FEATURE_EQUALIZER,
			CLAP_PLUGIN_FEATURE_STEREO,
			nullptr
		};
		static const clap_plugin_descriptor descriptor{
			.clap_version=CLAP_VERSION_INIT,
			.id="io.github.matiasharju.webclap-eq.airwindows-baxandall2",
			.name="Baxandall2 (Airwindows)",
			.vendor="WebCLAP EQ",
			.url="https://github.com/matiasharju/webclap-eq",
			.manual_url="https://www.airwindows.com/baxandall2/",
			.support_url="https://github.com/matiasharju/webclap-eq/issues",
			.version="0.1.0",
			.description="Treble and bass tone controls, ported from Airwindows Baxandall2 (MIT)",
			.features=features
		};
		return &descriptor;
	}

	enum {paramTreble, paramBass};

	Baxandall2Plugin(const clap_host *host) : Plugin(host, descriptor(), {
		{.id=1, .key="treble", .name="Treble", .unit="dB", .min=-24, .max=24, .defaultValue=0},
		{.id=2, .key="bass", .name="Bass", .unit="dB", .min=-24, .max=24, .defaultValue=0},
	}) {
		uiWidth = 360;
		uiHeight = 290;
	}

	airwindows::Baxandall2 dsp;

	void prepare(double sampleRate, uint32_t maxFrames) override {}
	void reset() override {
		dsp.reset();
	}
	void processAudio(const float *inL, const float *inR, float *outL, float *outR, uint32_t frames) override {
		dsp.setParameters(paramValue(paramTreble), paramValue(paramBass), sampleRate);
		dsp.process(inL, inR, outL, outR, frames);
	}
};

// ---- plugin factory ----

uint32_t factoryGetPluginCount(const clap_plugin_factory *) {
	return 1;
}
const clap_plugin_descriptor * factoryGetPluginDescriptor(const clap_plugin_factory *, uint32_t index) {
	if (index == 0) return Baxandall2Plugin::descriptor();
	return nullptr;
}
const clap_plugin * factoryCreatePlugin(const clap_plugin_factory *, const clap_host *host, const char *pluginId) {
	if (!clap_version_is_compatible(host->clap_version)) return nullptr;
	if (!std::strcmp(pluginId, Baxandall2Plugin::descriptor()->id)) {
		return (new Baxandall2Plugin(host))->clap();
	}
	return nullptr;
}

// ---- module entry ----

bool entryInit(const char *path) {
	return true;
}
void entryDeinit() {}
const void * entryGetFactory(const char *factoryId) {
	if (!std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID)) {
		static const clap_plugin_factory factory{
			.get_plugin_count=factoryGetPluginCount,
			.get_plugin_descriptor=factoryGetPluginDescriptor,
			.create_plugin=factoryCreatePlugin
		};
		return &factory;
	}
	return nullptr;
}

} // namespace

extern "C" {
	// All three functions must be present: some hosts (e.g. openDAW) call deinit() after scanning
	CLAP_EXPORT const clap_plugin_entry clap_entry{
		.clap_version=CLAP_VERSION_INIT,
		.init=entryInit,
		.deinit=entryDeinit,
		.get_factory=entryGetFactory
	};
}
