# Building 0.40.3

Use an Apple Silicon Mac with macOS 14+, Xcode command-line tools and CMake 3.22+. GitHub’s automatic source ZIP contains the current application source; dependencies are unchanged from 0.40.2 and are downloaded only once.

1. Download [the dependency/source bundle](https://github.com/dcupmusic/slice-groups-releases/releases/download/v0.40.2/SliceGroups-0.40.2-Source.zip), extract it, and copy its `Dependencies/` and `Vendor/` directories into this checkout. This includes complete dependency source, licenses, model architecture and the native Torch SDK/runtime.
2. For a full offline installer, also copy `Resources/SCNet.pt` and `Resources/SCNet-HQ.pt` from that bundle. Without bundled weights, the plugin downloads verified models on first stem use.
3. Build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --config Release -j4
```

To rebuild static libraries, use the supplied Rubber Band 4.0.0 and libsamplerate 0.2.2 sources with their Meson/CMake build files, and replace Vendor/lib/librubberband.a and Vendor/lib/libsamplerate.a. JUCE builds from source. PyTorch inference runs natively; no Python interpreter is required by the plugin. See THIRD_PARTY.md for licenses.
