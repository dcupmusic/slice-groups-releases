# Slice Groups

A macOS VST3 instrument for slicing loops, playing one-shots and multisamples, sequencing MIDI, and working with tracks and separated stems.

**[Download the latest version](https://github.com/dcupmusic/slice-groups-releases/releases/latest)**

## Install

Requires an **Apple Silicon Mac running macOS 14 or later**.

1. Download **AppleSilicon-Update.zip** for an existing installation, or **AppleSilicon-Installer.zip** for a new/offline installation, and unzip it.
2. Close Ableton or your other audio host.
3. Open **Install Slice Groups.app**, confirm the host is closed, then click **Install**.
4. Reopen the host and rescan VST3 plugins if needed. Add your sample folders inside Slice Groups.

The installer and plug-in are **Developer ID signed and notarised by Apple**. A normal first-open confirmation may appear; no Terminal quarantine-removal command is required. The installer backs up an existing version and leaves samples, tags and settings untouched. New installs use the user's VST3 folder. Existing user or writable system installations are updated in place; duplicate installations are flagged before copying.

For manual installation, right-click the installer app, choose Show Package Contents and copy `Contents/PlugIns/Slice Groups.vst3` to your existing VST3 location while the host is closed. Avoid duplicate user/system copies.

The full installer bundles stem models and the runtime. Lightweight updates reuse installed models, downloading missing weights on first stem use. No Python installation is needed. Windows and Intel Mac builds are not included.

## Source

The application source is readable in this repository. Use GitHub’s automatic source download for the current code. Download unchanged dependencies once from the permanent **0.40.2 Source.zip release asset**, which includes full dependency sources, headers, runtime libraries and model weights. MIDI extraction additionally needs the small ONNX dependency ZIP attached to v0.42.3. Similarity weights are published once as DrumSimilarity.pt on v0.43.2. See BUILD.md and THIRD_PARTY.md for exact build steps.

Slice Groups source is licensed under **AGPLv3-or-later**. Dependencies retain their own licenses.

## New in 0.43.2

- Local sound similarity: opt-in folder/file preparation, Drums and General Sound comparisons, reference-body matching and optional pitch tolerance. Prepared fingerprints and query variants are cached locally; CPU background processing, no audio upload. First pitch-tolerant search takes longer; warm searches reuse the cache.
- Bulk file moves preserve the selection and metadata; drag onto a folder or choose Move to Folder. A confirmation protects removal of more than 200 tag assignments.
- New Slice samples default to 80% transient detection; saved values are preserved.
- Faster group switching, keyboard shortcuts for favourites/previous/next/trash, sequencer MIDI output, default transient slicing and bulk slice-detection settings.
- Cooperative shutdown cancels pending metadata-sharing work safely.

## New in 0.42.3

- Native audio-to-MIDI extraction, spectrogram overlay, default Auto Quantise, longer-note priority in Mono mode and improved attack cleanup.
- Audition One Shot/Multi sounds from the browser or presets before confirmation; directly replace the source loop with that instrument and the edited MIDI.
- Independent BASS/SYNTH MIDI export folders; improved sequence locking and playback continuity.
- Folder keyword/ancestor import rules, clickable tag suggestions, melodic slicing with round robin, hover-to-delete mappings and Undo.
- Fold Notes, pitch-label dragging, horizontal pinch zoom, numeric input validation, faster group switching and improved meters.
- Synced Track-mode stem previews while whole-track timing is prepared.

## New in 0.40.2

- DAW-synced piano roll for slice patterns, one-shots and Multi instruments: MIDI recording, import, Save/Save As, pencil drawing, editable velocity, absolute snapping and waveform-style zoom.
- Master key with separate root-note selector and MIN/MAJ toggle. Sequencer swing shares Slice mode’s A–D swing groups.
- MIDI files appear in the library with detected/stored key and support tags. Save overwrites the loaded MIDI file; Save As reveals the new file in Finder.
- Multi mode: compact split mapping editor, filename root mapping and fallback pitch detection, key/velocity ranges, linked crossovers, per-sample trim and looping, mono/poly and legato.
- Bulk one-shot Mono/Poly editing; Legato is hidden in Poly.
- Dragging files or folders between library folders moves audio and metadata.


## New in 0.40.6

- Sequencer vertical pitch scrolling and cursor-centred zoom: wheel to scroll, Command-wheel to zoom, or drag the right-side bar/ends.
- Drag empty space to lasso notes; Shift-drag adds to the selection. Delete removes selected notes, Clear removes all notes; both support undo.
- Previous File, Next File and Move to Trash are configurable in Settings → Keyboard Shortcuts. Shortcuts also cover the top mixer strip, allow read-only displays, and avoid stale held-key state while suppressing native autorepeat for file actions. Editable text remains protected.
- Destination folders highlight while dragging files/folders.
- Fresh Slice files default to both grid and slice snapping off; saved snapping settings are retained.



## New in 0.40.11

- Incremental browser updates for moves, renames, deletes and Trash Undo; existing lists remain visible while results update.
- macOS filesystem notifications update affected folders after Finder changes without periodic library scans.
- One-file changes no longer rewrite the whole SQLite catalogue or rebuild/sort every browser row.
- Grid/BPM adjustments pivot around loop-in, keeping it fixed; Undo and saved recall preserve the anchor.

## New in 0.40.8

- Enhanced transient detection with frequency-band analysis, improved bass rejection, attack positioning and duplicate suppression. Compare Enhanced and Classic in Settings.
- Native macOS file drags now highlight the destination folder and move files with metadata.
- A clearly labelled yellow UPDATE AVAILABLE button opens the latest published download.

## New in 0.40.3

- Sequencer Swing is a rotary knob. Sixteenth-note tick lines remain visible at every zoom level; labels adapt to available space.
- Existing users: download **AppleSilicon-Update.zip**. It reuses your installed stem models. The installer preserves weights from older bundled installations before replacing the plugin.
- New users/offline installs: download **AppleSilicon-Installer.zip**, which includes all model weights.
- Missing weights download on first stem use, in a background worker, with size/SHA-256 verification and atomic installation. Models are retained in `~/Library/Application Support/Slice Groups/Models/scnet-v1/`.
- In-app update checks prefer the lightweight update.
- Source is available through GitHub’s automatic source archives. Unchanged dependency sources are kept in the permanent 0.40.2 source asset; see BUILD.md.
