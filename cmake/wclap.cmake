# add_wclap_bundle(NAME <name> SOURCES <files...> [RESOURCES_DIR <dir>] [EXTRA_FILES <files...>])
#
# Builds `<name>/module.wasm` in the build directory, and packs it into
# `dist/<name>.wclap.tar.gz` (the file you load into openDAW or another WCLAP host).
#
# Everything in RESOURCES_DIR (the web UI) is compiled into the module, and served
# to the host through the `clap.webview` extension.  EXTRA_FILES are copied into the
# bundle next to module.wasm (e.g. license texts).

set(WCLAP_EMBED_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/embed-resources.cmake)

function(add_wclap_bundle)
	cmake_parse_arguments(ARG "" "NAME;RESOURCES_DIR" "SOURCES;EXTRA_FILES" ${ARGN})

	set(bundleDir ${CMAKE_BINARY_DIR}/${ARG_NAME})
	set(resourcesCpp ${CMAKE_BINARY_DIR}/generated/${ARG_NAME}-resources.cpp)

	set(resourceFiles "")
	if (ARG_RESOURCES_DIR)
		file(GLOB_RECURSE resourceFiles CONFIGURE_DEPENDS ${ARG_RESOURCES_DIR}/*)
	endif()
	add_custom_command(
		OUTPUT ${resourcesCpp}
		COMMAND ${CMAKE_COMMAND} -DRESOURCES_DIR=${ARG_RESOURCES_DIR} -DOUTPUT=${resourcesCpp} -P ${WCLAP_EMBED_SCRIPT}
		DEPENDS ${resourceFiles} ${WCLAP_EMBED_SCRIPT}
		COMMENT "Embedding web UI for ${ARG_NAME}"
	)

	add_executable(${ARG_NAME} ${ARG_SOURCES} ${resourcesCpp})
	target_link_libraries(${ARG_NAME} PRIVATE webclap-shared)
	target_compile_options(${ARG_NAME} PRIVATE -msimd128 -fno-exceptions -Wall)
	target_link_options(${ARG_NAME} PRIVATE
		-mexec-model=reactor
		-Wl,--max-memory=67108864,--export-table,--growable-table,--export=malloc,--export=clap_entry
		$<$<CONFIG:Release>:-Wl,--strip-all>
	)
	set_target_properties(${ARG_NAME} PROPERTIES
		OUTPUT_NAME module
		SUFFIX ".wasm"
		RUNTIME_OUTPUT_DIRECTORY ${bundleDir}
	)

	set(archive ${CMAKE_SOURCE_DIR}/dist/${ARG_NAME}.wclap.tar.gz)
	set(bundleContents module.wasm)
	foreach(extra ${ARG_EXTRA_FILES})
		get_filename_component(extraName ${extra} NAME)
		list(APPEND bundleContents ${extraName})
		add_custom_command(TARGET ${ARG_NAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${extra} ${bundleDir}/${extraName}
		)
	endforeach()
	add_custom_command(TARGET ${ARG_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/dist
		COMMAND ${CMAKE_COMMAND} -E tar czf ${archive} --format=gnutar -- ${bundleContents}
		WORKING_DIRECTORY ${bundleDir}
		COMMENT "Packing dist/${ARG_NAME}.wclap.tar.gz"
	)
endfunction()
