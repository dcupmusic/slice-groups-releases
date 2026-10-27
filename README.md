# Slice Groups

A macOS VST3 instrument for slicing loops, playing one-shots and multisamples, sequencing MIDI, and working with tracks and separated stems.

**[Download the latest version](https://github.com/dcupmusic/slice-groups-releases/releases/latest)**

## Install

Requires an **Apple Silicon Mac running macOS 14 or later**.

1. Download **SliceGroups-0.47.0-AppleSilicon.pkg** from the latest release.
2. Close Ableton and other audio hosts.
3. Open the PKG and authorize installation when macOS asks.
4. Reopen your host and rescan VST3 plug-ins if needed.

The Developer ID signed, Apple-notarized PKG installs the full VST3 and stem models into `/Library/Audio/Plug-Ins/VST3/Slice Groups.vst3` for all users. It can replace an existing system installation with administrator authorization. Your samples, tags, presets and settings are preserved. No Python or model download is needed.

If you also have a duplicate personal copy in `~/Library/Audio/Plug-Ins/VST3`, remove that duplicate before restarting your host. ZIP installers remain available on earlier releases. Windows and Intel Mac builds are not included.

## Source

The application source is readable in this repository. Use GitHub’s automatic source download for the current code. Download unchanged dependencies once from the permanent **0.40.2 Source.zip release asset**, which includes full dependency sources, headers, runtime libraries and model weights. MIDI extraction additionally needs the small ONNX dependency ZIP attached to v0.42.3. Similarity weights are published once as DrumSimilarity.pt on v0.43.2. See BUILD.md and THIRD_PARTY.md for exact build steps.

Slice Groups source is licensed under **AGPLv3-or-later**. Dependencies retain their own licenses.

## New in 0.47.0

- Folder Up/Down navigation updates the file list and keeps a single highlight; intentional multi-selection remains supported.
- Option-drag copies notes or the selected notes in one Undo step. Option-click creates no copy; Command bypasses grid snapping.
- The audition sound menu can save or clear a global default, including the sound configuration, gain and Mono/Poly. Default loading is asynchronous and respects existing choices.
- Deleting a melodic sample zone preserves MIDI notes, clears its mapping references and supports Undo.
- Melodic Multis have a bipolar Timbre knob (-24 to +24 semitones) beside Swing. It selects lower/higher sample zones with pitch compensation; notes and MIDI export remain unchanged. Timbre saves with the instrument and supports Undo.
- Signed, notarized system-wide PKG installer with administrator authorization.

## New in 0.46.3

- Per-slice playback ends in milliseconds and end fades, with yellow waveform handles, multi-selection, reset and Undo. Fades reach zero on their last output frame, including resampled/pitched playback, timed cuts and generated Multi regions.
- Editable Melodic Slices: adjust boundaries, review detected MIDI pitches or choose No note, audition browser/preset sounds, then explicitly Create Sequence. Creation restores full-keyboard Multi mapping and round-robin layers; Zones opens the mapping editor.
- Minimise/maximise lower instrument parameters in every playback mode for a shorter browsing window. This view state is not stored in presets. Stretch → Slice or Melodic Slices starts transient detection.
- Similarity feedback with thumbs up/down and Best Match. Improve Search learns a local ranking from feedback with progress/readiness indication; ratings remain visible.
- Per-track saved grid adjustments, no cross-track nudge carry, and faster reusable track pre-analysis.

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
