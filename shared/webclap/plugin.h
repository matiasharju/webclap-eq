#pragma once

// Shared base for our stereo effect plugins.
//
// It implements the CLAP plumbing which every plugin in this repo needs:
//   - parameters (with sample-accurate automation and UI gestures)
//   - state save/load
//   - a stereo audio port
//   - a web UI through the `clap.webview/3` extension (plus `clap.gui` with the "webview" API)
//
// A plugin derives from `webclap::Plugin`, lists its parameters, and implements `prepare()`,
// `reset()` and `processAudio()`.
//
// UI <-> plugin messages are short UTF-8 text lines, so they're easy to read while debugging:
//   UI -> plugin:  "ready" | "poll" | "set <key> <value>" | "begin <key>" | "end <key>"
//   plugin -> UI:  "param <key> <value>" | "samplerate <hz>"
//
// The UI sends "poll" regularly while it's open, and the plugin replies with anything that changed
// (e.g. from automation).  Hosts should call `on_main_thread()` after `request_callback()` so we
// could push updates ourselves, but not all do (openDAW, October 2026), so we don't rely on it.

#include "clap/clap.h"
#include "clap/ext/draft/webview.h"

#include "./resources.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace webclap {

// Makes a plain-C CLAP function which calls a C++ method on our plugin object.
// (Same idea as `signalsmith::clap::pluginMethod()` from signalsmith-clap-cpp)
template<class T> struct MethodHelper;
template<class Object, typename Return, typename... Args>
struct MethodHelper<Return (Object::*)(Args...)> {
	template<Return (Object::*method)(Args...)>
	static Return call(const clap_plugin *plugin, Args... args) {
		return (((Object *)plugin->plugin_data)->*method)(args...);
	}
};
template<auto method>
auto pluginMethod() {
	return MethodHelper<decltype(method)>::template call<method>;
}

struct ParamSpec {
	clap_id id; // must never change once released, or saved projects lose their automation
	const char *key; // short name used in the UI messages and saved state
	const char *name; // shown by the host
	const char *unit;
	double min, max, defaultValue;
	int decimals = 1;
};

class Plugin {
public:
	Plugin(const clap_host *host, const clap_plugin_descriptor *descriptor, std::vector<ParamSpec> specs)
			: host(host), paramCount(specs.size()), params(new Param[specs.size()]) {
		for (size_t i = 0; i < paramCount; ++i) {
			params[i].spec = specs[i];
			params[i].value = specs[i].defaultValue;
		}
		clapPlugin = {
			.desc=descriptor,
			.plugin_data=this,
			.init=pluginMethod<&Plugin::pluginInit>(),
			.destroy=pluginMethod<&Plugin::pluginDestroy>(),
			.activate=pluginMethod<&Plugin::pluginActivate>(),
			.deactivate=pluginMethod<&Plugin::pluginDeactivate>(),
			.start_processing=pluginMethod<&Plugin::pluginStartProcessing>(),
			.stop_processing=pluginMethod<&Plugin::pluginStopProcessing>(),
			.reset=pluginMethod<&Plugin::pluginReset>(),
			.process=pluginMethod<&Plugin::pluginProcess>(),
			.get_extension=pluginMethod<&Plugin::pluginGetExtension>(),
			.on_main_thread=pluginMethod<&Plugin::pluginOnMainThread>()
		};
	}
	virtual ~Plugin() {}

	const clap_plugin * clap() const {
		return &clapPlugin;
	}

protected:
	// ---- implemented by each plugin ----

	virtual void prepare(double sampleRate, uint32_t maxFrames) = 0;
	virtual void reset() = 0;
	// Stereo in, stereo out.  Buffers may be the same (in-place).  Read parameters with `paramValue()`.
	virtual void processAudio(const float *inL, const float *inR, float *outL, float *outR, uint32_t frames) = 0;

	// UI page size in pixels, and the start page (the plugin's `ui/` folder is `/ui/` in the bundle)
	uint32_t uiWidth = 400, uiHeight = 300;
	const char *uiStartPage = "/ui/index.html";

	double paramValue(size_t index) const {
		return params[index].value.load(std::memory_order_relaxed);
	}
	double sampleRate = 48000;

private:
	struct Param {
		ParamSpec spec;
		std::atomic<double> value{0};
		// value changed by the host, needs sending to the UI
		std::atomic_flag uiUpToDate = ATOMIC_FLAG_INIT;
		// changed in the UI, needs sending to the host as events
		std::atomic_flag hostGotBegin = ATOMIC_FLAG_INIT, hostGotValue = ATOMIC_FLAG_INIT, hostGotEnd = ATOMIC_FLAG_INIT;

