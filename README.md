# Slice Groups

A macOS VST3 instrument for slicing loops, playing one-shots and working with tracks and separated stems.

**[Download the latest version](https://github.com/dcupmusic/slice-groups-releases/releases/latest)**

## Install

Requires an **Apple Silicon Mac running macOS 14 or later**.

1. Download the **AppleSilicon.zip** release asset and unzip it.
2. Close Ableton or your other audio host.
3. Copy **Slice Groups.vst3** to `~/Library/Audio/Plug-Ins/VST3/` (create the folder if needed).
4. Restart the host and rescan VST3 plugins. Add your sample folders inside Slice Groups.

This build is ad-hoc signed and **not notarized**. If macOS blocks it, allow it in **System Settings → Privacy & Security**. If the host still blocks it, remove quarantine from this downloaded plugin only:

```sh
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/VST3/Slice Groups.vst3"
```

The stem models and runtime are bundled. No Python installation is needed. Windows and Intel Mac builds are not included.

## Source

The application source is readable in this repository. For a self-contained build, download the **Source.zip release asset**, which includes dependencies and model weights; GitHub’s automatically generated source download contains the small repository only. See BUILD.md and THIRD_PARTY.md.

Slice Groups source is licensed under **AGPLv3-or-later**. Dependencies retain their own licenses.
