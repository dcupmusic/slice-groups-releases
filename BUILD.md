# Building 0.50.1

Use an Apple Silicon Mac with macOS 14+, Xcode command-line tools and CMake 3.22+. GitHub’s automatic source ZIP contains the current application source; the base dependencies from 0.40.2 are downloaded only once. MIDI extraction adds ONNX Runtime 1.26.0.

1. Download [the dependency/source bundle](https://github.com/dcupmusic/slice-groups-releases/releases/download/v0.40.2/SliceGroups-0.40.2-Source.zip), extract it, and copy its `Dependencies/` and `Vendor/` directories into this checkout. This includes complete dependency source, licenses, model architecture and the native Torch SDK/runtime.
2. Download [the ONNX dependency add-on](https://github.com/dcupmusic/slice-groups-releases/releases/download/v0.42.3/SliceGroups-0.42.3-ONNX-Dependency.zip) and extract its `Vendor/` into this checkout, merging it with the base bundle. This adds ONNX Runtime headers, the Apple Silicon runtime and licenses; no Python runtime is required. The Basic Pitch model and Apache-2.0 notices are tracked in `Resources/`.
3. For a full offline installer, also copy `Resources/SCNet.pt` and `Resources/SCNet-HQ.pt` from that bundle. Without bundled weights, the plugin downloads verified models on first stem use.
4. Download [DrumSimilarity.pt](https://github.com/dcupmusic/slice-groups-releases/releases/download/v0.43.2/DrumSimilarity.pt) into `Resources/` for sound similarity. This is the audio-only LAION CLAP model, Apache-2.0; provenance and license are in Resources/DrumSimilarity-NOTICE.txt and DrumSimilarity-LICENSE.txt. To reproduce its conversion, run `tools/export-drum-similarity.py` with PyTorch 2.14.1 and Transformers 4.57.6. The plugin itself requires no Python.
5. Build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --config Release -j4
```

To rebuild static libraries, use the supplied Rubber Band 4.0.0 and libsamplerate 0.2.2 sources with their Meson/CMake build files, and replace Vendor/lib/librubberband.a and Vendor/lib/libsamplerate.a. JUCE builds from source. PyTorch inference runs natively; no Python interpreter is required by the plugin. See THIRD_PARTY.md for licenses.