		Param() {
			hostGotBegin.test_and_set();
			hostGotValue.test_and_set();
			hostGotEnd.test_and_set();
		}

		double clamp(double v) const {
			if (!(v >= spec.min)) return spec.min; // also catches NaN
			if (v > spec.max) return spec.max;
			return v;
		}
	};

	const clap_host *host;
	const clap_host_params *hostParams = nullptr;
	const clap_host_state *hostState = nullptr;
	const clap_host_webview *hostWebview = nullptr;

	clap_plugin clapPlugin;
	size_t paramCount;
	std::unique_ptr<Param[]> params;

	std::atomic_flag stateClean = ATOMIC_FLAG_INIT;
	std::atomic_flag uiSampleRateSent = ATOMIC_FLAG_INIT;
	bool uiOpen = false;

	Param * findParam(clap_id id) {
		for (size_t i = 0; i < paramCount; ++i) {
			if (params[i].spec.id == id) return &params[i];
		}
		return nullptr;
	}
	Param * findParam(const char *key) {
		for (size_t i = 0; i < paramCount; ++i) {
			if (!std::strcmp(params[i].spec.key, key)) return &params[i];
		}
		return nullptr;
	}

	template<class Ext>
	void getHostExtension(const char *id, const Ext *&ext) {
		ext = (const Ext *)host->get_extension(host, id);
	}
	void requestCallback() {
		host->request_callback(host);
	}

	// ---- plugin lifecycle ----

	bool pluginInit() {
		getHostExtension(CLAP_EXT_PARAMS, hostParams);
		getHostExtension(CLAP_EXT_STATE, hostState);
		getHostExtension(CLAP_EXT_WEBVIEW, hostWebview);
		return true;
	}
	void pluginDestroy() {
		delete this;
	}
	bool pluginActivate(double sRate, uint32_t minFrames, uint32_t maxFrames) {
		sampleRate = sRate;
		scratch.resize(maxFrames);
		silence.assign(maxFrames, 0.0f);
		prepare(sRate, maxFrames);
		reset();
		uiSampleRateSent.clear();
		requestCallback();
		return true;
	}
	void pluginDeactivate() {}
	bool pluginStartProcessing() {
		return true;
	}
	void pluginStopProcessing() {}
	void pluginReset() {
		reset();
	}

	// ---- events & processing ----

	void applyEvent(const clap_event_header *event) {
		if (event->space_id != CLAP_CORE_EVENT_SPACE_ID || event->type != CLAP_EVENT_PARAM_VALUE) return;
		auto *paramEvent = (const clap_event_param_value *)event;
		Param *param = findParam(paramEvent->param_id);
		if (!param) return;
		param->value = param->clamp(paramEvent->value);
		param->uiUpToDate.clear();
		stateClean.clear();
		requestCallback();
	}

	// Sends UI-originated changes to the host (gesture begin, value, gesture end)
	void sendParamEventsToHost(const clap_output_events *out) {
		for (size_t i = 0; i < paramCount; ++i) {
			auto &param = params[i];
			if (!param.hostGotBegin.test_and_set()) {
				clap_event_param_gesture event{
					.header={sizeof(clap_event_param_gesture), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_GESTURE_BEGIN, CLAP_EVENT_IS_LIVE},
					.param_id=param.spec.id
				};
				out->try_push(out, &event.header);
			}
			if (!param.hostGotValue.test_and_set()) {
				clap_event_param_value event{
					.header={sizeof(clap_event_param_value), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, CLAP_EVENT_IS_LIVE},
					.param_id=param.spec.id,
					.cookie=nullptr,
					.note_id=-1, .port_index=-1, .channel=-1, .key=-1,
					.value=param.value.load()
				};
				out->try_push(out, &event.header);
			}
			if (!param.hostGotEnd.test_and_set()) {
				clap_event_param_gesture event{
					.header={sizeof(clap_event_param_gesture), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_GESTURE_END, CLAP_EVENT_IS_LIVE},
					.param_id=param.spec.id
				};
				out->try_push(out, &event.header);
			}
		}
	}

