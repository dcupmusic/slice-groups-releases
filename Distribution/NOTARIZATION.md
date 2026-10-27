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

## System-wide PKG installer

`package.py` builds a full installer for `/Library/Audio/Plug-Ins/VST3/Slice Groups.vst3`.
It requires macOS 14+, Apple Silicon, administrator authorization, and closed audio
hosts. A previous system plug-in is upgraded in place; user libraries and metadata
are outside the payload. The welcome page explains removing any duplicate personal
VST3 copy. It never deletes that personal copy automatically.

Use a complete, signed plug-in with all models as input:

```sh
python3 Distribution/package.py --plugin "/path/to/Slice Groups.vst3" --version 0.46.3 --output /path/to/pkg-stage --identity "Developer ID Installer: YOUR NAME (TEAMID)"
```

This needs a separate **Developer ID Installer** identity in Keychain, in addition
to the Application identity used to sign the plug-in. Without `--identity`, the
result is explicitly named `UNSIGNED.pkg` for inspection only and must not be
published. Build paths may be adapted to the source checkout.

Before publication, submit the signed `.pkg` using `xcrun notarytool submit` with
the existing Keychain profile, retain the submission ID, and wait for `Accepted`.
Do not resubmit a pending submission. Then run `xcrun stapler staple`,
`xcrun stapler validate`, `pkgutil --check-signature`, and
`spctl --assess --type install --verbose=2` on the final PKG. Calculate release
checksums only after stapling. Never include private keys or signing credentials
in source or release assets.
