SCNet official small model, starrytong/SCNet, MIT. Source: https://github.com/starrytong/SCNet
Original author checkpoint: https://drive.google.com/file/d/1CdEIIqsoRfHn1SJ7rccPfyYioW3BlXcW/view
Exported fixed 8-second TorchScript module with input-device-aware recurrent allocations; weights unchanged. Exported SHA256: 7ad8ab941f3e9a49a6a1847dda64b15af4f523abce41f82c36eaf239ce5c0df7
PyTorch 2.14.1 native CPU/MPS runtime, BSD-style license and third-party notices in Contents/Resources. No Python interpreter is required at runtime.

HQ uses the official SCNet-large checkpoint (MIT), with unchanged weights and the authors' configuration.
Checkpoint: https://drive.google.com/file/d/1s7QvQwn8ag9oVstGDBQ6KZvacJkvyK7t/view
Configuration: https://drive.google.com/file/d/1qxK7SZx6-Gsp1s3wCrj98X7--UcI4O3K/view
Fixed 8-second TorchScript module, SHA256 a66b584f8e25579059434f7cda01b8e6d0d9ea3488fc4d16162ebc1ce93c241e.
HQ full-track inference uses 50% overlapping, Hann-weighted windows on original source audio.
The HQ separation and playback waveforms are temporary RAM data, cleared on unload.
