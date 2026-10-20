# Dropbox library sharing

Keep library.db in Slice Groups' local application-data folder. Do not put that database in Dropbox.

1. Add a folder within your shared Dropbox location to Slice Groups.
2. Right-click that top-level library folder and choose **Share this library via Dropbox**.
3. Your collaborator adds their local copy of the same Dropbox folder in Slice Groups 0.37.0 or later.

Slice Groups publishes owner-written metadata under `.slicegroups/sharing-v2` inside the sample folder. Dropbox syncs those files alongside the audio. Paths inside the snapshot are relative, so the two computers can have different Dropbox paths. Allow Dropbox to finish syncing before expecting another computer to see an edit.

Tags, musical keys, playback types, folder colours and saved playback/timing configurations are shared. Favourites and application preferences stay on each computer. Normalisation settings are project state and are not saved with loops.

Another owner's library is read-only for metadata, Render and Trash operations in Slice Groups. Playback controls can still be adjusted temporarily. Each person can share a separate library that they own, and add the other person's library to read it.

Sharing uses local files; no Dropbox login or API integration is required. Use Dropbox itself to grant access to the folder. The live SQLite database and personal favourites are never published.
