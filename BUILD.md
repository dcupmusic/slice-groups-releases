# Building 0.36.3

Use an Apple Silicon Mac with macOS 14+, Xcode command-line tools and CMake 3.22+. Download and unzip the full Source release asset, which includes the dependency sources, matching headers, model weights and prebuilt Apple Silicon libraries. The small GitHub checkout alone omits these large dependencies.

```sh
cd SliceGroups-source
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --config Release -j4
```

The VST3 and standalone app appear under `build/SliceGroups_artefacts/Release/`. The source archive changes only dependency default paths in CMakeLists.txt to use this self-contained layout.

To rebuild the vendored static libraries, use the supplied Rubber Band 4.0.0 and libsamplerate 0.2.2 sources with their Meson/CMake build files and Apple Silicon/static-library settings, and replace Vendor/lib/librubberband.a and Vendor/lib/libsamplerate.a. Their upstream build instructions and licenses are included. JUCE is compiled from the included source as part of the main build. The supplied PyTorch runtime is used for native SCNet inference; no Python interpreter is used by the plugin.

The source license is AGPLv3-or-later; third-party components retain the licenses listed in THIRD_PARTY.md.
