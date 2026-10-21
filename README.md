# Slice Groups

A macOS VST3 instrument for slicing loops, playing one-shots and working with tracks and separated stems.

**[Download the latest version](https://github.com/dcupmusic/slice-groups-releases/releases/latest)**

## Install

Requires an **Apple Silicon Mac running macOS 14 or later**.

1. Download **AppleSilicon-Installer.zip** and unzip it (or open the **AppleSilicon.dmg** if that version offers a disk image).
2. Close Ableton or your other audio host.
3. Open **Install Slice Groups.app**, confirm the host is closed, then click **Install**.
4. Reopen the host and rescan VST3 plugins if needed. Add your sample folders inside Slice Groups.

The installer and plug-in are **Developer ID signed and notarised by Apple**. A normal first-open confirmation may appear; no Terminal quarantine-removal command is required. The installer backs up an existing version and leaves samples, tags and settings untouched. New installs use the user's VST3 folder. Existing user or writable system installations are updated in place; duplicate installations are flagged before copying.

The **AppleSilicon.zip** asset is an alternative for manual installation. Close the host and replace your existing plug-in in its current folder; for new manual installs use `~/Library/Audio/Plug-Ins/VST3/`. Avoid duplicate copies in both user and system VST3 folders.

The stem models and runtime are bundled. No Python installation is needed. Windows and Intel Mac builds are not included.

## Source

The application source is readable in this repository. For a self-contained build, download the **Source.zip release asset**, which includes dependencies and model weights; GitHub’s automatically generated source download contains the small repository only. See BUILD.md and THIRD_PARTY.md.

Slice Groups source is licensed under **AGPLv3-or-later**. Dependencies retain their own licenses.
