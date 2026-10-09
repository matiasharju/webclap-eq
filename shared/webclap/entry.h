#pragma once

// The module entry (`clap_entry`) and plugin factory for a bundle.  In one .cpp per bundle:
//
//     WEBCLAP_BUNDLE(MyFirstPlugin, MySecondPlugin)
//
// Each plugin class needs `static const clap_plugin_descriptor * descriptor()` and a constructor
// taking `const clap_host *`, and must derive from `webclap::Plugin`.

#include "clap/clap.h"

#include <cstring>

namespace webclap {

template<class... Plugins>
struct Bundle {
	static uint32_t getPluginCount(const clap_plugin_factory *) {
		return sizeof...(Plugins);
	}
	static const clap_plugin_descriptor * getPluginDescriptor(const clap_plugin_factory *, uint32_t index) {
		const clap_plugin_descriptor *descriptors[] = {Plugins::descriptor()...};
		return index < sizeof...(Plugins) ? descriptors[index] : nullptr;
	}
	static const clap_plugin * createPlugin(const clap_plugin_factory *, const clap_host *host, const char *pluginId) {
		if (!clap_version_is_compatible(host->clap_version)) return nullptr;
		const clap_plugin *result = nullptr;
		((result = result ? result : (std::strcmp(pluginId, Plugins::descriptor()->id) ? nullptr : (new Plugins(host))->clap())), ...);
		return result;
	}

	static bool init(const char *path) {
		return true;
	}
	// Must exist: some hosts (e.g. openDAW) call deinit() after scanning
	static void deinit() {}
	static const void * getFactory(const char *factoryId) {
		static constexpr clap_plugin_factory factory{
			.get_plugin_count=getPluginCount,
			.get_plugin_descriptor=getPluginDescriptor,
			.create_plugin=createPlugin
		};
		return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) ? nullptr : &factory;
	}

	static constexpr clap_plugin_entry entry{
		.clap_version=CLAP_VERSION_INIT,
		.init=init,
		.deinit=deinit,
		.get_factory=getFactory
	};
};

} // namespace

#define WEBCLAP_BUNDLE(...) \
	extern "C" { \
		CLAP_EXPORT const clap_plugin_entry clap_entry = webclap::Bundle<__VA_ARGS__>::entry; \
	}
