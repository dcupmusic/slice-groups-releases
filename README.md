# Slice Groups

A macOS VST3 instrument for slicing loops, playing one-shots and multisamples, sequencing MIDI, and working with tracks and separated stems.

**[Download the latest version](https://github.com/dcupmusic/slice-groups-releases/releases/latest)**

## Install

Requires an **Apple Silicon Mac running macOS 14 or later**.

1. Download **SliceGroups-0.51.0-AppleSilicon.pkg** from the latest release.
2. Close Ableton and other audio hosts.
3. Open the PKG and authorize installation when macOS asks.
4. Reopen your host and rescan VST3 plug-ins if needed.

The Developer ID signed, Apple-notarized PKG installs the full VST3 and stem models into `/Library/Audio/Plug-Ins/VST3/Slice Groups.vst3` for all users. It can replace an existing system installation with administrator authorization. Your samples, tags, presets and settings are preserved. No Python or model download is needed.

If you also have a duplicate personal copy in `~/Library/Audio/Plug-Ins/VST3`, remove that duplicate before restarting your host. ZIP installers remain available on earlier releases. Windows and Intel Mac builds are not included.

## Source

The application source is readable in this repository. Use GitHub’s automatic source download for the current code. Download unchanged dependencies once from the permanent **0.40.2 Source.zip release asset**, which includes full dependency sources, headers, runtime libraries and model weights. MIDI extraction additionally needs the small ONNX dependency ZIP attached to v0.42.3. Similarity weights are published once as DrumSimilarity.pt on v0.43.2. See BUILD.md and THIRD_PARTY.md for exact build steps.

Slice Groups source is licensed under **AGPLv3-or-later**. Dependencies retain their own licenses.

## New in 0.51.0

- Project-only arrangement controls, MIDI autosave and separate audio/MIDI browser locations per group.
- MIDI-aware Previous/Next, Trash and Show in Library.
- MIDI loop-region handles and fixes for live-edit retriggering and browser scroll jumps.
- All 36 regression suites passed. Developer ID signed, Apple-notarized PKG with approval ticket attached.

## New in 0.50.5

- Fixed Option-click slice insertion; FX anchor placement uses Option+Shift-click.
- Dynamic stems pitch fix, Slice FX scheduling with MIDI Gate, power-of-two bar scaling, keyboard warning controls, Anchor Bar and Bars column.
- Faster tag display while Bars metadata updates in the background.
- The current installer is signed and Apple-notarized.

## New in 0.50.1

- Multi-file selection keeps the normal mode controls, layout and units. The waveform shows the selection count; unsupported actions are hidden. Existing controls edit all selected files with Mixed states, background saves and Undo.
- MIDI Gate buttons show each group’s Ableton trigger note: C-2 through G-2.
- Bulk editing follows the selected files’ playback type even with Auto Load off, without replacing the playing sound.

## New in 0.50.0

- FX Anchor supports multiple editable, individually sized Project Bar boxes, with add/remove controls and project-state/Undo support. Positions stay with the group while browsing samples.
- MIDI Gate enables normal synced playback while each group’s assigned MIDI note (0–7) is held. Adjacent notes keep longer phrases moving without restarting. Gate and FX Anchor are mutually exclusive; One Shot and sequencer playback retain normal note routing.
- Track Auto Cue: on loads the next Track in the same folder paused; off prepares and starts it at the next project bar boundary. Other file types are skipped, active loops remain loops, and the final track stops the group.
- Track DB −1 / DB +1 shifts the downbeat assignment by exactly one beat while preserving BPM, beat timing, playhead sample and active loop audio region. Supports Undo and saved track recall.
- Dynamic/Straight dropdown no longer loses selections to periodic UI refresh. Pending Straight analysis is identified in the control tooltip.
- Bar half/double controls round down to whole even values, with a one-bar minimum. Escape commits numeric edits and removes focus like Enter, including slider text fields.
- New Multis default to 250 ms release. Fresh Note Harvest SFZ imports use 250 ms rather than the generated recording-tail release; explicit releases from other SFZs and existing saved edits remain respected.


## New in 0.49.0

- MIDI filename keys (including major/minor) take precedence over embedded key signatures and note inference. Explicit library edits take precedence over detection.
- Audio and MIDI browser key editing supports root, major/minor, Unknown and Use Detected, including bulk editing. MIDI browser and Clip Key use one shared key lookup and manual metadata.
- Fixed Clip Key selections reverting during asynchronous dropdown notifications. Key edits support Undo and saved recall; metadata writes run in the background.
- Saving transposed MIDI adopts the actual saved notes and key, avoiding a second transposition or swing pass. Finishing a save cannot replace a subsequently loaded clip.
- Right-click a group number to copy its sound, mode, edits and sequence to another group. Destination MIDI channel, output, volume, mute and solo remain unchanged. Group replacement supports Undo/Redo, including empty slots and Track playback ownership. Copies share immutable decoded audio with independent editable state.
- With zone Audition enabled, each left-list row click retriggers the selected zone, including repeated clicks on the same row. Prior preview voices stop, Audition stays enabled, and refresh/trash actions do not trigger sound.


## New in 0.48.0

- Mono + Legato keeps active sample layers and their envelopes, loops and playback position across overlapping notes, including octave changes.
- SFZ/native Multi loading skips musical analysis and reuses decoded samples with a 128 MiB bounded cache. Waveform preparation runs separately in the background.
- SFZ and unsaved Multi autosave uses local `multi-edits` storage; original SFZ/audio files stay unchanged. Save Multi creates a native instrument on first save and asynchronously overwrites it afterward. Save Multi As creates a separate instrument.
- Sequencer length offers Bars or Beats, including one-, two- and three-beat loops. MIDI export preserves exact duration.
- Large tag-removal confirmations name the tag. Search tag text opens Include/Exclude; only the right X removes it, and changing polarity preserves typed keywords.


## New in 0.47.1

- Key Lock and target-key controls are linked across the MIDI and waveform pages. Master Key shows its effective destination while preserving the local target; a missing Clip Key is indicated instead of guessed.
- Melodic conversion preserves source key, Key Lock, target, transpose direction and manual pitch. Local Key Lock transposes the melody without changing its intervals or baking pitches into sample mappings. Timbre remains independent.
- MIDI Out, Save, Save As and drag-out use effective melodic pitches and matching key metadata. Editable notes remain native; ordinary Slice exports retain their trigger notes. Key changes release old sequencer notes, and local Key Lock supports Undo and saved recall.
- Minimize keeps the plugin window size and expands the file browser in all six modes. Expanding restores the browser allocation without resizing when there is room. The collapsed view still allows manual window resizing and is not stored in presets.

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
