# webclap-eq

Free, beginner-friendly EQ plugins for the web, made for high-school students and teachers.

The plugins are [WebCLAP](https://github.com/WebCLAP) (WCLAP) modules: [CLAP](https://github.com/free-audio/clap) plugins compiled to WebAssembly, with a web-page UI. They run in browser DAWs such as [openDAW](https://opendaw.studio), with nothing to install.

## Plugins

| Bundle | Plugin | Status |
|---|---|---|
| `webclap-eq-airwindows` | **Baxandall2 (Airwindows)**: bass and treble tone controls, ported from [Airwindows](https://www.airwindows.com/baxandall2/) | milestone 0 |
| | **Console EQ**: British-console-style channel EQ | planned |
| | **Studio EQ**: graphical EQ with draggable bands | planned |

## Using a plugin

1. Download `webclap-eq-airwindows.wclap.tar.gz`. Until releases exist, build it as described below; the file appears in `dist/`.
2. Load the `.wclap.tar.gz` file into your DAW (openDAW, or a test host such as [wclap.plinken.org](https://wclap.plinken.org/)) as an audio effect.

## Building

You need [CMake](https://cmake.org) 3.28+, [Ninja](https://ninja-build.org) and [WASI-SDK](https://github.com/WebAssembly/wasi-sdk/releases). Unpack WASI-SDK into `.tools/wasi-sdk/` (git-ignored), or point the `WASI_SDK_PATH` environment variable at it.

```sh
cmake --preset wasm
cmake --build --preset wasm
```

This produces `dist/<bundle>.wclap.tar.gz`.

### Testing

`tests/smoke-test.mjs` is a small headless host in Node.js (v20+). It loads a bundle and checks:
- the CLAP entry and factory
- parameters
- audio processing, including the measured frequency response
- state save and load
- the web UI messages

```sh
node tests/smoke-test.mjs dist/webclap-eq-airwindows.wclap.tar.gz
```

A plugin UI can also be opened directly in a browser (e.g. `plugins/airwindows/ui/index.html`). It then runs in demo mode, without audio.

## Project layout

```
cmake/             build helpers (WASI toolchain, bundle packing, UI embedding)
shared/webclap/    common plugin code: parameters, state, webview UI messages
plugins/airwindows Airwindows ports (DSP + ui/)
tests/             headless smoke-test host
```

Each plugin's `ui/` folder is compiled into its `module.wasm` and served through the `clap.webview` extension. UI and plugin exchange short text messages (see `shared/webclap/plugin.h`).

## License

MIT. See [LICENSE.txt](LICENSE.txt), which also covers the Airwindows code (MIT, Chris Johnson).
