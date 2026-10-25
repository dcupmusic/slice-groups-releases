# Third-party components

- JUCE 8: AGPLv3/commercial dual license; bundled source in Dependencies/JUCE. This release uses the AGPLv3 option. See Dependencies/JUCE/LICENSE.md.
- Rubber Band 4.0.0: GPLv2 or later; source in Dependencies/rubberband-4.0.0 and license in Vendor/RubberBand-COPYING. Built Apple Silicon static library in Vendor/lib.
- libsamplerate 0.2.2: BSD 2-clause; source in Dependencies/libsamplerate-0.2.2 and license in Vendor/libsamplerate-LICENSE.md.
- SCNet small and large: MIT; architecture source in Dependencies/SCNet. Checkpoint origins, conversion notes and hashes in Resources/StemModel-NOTICE.md, MIT license in Resources/StemModel-LICENSE.md. Exported weights are included in Resources.
- PyTorch native runtime and headers: BSD-style and third-party licenses in Vendor/scnet-runtime/PyTorch-LICENSE.txt and PyTorch-THIRD-PARTY-NOTICES.txt. Header SDK included in Vendor/torch-sdk/include.

No audio samples or user sample libraries are included.

Dependency sources, runtime headers and upstream build instructions remain available in the permanent [0.40.2 source bundle](https://github.com/dcupmusic/slice-groups-releases/releases/download/v0.40.2/SliceGroups-0.40.2-Source.zip). Model weights are also published once as SCNet.pt and SCNet-HQ.pt on v0.40.3. Their hashes/origins are in Resources/StemModel-NOTICE.md.

- Spotify Basic Pitch model and native decoder adaptation: Apache-2.0. Graph, license and attribution are in Resources/BasicPitch.onnx, BasicPitch-LICENSE.txt and BasicPitch-NOTICE.txt. Upstream: https://github.com/spotify/basic-pitch.
- ONNX Runtime 1.26.0: MIT and component notices in Vendor/onnxruntime/LICENSE and ThirdPartyNotices.txt. Runtime and headers are in the v0.42.3 ONNX dependency add-on. Upstream source: https://github.com/microsoft/onnxruntime/tree/v1.26.0.
