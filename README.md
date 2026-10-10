# webclap-plugins

Free, easy-to-use audio plugins for the browser-based DAWs.

The plugins are [WebCLAP](https://github.com/WebCLAP) (WCLAP) modules: [CLAP](https://github.com/free-audio/clap) plugins compiled to WebAssembly, with a web-page UI. They run in browser DAWs such as [openDAW](https://opendaw.studio), with nothing to install.

## Plugins

| Bundle | Plugin | Status |
|---|---|---|
| `console-eq.wclap.tar.gz` | **Console EQ**: British-console-style channel EQ | first version |
| | **Studio EQ**: graphical EQ with draggable bands | planned |

## Using a plugin

1. Download a bundle from **https://matiasharju.github.io/webclap-plugins/** (always the latest build)<!--, or a fixed version from [Releases](https://github.com/matiasharju/webclap-plugins/releases)-->.
2. Load the `.wclap.tar.gz` file into your DAW as an audio effect.

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
- audio processing: the measured frequency response must match the plugin's `ui/response.js` (the formula its UI draws the curve with) in a series of random settings
- state save and load
- the web UI messages

```sh
node tests/smoke-test.mjs dist/airwindows.wclap.tar.gz
```

A plugin UI can also be opened in a browser, where it runs in demo mode without audio. Serve the folder so `response.js` loads, e.g. `python -m http.server --directory plugins` and open `http://localhost:8000/console-eq/ui/`.

### Automatic builds

[`.github/workflows/build.yml`](.github/workflows/build.yml) builds and tests every bundle on each push and pull request.
- **Pushes to `main`:** the workflow also updates the GitHub Pages site: [`web/index.html`](web/index.html) (English) and [`web/fi/index.html`](web/fi/index.html) (Finnish). The plugins shown there are listed in `SITE_PLUGINS` in the workflow. Each plugin's screenshot is rendered from its UI in demo mode, with the settings in `plugins/<name>/screenshot-settings`.
- **Tags like `v0.2.0`:** the workflow creates a GitHub Release with the bundles attached:

```sh
git tag v0.2.0
git push origin v0.2.0
```

## Project layout

```
cmake/             build helpers (WASI toolchain, bundle packing, UI embedding)
shared/webclap/    common plugin code: parameters, state, webview UI messages
plugins/airwindows Airwindows ports (DSP + ui/)
plugins/console-eq Console EQ (DSP + ui/)
tests/             headless smoke-test host
web/               GitHub Pages site (download page)
```

Each plugin's `ui/` folder is packed into the bundle as `ui/` (openDAW loads UI pages from the bundle files) and also compiled into `module.wasm`, served through the `clap.webview` extension for other hosts. UI and plugin exchange short text messages (see `shared/webclap/plugin.h`).

## License

MIT. See [LICENSE.txt](LICENSE.txt), which also covers the Airwindows code (MIT, Chris Johnson).
