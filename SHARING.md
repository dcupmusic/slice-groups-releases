# Dropbox library sharing

Keep library.db in Slice Groups' local application-data folder. Do not put that database in Dropbox.

1. Add a folder within your shared Dropbox location to Slice Groups.
2. Right-click that top-level library folder and choose **Share this library via Dropbox**.
3. Your collaborator adds their local copy of the same Dropbox folder in Slice Groups 0.37.0 or later.

Slice Groups publishes owner-written metadata under `.slicegroups/sharing-v2` inside the sample folder. Dropbox syncs those files alongside the audio. Paths inside the snapshot are relative, so the two computers can have different Dropbox paths. Allow Dropbox to finish syncing before expecting another computer to see an edit.

Tags, musical keys, playback types, folder colours and saved playback/timing configurations are shared. Favourites and application preferences stay on each computer. Normalisation settings are project state and are not saved with loops.

Another owner's library is read-only for metadata, Render and Trash operations in Slice Groups. Playback controls can still be adjusted temporarily. Each person can share a separate library that they own, and add the other person's library to read it.

Sharing uses local files; no Dropbox login or API integration is required. Use Dropbox itself to grant access to the folder. The live SQLite database and personal favourites are never published.

## Copy to My Library

Right-click files in the file browser or folders in Library Folders and choose **Copy to My Library...**. Select one of your editable library roots or **Choose destination folder...**. A new destination is added to your library automatically.

Copying runs in the background, including audio/support files, subfolders, tags, keys, playback types, folder colours and saved playback/timing settings. Inherited folder playback types are retained. Favourites and the original owner's sharing identity are not copied. Existing files are not overwritten; duplicate names receive a “copy” suffix. Right-click either browser and choose **Cancel library copy** to cancel. Symbolic links are refused rather than followed outside the selected folder.

The resulting copy belongs to your local library and can be tagged, edited and saved independently. It does not receive future updates from the original owner. Relative sample references inside copied SFZ folders are retained; include all referenced sample folders in your selection.

## Updates

Slice Groups checks the public GitHub release feed in the background when opened (at most once every six hours per host session). The version button changes to **UPDATE** when a newer stable release is available. Click it to download the matching Apple Silicon package, or use **Settings → Check for updates**. If the package is unavailable, it opens the release page. No account or library information is sent.

Close your audio host before installing the download. The plug-in does not replace itself while loaded. Update checks require an internet connection; failed checks do not affect playback.

## Relocating a library (0.38.6+)

Move the entire library folder in Finder, including its hidden `.slicegroups` folder when sharing is enabled. In Slice Groups, right-click its original library entry and choose **Relocate Library...**, then select the folder at its new location. Missing registered folders remain available for relinking. The same command is available under **Settings → Relocate Library...**.

This command updates references; it does not move or copy audio. Tags, favourites, playback types, keys, folder colours and saved sample/track settings move together in a database transaction. Conflicting destination metadata is rejected without a partial update. Existing loaded samples can still save through their old paths, and old project file references are redirected locally. A collaborator's library remains read-only and its owner snapshot is not modified. Search catalogues for affected roots rebuild in the background; relocation does not request whole-library audio preparation.
