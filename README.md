StepMania (Browser Port)
========================

A WebAssembly/WebGL port of [StepMania 5.1](https://github.com/stepmania/stepmania)
that runs in the browser via Emscripten. Forked from upstream `5_1-new` and
extended with browser-specific arch backends.

[Play it in the browser](https://harrisong.github.io/stepmania-browser/) ·
[Upstream project](https://github.com/stepmania/stepmania)

## What's different in this fork

This fork adds an Emscripten target on top of the upstream build, plus the arch
plumbing needed to run the engine in a single-threaded WebAssembly environment.

* **New arch backends.** `RageSoundDriver_SDL` (Web Audio via SDL2),
  `InputHandler_SDL` (HTML5 keyboard/mouse), `LowLevelWindow_SDL` (WebGL 2
  context), `ArchHooks_Emscripten`, `Threads_Null` (single-threaded stubs),
  and a `GameLoop_Emscripten` driven by `emscripten_set_main_loop`.
* **Single-threaded pipeline workarounds.** Emscripten builds without
  `-pthread`, so `RageThread::Create` returns stubs. The audio decode chain
  is pumped inline instead: `RageSoundDriver` exposes `DecodePlayingSounds()`,
  called from the SDL/Null driver `Update()` paths under `__EMSCRIPTEN__`,
  and `RageSoundReader_ThreadedBuffer::Read` fills synchronously when the
  buffering thread can't run. Without this, song position never advances and
  no notes appear.
* **GLES2/WebGL backend fixes.** Several rendering paths were tightened to
  satisfy WebGL's stricter validation than desktop GL.
* **CMake wiring.** `CMakeLists.emscripten.txt` selects the browser-friendly
  options (no FFmpeg, no MP3, OGG/Vorbis, WebGL 2, IDBFS) and preloads
  `Themes/`, `Songs/`, `NoteSkins/`, etc. into the `.data` virtual filesystem.
* **Regenerated font atlases.** The bundled font PNGs were re-encoded to a
  format the WebGL texture loader accepts.

The desktop builds (Windows/macOS/Linux) are unchanged and continue to work
through the upstream CMake paths.

## Building for the browser

Requires the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html).
Tested with `emcc 5.0.7`.

```bash
# Activate emsdk (path will differ on your machine)
source /path/to/emsdk/emsdk_env.sh

# Release build (used by the Pages deploy: -O3 -flto --closure 1)
mkdir -p build-web-release
cd build-web-release
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release -C ../CMakeLists.emscripten.txt
emmake make -j$(nproc)
```

The build emits `stepmania.{js,wasm,data}` at the repo root. To serve them:

```bash
cp stepmania.{js,wasm,data} web/
cd web && python3 -m http.server 8080
# Then open http://localhost:8080/
```

A debug build (`-DCMAKE_BUILD_TYPE=Debug`) is significantly larger (~77 MB
wasm vs ~5.9 MB release) but easier to debug.

## Deployment

`.github/workflows/pages.yml` runs the release build on every push to
`5_1-new` and deploys the result to GitHub Pages. To enable hosting on a
fork, go to **Settings → Pages → Source: GitHub Actions**.

## Browser support

Requires WebGL 2 and Web Audio. Tested in recent Chromium-based browsers.
The `ScriptProcessorNode` deprecation warning in the console is expected —
that's how Emscripten's SDL2 audio backend currently dispatches the audio
callback.

---

Below is the upstream StepMania README, unchanged.

StepMania
=========

StepMania is an advanced cross-platform rhythm game for home and arcade use.

[![Continuous integration](https://github.com/stepmania/stepmania/workflows/Continuous%20integration/badge.svg?branch=5_1-new)](https://github.com/stepmania/stepmania/actions?query=workflow%3A%22Continuous+integration%22+branch%3A5_1-new)
[![Build Status](https://travis-ci.org/stepmania/stepmania.svg?branch=master)](https://travis-ci.org/stepmania/stepmania)
[![Build status](https://ci.appveyor.com/api/projects/status/uvoplsnyoats81r2?svg=true)](https://ci.appveyor.com/project/Nickito12/stepmania)

## Installation
### From Packages

For those that do not wish to compile the game on their own and use a binary right away, be aware of the following issues:

* Windows users are expected to have installed the [Microsoft Visual C++ x86 Redistributable for Visual Studio 2015](http://www.microsoft.com/en-us/download/details.aspx?id=48145) prior to running the game. For those on a 64-bit operating system, grab the x64 redistributable as well. Windows 7 is the minimum supported version.
* Mac OS X users need to have Mac OS X 10.6.8 or higher to run StepMania.
* Linux users should receive all they need from the package manager of their choice.

### From Source

StepMania can be compiled using [CMake](http://www.cmake.org/). More information about using CMake can be found in both the `Build` directory and CMake's documentation.

## Resources

* Website: https://www.stepmania.com/
* IRC: irc.freenode.net/#stepmania-devs
* Lua for SM5: https://quietly-turning.github.io/Lua-For-SM5/
* Lua API Documentation can be found in the Docs folder.

## Licensing Terms

In short- you can do anything you like with the game (including sell products made with it), provided you *do not*:

1. Sell the game *with the included songs*
2. Claim to have created the engine yourself or remove the credits
3. Not provide source code for any build which differs from any official release which includes MP3 support.

(It's not required, but we would also appreciate it if you link back to http://www.stepmania.com/)

For specific information/legalese:

* All of our source code is under the [MIT license](http://opensource.org/licenses/MIT).
* Any songs that are included within this repository are under the [<abbr title="Creative Commons Non-Commercial">CC-NC</abbr> license](https://creativecommons.org/).
* The [MAD library](http://www.underbit.com/products/mad/) and [FFmpeg codecs](https://www.ffmpeg.org/) when built with our code use the [GPL license](http://www.gnu.org).
