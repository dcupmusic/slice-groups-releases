#pragma once
namespace Guide520 {
inline constexpr const char* hotkeys=R"SGDOC(<!doctype html><html><meta charset="utf-8"><title>Slice Groups Hotkeys</title><style>body{font:17px system-ui;max-width:850px;margin:40px auto;padding:0 24px;line-height:1.6;background:#222;color:#eee}pre{white-space:pre-wrap;font:inherit}</style><pre># Hotkeys and mouse gestures

## Assign your keyboard shortcuts
Open Settings → Keyboard Shortcuts. Click an action and press your preferred shortcut. X clears a binding; Escape cancels learning. Assignments are global. The plug-in must have keyboard focus. Typing into an editable text or numeric field takes precedence over file-navigation shortcuts.

Available actions: Back 1 bar, Forward 1 bar, Beat Jump Back and Forward (selected size), Loop on/off, Previous file, Next file, Move loaded file to Trash, and Favourite selected/loaded file. These actions are configurable, rather than fixed universal key bindings.

## General
- Command+Z: Undo. Command+Shift+Z: Redo.
- Command+T: open tagging for the selection.
- Command+Up in a browser list: parent folder.

## Sequencer
- Delete/Backspace with the note editor focused: delete selected notes. Command+A: select all notes.
- Click a note: audition it once. Double-click: delete it.
- Drag empty space: lasso select. Shift: add to selection.
- Option-drag a note: duplicate; selected notes duplicate together.
- Command while editing: bypass grid snapping.
- Command+Shift-drag a note up/down: edit velocity.
- Command+scroll: horizontal zoom. Option+scroll: vertical zoom.
- Trackpad pinch: horizontal zoom.
- Click a pitch label: select all notes of that pitch. Sweep labels: select a pitch range.
- Drag the separate left gutter vertically: scroll pitches; horizontally: zoom pitches.
- Drag the divider above the sequencer to change its height; double-click it to reset.

## Waveform
- Option-click in Slice: add a slice. Option+Shift-click: place the FX anchor.
- Command: bypass snapping. Shift: fine adjustment where supported.
- Stretch: drag to select a region; Shift-drag selects finely. Command+Shift-click mutes/unmutes the selection.
- One Shot: sample START/END handles are at the top, loop handles at the bottom. Drag inside the loop to move both loop markers. Command bypasses Zero Snap.
- Track anchor strip: Option-drag an anchor to adjust it. Double-click adds/removes anchors.

## Browser and dialogs
- Folder browser: Up/Down selects and browses folders. Shift/Command preserves deliberate multiple selection.
- Command+C / X / V in the folder browser: Copy / Cut / Paste within Slice Groups.
- Right-click file headers: choose columns. Drag headers to reorder, or their edges to resize.
- Search-tag text: Include/Exclude menu. Only × removes the search chip.
- Warning dialogs: Tab/Shift+Tab changes the focused option; Enter or Space activates it; Escape cancels. Cancel is initially focused.
- Save Preset: Save As dialog. Option-click Save Preset: overwrite the current preset, or Save As if it has no destination.

Gestures apply to the control under the pointer. Settings → Show Tooltip Hints provides contextual help.
</pre></html>)SGDOC";
inline constexpr const char* quickstart=R"SGDOC(<!doctype html><html><meta charset="utf-8"><title>Slice Groups Quickstart</title><style>body{font:17px system-ui;max-width:850px;margin:40px auto;padding:0 24px;line-height:1.6;background:#222;color:#eee}pre{white-space:pre-wrap;font:inherit}</style><pre># Slice Groups Quickstart

## Browse and play
Add your sample folders with Add Folder. Select a folder to browse its contents recursively. Search by keywords and tags; combine tags with AND/OR, use favourites, playback types or colour filters. Back restores a previous search. Parent moves up without clearing search terms. Show in Library locates the loaded file.

Auto Load (A) loads files as you select them. Turn it off to edit metadata without changing the playing sound. Previous/Next follows the current sorted and filtered list. Date Added sorts by the macOS arrival date where available; — means macOS did not supply it. It is distinct from creation date.

## Choose a playback mode
- Slice: detect transients, edit slice positions and adjust quantise/swing. MIDI notes trigger slices.
- Melodic Slice: inspect slice pitches, set silent slices to no note, then Create Sequence. Zones edits the generated Multi mapping.
- Stretch: align loops to project tempo. Set Bars and BPM, then trim/loop as needed.
- Warp: edit timing markers and use optional tempo sync.
- Track: analyse a full track, choose Straight/Dynamic timing, beat-jump, loop and optionally use stems. HQ export and playback quality are separate controls.
- One Shot: play pitched samples with envelope, Mono/Poly, looping and glide controls.
- Multi: load SFZ or native Multi instruments and edit key/velocity zones. Mono+Legato retains layers while held notes overlap.

Playback in the plug-in follows the DAW transport; standalone also provides its own audio/MIDI setup.

## Sequence
Open Sequencer to import MIDI from the browser or Import MIDI, record incoming MIDI, or draw notes with the pencil tool. Use Bars/Beats and loop handles to set the active region. Crop Clip trims the editable clip to that region. Quantise and Swing shape timing. Use Fold Notes and zoom for compact editing.

Clip Key describes the original melody. Key Lock transposes playback to the target without changing editable notes; Master Key overrides the local destination. Ordinary Slice trigger numbers stay unchanged. MIDI Out sends the sequence to the host for routing to another instrument. MIDI exports include effective melodic transposition.

Lock Sequence lets you browse instruments while retaining the melody. Extract MIDI converts melodic audio; choose an audition One Shot/Multi, compare Audio/MIDI, and optionally make that sound the default.

## Auto Continue and arrangement
AUTO CONT. advances through eligible files in the current browser order. Audio uses the selected region and Bar Lock; MIDI uses each clip’s saved loop region. No wrapping to the first file occurs at the end of the list. You can scroll freely; newly loaded files are revealed once.

FX Anchor aligns a point inside the sample (Anchor Bar) to configured Project Bars. A four-bar sample ends at Anchor Bar 5. Option+Shift-click places the anchor without turning it on. MIDI Gate controls audibility while transport runs; its button shows the group’s trigger note. Arrangement switches and Key Lock/target settings belong to the Ableton Set.

## Save and share
Save Loop/One Shot/Track stores per-file edits and metadata. Enable Auto Save to persist supported edits; writable MIDI clips also autosave. Save/Save As in Sequencer writes MIDI; unsaved sequences remain in the Set until given a destination. Multi saves use .sgmulti, preserving the imported SFZ.

Click Save Preset to choose a portable .sgpreset destination. Option-click overwrites the current preset in the background. Presets contain all loaded source audio from the eight groups and retain mappings and sequences. Ableton Sets also embed loaded audio automatically. Save an existing Set again with this version to make it portable; your collaborator needs a compatible Slice Groups installation.

Library sharing shares published metadata through the configured shared library. Favourites are personal. Copy to My Library makes an independent editable copy. Use Relocate Library when moving a library root; folder Rename/Move inside Slice Groups preserves metadata.

## Phone companion and help
Settings → Phone Remote opens the local companion setup. Connect the phone to the same local network and open the pairing link shown there. Choose visible instances/groups; use Favourite and Previous/Next remotely. Keep the Mac awake and the audio host running. Do not share the private pairing link publicly.

Settings → Keyboard Shortcuts configures actions. Settings → Hotkeys opens the gesture reference. A version label remains visible at the bottom-right. Use Check for Updates for available downloads; close Ableton and standalone before installing an update.
</pre></html>)SGDOC";
}