	clap_process_status pluginProcess(const clap_process *process) {
		const uint32_t frames = process->frames_count;
		float *outL = nullptr, *outR = nullptr;
		if (process->audio_outputs_count > 0 && process->audio_outputs[0].channel_count > 0) {
			auto &port = process->audio_outputs[0];
			outL = port.data32[0];
			outR = (port.channel_count > 1) ? port.data32[1] : nullptr;
		}
		const float *inL = nullptr, *inR = nullptr;
		if (process->audio_inputs_count > 0 && process->audio_inputs[0].channel_count > 0) {
			auto &port = process->audio_inputs[0];
			inL = port.data32[0];
			inR = (port.channel_count > 1) ? port.data32[1] : inL; // mono input: feed it to both sides
		}

		auto *inEvents = process->in_events;
		const uint32_t eventCount = inEvents->size(inEvents);
		if (!outL) {
			// Nothing to write audio into, but keep track of parameter changes
			for (uint32_t i = 0; i < eventCount; ++i) applyEvent(inEvents->get(inEvents, i));
			sendParamEventsToHost(process->out_events);
			return CLAP_PROCESS_CONTINUE;
		}

		// Mono output: process into scratch for the right channel
		if (!outR) {
			if (scratch.size() < frames) scratch.resize(frames); // only if the host gave a bigger block than promised
			outR = scratch.data();
		}
		if (!inL) {
			// No input connected: process silence
			if (silence.size() < frames) silence.assign(frames, 0.0f);
			inL = inR = silence.data();
		}

		// Split the block at each parameter change, so automation is sample-accurate
		uint32_t start = 0, eventIndex = 0;
		while (start < frames) {
			while (eventIndex < eventCount) {
				auto *event = inEvents->get(inEvents, eventIndex);
				if (event->time > start) break;
				applyEvent(event);
				++eventIndex;
			}
			uint32_t end = frames;
			if (eventIndex < eventCount) {
				end = std::min(frames, inEvents->get(inEvents, eventIndex)->time);
			}
			processAudio(inL + start, inR + start, outL + start, outR + start, end - start);
			start = end;
		}
		for (; eventIndex < eventCount; ++eventIndex) applyEvent(inEvents->get(inEvents, eventIndex));

		sendParamEventsToHost(process->out_events);
		return CLAP_PROCESS_CONTINUE;
	}
	std::vector<float> scratch, silence;

	void pluginOnMainThread() {
		if (hostState && !stateClean.test_and_set()) {
			hostState->mark_dirty(host);
		}
		sendUiUpdates();
	}

	// ---- extensions ----

