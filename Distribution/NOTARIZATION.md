# Signed and notarised distribution

`notarize.py` packages a small native installer inside a signed DMG. It uses a Developer ID **Application** certificate; a Developer ID Installer certificate is not required because the installer is an app rather than a `.pkg`.

Requirements on the release machine:

- Xcode command-line tools and Swift compiler.
- The configured Developer ID Application certificate and private key in Keychain.
- Working notarisation credentials stored locally with `xcrun notarytool store-credentials`. Credentials are never committed or copied into the package. The script defaults to the existing `BounceLabNotary` Keychain profile; override with `--profile NAME`.
- Matching build staged by `Distribution/release.py` under `work/distribution/VERSION/download/`.

Run from the workspace root, replacing the version if needed:

```sh
python3 outputs/SliceGroups/Distribution/notarize.py prepare --version 0.38.0
python3 outputs/SliceGroups/Distribution/notarize.py submit-app --version 0.38.0
python3 outputs/SliceGroups/Distribution/notarize.py status-app --version 0.38.0
# Wait until status is Accepted. Do not resubmit a pending submission.
python3 outputs/SliceGroups/Distribution/notarize.py staple-app --version 0.38.0
python3 outputs/SliceGroups/Distribution/notarize.py make-dmg --version 0.38.0
python3 outputs/SliceGroups/Distribution/notarize.py submit-dmg --version 0.38.0
python3 outputs/SliceGroups/Distribution/notarize.py status-dmg --version 0.38.0
# Wait until Accepted, then:
python3 outputs/SliceGroups/Distribution/notarize.py finish --version 0.38.0
```

Outputs and resumable submission/status/log JSON files live in `work/distribution/VERSION/notarized/`. Both submission phases upload binaries and models to Apple. The script does not install anything or publish to GitHub. Only publish after the final Gatekeeper and staple checks succeed. The DMG is the recommended download; the ZIP contains the signed, stapled plugin for manual installation.

`Installer.swift` checks the plugin's Developer ID signature and identifier, requires confirmation that audio hosts are closed, and blocks recognised running hosts. New installs use the user's VST3 directory. Existing user or writable system installations are updated in place; duplicates are rejected. It stages and verifies the new bundle, moves the old one aside, verifies the installed bundle, and restores the previous bundle if replacement fails. A backup is retained in `~/Library/Application Support/Slice Groups Installer/Backups/` where possible. Sample libraries, tags and settings are not touched. System installations without write permission require a manual Finder copy with administrator authorisation.

The signing identity/team in the script and Swift requirement are public certificate identifiers, not credentials. A different publisher must update both. The script assumes this project's local workspace layout and should be adapted if used from a standalone source archive.
