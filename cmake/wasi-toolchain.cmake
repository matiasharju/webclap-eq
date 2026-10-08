# CMake toolchain for WASI-SDK (threads-enabled WebAssembly, shared imported memory)
# Like wasi-sdk's own `wasi-sdk-pthread.cmake`, but with the current `wasm32-wasip1-threads` target name.

if (NOT WASI_SDK_PREFIX)
	if (DEFINED ENV{WASI_SDK_PATH})
		set(WASI_SDK_PREFIX $ENV{WASI_SDK_PATH})
	else()
		set(WASI_SDK_PREFIX ${CMAKE_CURRENT_LIST_DIR}/../.tools/wasi-sdk)
	endif()
endif()

set(CMAKE_SYSTEM_NAME WASI)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR wasm32)
set(triple wasm32-wasip1-threads)

if (CMAKE_HOST_WIN32)
	set(exe ".exe")
else()
	set(exe "")
endif()

set(CMAKE_C_COMPILER ${WASI_SDK_PREFIX}/bin/clang${exe})
set(CMAKE_CXX_COMPILER ${WASI_SDK_PREFIX}/bin/clang++${exe})
set(CMAKE_AR ${WASI_SDK_PREFIX}/bin/llvm-ar${exe})
set(CMAKE_RANLIB ${WASI_SDK_PREFIX}/bin/llvm-ranlib${exe})
set(CMAKE_C_COMPILER_TARGET ${triple})
set(CMAKE_CXX_COMPILER_TARGET ${triple})
set(CMAKE_SYSROOT ${WASI_SDK_PREFIX}/share/wasi-sysroot)

set(CMAKE_C_FLAGS_INIT "-pthread")
set(CMAKE_CXX_FLAGS_INIT "-pthread")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--import-memory -Wl,--export-memory")
set(CMAKE_EXECUTABLE_SUFFIX .wasm)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