	const void * pluginGetExtension(const char *id) {
		if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) {
			static const clap_plugin_audio_ports ext{
				.count=pluginMethod<&Plugin::audioPortsCount>(),
				.get=pluginMethod<&Plugin::audioPortsGet>()
			};
			return &ext;
		} else if (!std::strcmp(id, CLAP_EXT_PARAMS)) {
			static const clap_plugin_params ext{
				.count=pluginMethod<&Plugin::paramsCount>(),
				.get_info=pluginMethod<&Plugin::paramsGetInfo>(),
				.get_value=pluginMethod<&Plugin::paramsGetValue>(),
				.value_to_text=pluginMethod<&Plugin::paramsValueToText>(),
				.text_to_value=pluginMethod<&Plugin::paramsTextToValue>(),
				.flush=pluginMethod<&Plugin::paramsFlush>()
			};
			return &ext;
		} else if (!std::strcmp(id, CLAP_EXT_STATE)) {
			static const clap_plugin_state ext{
				.save=pluginMethod<&Plugin::stateSave>(),
				.load=pluginMethod<&Plugin::stateLoad>()
			};
			return &ext;
		} else if (!std::strcmp(id, CLAP_EXT_WEBVIEW)) {
			static const clap_plugin_webview ext{
				.get_uri=pluginMethod<&Plugin::webviewGetUri>(),
				.get_resource=pluginMethod<&Plugin::webviewGetResource>(),
				.receive=pluginMethod<&Plugin::webviewReceive>()
			};
			return &ext;
		} else if (!std::strcmp(id, CLAP_EXT_GUI)) {
			static const clap_plugin_gui ext{
				.is_api_supported=pluginMethod<&Plugin::guiIsApiSupported>(),
				.get_preferred_api=pluginMethod<&Plugin::guiGetPreferredApi>(),
				.create=pluginMethod<&Plugin::guiCreate>(),
				.destroy=pluginMethod<&Plugin::guiDestroy>(),
				.set_scale=pluginMethod<&Plugin::guiSetScale>(),
				.get_size=pluginMethod<&Plugin::guiGetSize>(),
				.can_resize=pluginMethod<&Plugin::guiCanResize>(),
				.get_resize_hints=pluginMethod<&Plugin::guiGetResizeHints>(),
				.adjust_size=pluginMethod<&Plugin::guiAdjustSize>(),
				.set_size=pluginMethod<&Plugin::guiSetSize>(),
				.set_parent=pluginMethod<&Plugin::guiSetParent>(),
				.set_transient=pluginMethod<&Plugin::guiSetTransient>(),
				.suggest_title=pluginMethod<&Plugin::guiSuggestTitle>(),
				.show=pluginMethod<&Plugin::guiShow>(),
				.hide=pluginMethod<&Plugin::guiHide>()
			};
			return &ext;
		}
		return nullptr;
	}

	// ---- audio ports: one stereo in, one stereo out ----

	uint32_t audioPortsCount(bool isInput) {
		return 1;
	}
	bool audioPortsGet(uint32_t index, bool isInput, clap_audio_port_info *info) {
		if (index != 0) return false;
		*info = {
			.id=0,
			.name={},
			.flags=CLAP_AUDIO_PORT_IS_MAIN,
			.channel_count=2,
			.port_type=CLAP_PORT_STEREO,
			.in_place_pair=0
		};
		std::strncpy(info->name, isInput ? "Input" : "Output", CLAP_NAME_SIZE);
		return true;
	}

	// ---- parameters ----

	uint32_t paramsCount() {
		return uint32_t(paramCount);
	}
	bool paramsGetInfo(uint32_t index, clap_param_info *info) {
		if (index >= paramCount) return false;
		auto &spec = params[index].spec;
		*info = {
			.id=spec.id,
			.flags=CLAP_PARAM_IS_AUTOMATABLE,
			.cookie=nullptr,
			.name={},
			.module={},
			.min_value=spec.min,
			.max_value=spec.max,
			.default_value=spec.defaultValue
		};
		std::strncpy(info->name, spec.name, CLAP_NAME_SIZE - 1);
		return true;
	}
	bool paramsGetValue(clap_id id, double *value) {
		auto *param = findParam(id);
		if (!param) return false;
		*value = param->value;
		return true;
	}
	bool paramsValueToText(clap_id id, double value, char *text, uint32_t capacity) {
		auto *param = findParam(id);
		if (!param) return false;
		std::snprintf(text, capacity, "%.*f %s", param->spec.decimals, value, param->spec.unit);
		return true;
	}
	bool paramsTextToValue(clap_id id, const char *text, double *value) {
		auto *param = findParam(id);
		if (!param) return false;
		char *end = nullptr;
		double v = std::strtod(text, &end);
		if (end == text) return false;
		*value = param->clamp(v);
		return true;
	}
	void paramsFlush(const clap_input_events *in, const clap_output_events *out) {
		const uint32_t count = in->size(in);
		for (uint32_t i = 0; i < count; ++i) applyEvent(in->get(in, i));
		sendParamEventsToHost(out);
	}

	// ---- state: one "key value" line per parameter ----

	bool stateSave(const clap_ostream *stream) {
		std::string text = "webclap-state 1\n";
		char line[128];
		for (size_t i = 0; i < paramCount; ++i) {
			std::snprintf(line, sizeof(line), "%s %.17g\n", params[i].spec.key, params[i].value.load());
			text += line;
		}
		size_t written = 0;
		while (written < text.size()) {
			int64_t result = stream->write(stream, text.data() + written, text.size() - written);
			if (result <= 0) return false;
			written += size_t(result);
		}
		stateClean.test_and_set();
		return true;
	}
	bool stateLoad(const clap_istream *stream) {
		std::string text;
		char buffer[256];
		while (true) {
			int64_t result = stream->read(stream, buffer, sizeof(buffer));
			if (result < 0) return false;
			if (result == 0) break;
			text.append(buffer, size_t(result));
		}
		if (text.rfind("webclap-state ", 0) != 0) return false;

		size_t lineStart = 0;
		while (lineStart < text.size()) {
			size_t lineEnd = text.find('\n', lineStart);
			if (lineEnd == std::string::npos) lineEnd = text.size();
			std::string line = text.substr(lineStart, lineEnd - lineStart);
			lineStart = lineEnd + 1;

			char key[64];
			double value;
			if (std::sscanf(line.c_str(), "%63s %lf", key, &value) == 2) {
				if (auto *param = findParam(key)) { // unknown keys (from newer versions) are ignored
					param->value = param->clamp(value);
					param->uiUpToDate.clear();
				}
			}
		}
		requestCallback(); // update the UI
		return true;
	}

	// ---- web UI ----

	void uiSend(const char *message) {
		if (!uiOpen || !hostWebview) return;
		hostWebview->send(host, message, uint32_t(std::strlen(message)));
	}
	void sendUiUpdates() {
		if (!uiOpen) return;
		char message[128];
		if (!uiSampleRateSent.test_and_set()) {
			std::snprintf(message, sizeof(message), "samplerate %.17g", sampleRate);
			uiSend(message);
		}
		for (size_t i = 0; i < paramCount; ++i) {
			auto &param = params[i];
			if (param.uiUpToDate.test_and_set()) continue;
			std::snprintf(message, sizeof(message), "param %s %.17g", param.spec.key, param.value.load());
			uiSend(message);
		}
	}

	int32_t webviewGetUri(char *uri, uint32_t capacity) {
		if (uri && capacity > 0) {
			std::strncpy(uri, uiStartPage, capacity - 1);
			uri[capacity - 1] = 0;
		}
		return int32_t(std::strlen(uiStartPage) + 1);
	}
	bool webviewGetResource(const char *path, char *mime, uint32_t mimeCapacity, const clap_ostream *stream) {
		// ignore any query string or fragment
		std::string cleanPath = path;
		size_t queryStart = cleanPath.find_first_of("?#");
		if (queryStart != std::string::npos) cleanPath.resize(queryStart);
		if (cleanPath == "/") cleanPath = uiStartPage;

		auto *resource = findResource(cleanPath.c_str());
		if (!resource) return false;
		if (mime && mimeCapacity > 0) {
			std::strncpy(mime, resource->mimeType, mimeCapacity - 1);
			mime[mimeCapacity - 1] = 0;
		}
		size_t written = 0;
		while (written < resource->size) {
			int64_t result = stream->write(stream, resource->data + written, resource->size - written);
			if (result <= 0) return false;
			written += size_t(result);
		}
		return true;
	}
	bool webviewReceive(const void *buffer, uint32_t size) {
		std::string message((const char *)buffer, size);
		char command[16], key[64];
		double value;
		if (message == "ready") {
			uiOpen = true;
			uiSampleRateSent.clear();
			for (size_t i = 0; i < paramCount; ++i) params[i].uiUpToDate.clear();
			sendUiUpdates();
			return true;
		} else if (message == "poll") {
			uiOpen = true;
			pluginOnMainThread();
			return true;
		}
		int fields = std::sscanf(message.c_str(), "%15s %63s %lf", command, key, &value);
		if (fields < 2) return false;
		Param *param = findParam(key);
		if (!param) return false;

		if (!std::strcmp(command, "set") && fields == 3) {
			param->value = param->clamp(value);
			param->hostGotValue.clear();
			stateClean.clear();
		} else if (!std::strcmp(command, "begin")) {
			param->hostGotBegin.clear();
		} else if (!std::strcmp(command, "end")) {
			param->hostGotEnd.clear();
		} else {
			return false;
		}
		if (hostParams) hostParams->request_flush(host);
		if (hostState && !stateClean.test_and_set()) hostState->mark_dirty(host);
		return true;
	}

	// ---- clap.gui, only for the "webview" window API (no native windows inside WebAssembly) ----

	bool guiIsApiSupported(const char *api, bool isFloating) {
		return !std::strcmp(api, CLAP_WINDOW_API_WEBVIEW);
	}
	bool guiGetPreferredApi(const char **api, bool *isFloating) {
		*api = CLAP_WINDOW_API_WEBVIEW;
		*isFloating = false;
		return true;
	}
	bool guiCreate(const char *api, bool isFloating) {
		return !std::strcmp(api, CLAP_WINDOW_API_WEBVIEW);
	}
	void guiDestroy() {
		uiOpen = false;
	}
	bool guiSetScale(double scale) {
		return false; // webview UIs use logical pixels
	}
	bool guiGetSize(uint32_t *width, uint32_t *height) {
		*width = uiWidth;
		*height = uiHeight;
		return true;
	}
	bool guiCanResize() {
		return false;
	}
	bool guiGetResizeHints(clap_gui_resize_hints *hints) {
		return false;
	}
	bool guiAdjustSize(uint32_t *width, uint32_t *height) {
		return guiGetSize(width, height);
	}
	bool guiSetSize(uint32_t width, uint32_t height) {
		return width == uiWidth && height == uiHeight;
	}
	bool guiSetParent(const clap_window *window) {
		return true;
	}
	bool guiSetTransient(const clap_window *window) {
		return false;
	}
	void guiSuggestTitle(const char *title) {}
	bool guiShow() {
		return true;
	}
	bool guiHide() {
		return true;
	}
};

} // namespace
