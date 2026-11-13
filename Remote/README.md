# Slice Remote — first version

An iPhone-friendly Safari/Home Screen remote for all Slice Groups instances in the same audio-host process.

1. In Slice Groups on the Mac, choose **Settings → Phone Remote…**.
2. In the page, expand **Connect your iPhone**. Open one of the displayed Mac Wi-Fi addresses on the iPhone (same local network). You can AirDrop that link from the Mac rather than type it.
3. In iPhone Safari choose **Share → Add to Home Screen**.
4. Tap **Edit** to hide/show rows or give them your own names. Each loaded group has its own Previous, Favourite and Next controls.

Favourite adds the exact file displayed when you tap, even if Auto Continue changes tracks before the command arrives. It is an add-to-favourites action, not a toggle. Previous/Next follow that group's last filtered/sorted browser list and do not wrap at its ends. Browse the group's audio or MIDI list at least once to establish its order.

Once enabled, the Mac endpoint reconnects on future host launches using a locally saved private pairing URL. Hidden-row and label choices are stored on the phone; save the Ableton Set to retain the group identities. The Mac must stay awake and on the same network. Only loaded groups are listed. Separate host processes use separate ports/pages in this first version; all instances within Ableton appear together.

No App Store, Apple notarization or iPhone development signing is required. This is a local-network web app, not a native iOS binary. Do not share the pairing URL publicly or expose the listening port to the internet.
