#pragma once

#include <cstddef>
#include <cstring>

namespace webclap {

// Files from a plugin's `ui/` folder, compiled into the module (see cmake/embed-resources.cmake)
struct EmbeddedResource {
	const char *path; // e.g. "/index.html"
	const char *mimeType;
	const unsigned char *data;
	size_t size;
};

// Terminated by an entry with `path == nullptr`
extern const EmbeddedResource embeddedResources[];

inline const EmbeddedResource * findResource(const char *path) {
	for (auto *r = embeddedResources; r->path; ++r) {
		if (!std::strcmp(r->path, path)) return r;
	}
	return nullptr;
}

} // namespace
