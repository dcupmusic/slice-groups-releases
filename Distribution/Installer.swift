import AppKit
import Security

// User-level installer: no shell commands, privilege escalation, telemetry or
// changes to sample libraries. Existing installations are backed up before replacing.
struct PluginInstaller {
    static let fm = FileManager.default
    static let pluginName = "Slice Groups.vst3"
    static let team = "Q5YNM8U8JB"
    static func verify(_ url: URL) throws {
        var code: SecStaticCode?
        let created = SecStaticCodeCreateWithPath(url as CFURL, [], &code)
        guard created == errSecSuccess, let code = code else { throw problem("Cannot read the plug-in signature.") }
        var requirement: SecRequirement?
        let rule = "anchor apple generic and certificate leaf[subject.OU] = \"\(team)\" and identifier \"audio.slicegroups.plugin\""
        guard SecRequirementCreateWithString(rule as CFString, [], &requirement) == errSecSuccess else { throw problem("Cannot check the developer identity.") }
        var error: Unmanaged<CFError>?
        let flags = SecCSFlags(rawValue: kSecCSCheckNestedCode | kSecCSStrictValidate)
        guard SecStaticCodeCheckValidityWithErrors(code, flags, requirement, &error) == errSecSuccess else {
            let detail = error?.takeRetainedValue().localizedDescription ?? "The download may be incomplete."
            throw problem("The plug-in signature could not be verified. \(detail)")
        }
    }
    static func problem(_ text: String) -> NSError { NSError(domain: "audio.slicegroups.installer", code: 1, userInfo: [NSLocalizedDescriptionKey: text]) }
    static func destination() throws -> URL {
        let home = fm.homeDirectoryForCurrentUser.appendingPathComponent("Library/Audio/Plug-Ins/VST3/\(pluginName)")
        let system = URL(fileURLWithPath: "/Library/Audio/Plug-Ins/VST3/\(pluginName)")
        let existing = [home, system].filter { fm.fileExists(atPath: $0.path) }
        guard existing.count < 2 else { throw problem("Slice Groups exists in both your user and system VST3 folders. Remove the duplicate in Finder before installing, so Ableton loads the correct version.\n\n\(home.path)\n\(system.path)") }
        let target = existing.first ?? home
        if target == system && !fm.isWritableFile(atPath: target.deletingLastPathComponent().path) {
            throw problem("The existing system-wide plug-in needs administrator access. In Finder, replace it manually using the plug-in inside this installer's Contents/PlugIns folder:\n\n\(system.path)")
        }
        return target
    }
    static func install(source: URL, target: URL, backups: URL) throws -> URL? {
        try verify(source)
        let parent = target.deletingLastPathComponent()
        try fm.createDirectory(at: parent, withIntermediateDirectories: true)
        guard fm.isWritableFile(atPath: parent.path) else { throw problem("The VST3 folder is not writable: \(parent.path)") }
        let stage = parent.appendingPathComponent(".SliceGroups-install-\(UUID().uuidString).vst3")
        let backup = parent.appendingPathComponent(".SliceGroups-previous-\(UUID().uuidString)")
        var previous = false, installed = false
        defer { try? fm.removeItem(at: stage) }
        do {
            try fm.copyItem(at: source, to: stage)
            try verify(stage)
            if fm.fileExists(atPath: target.path) { try fm.moveItem(at: target, to: backup); previous = true }
            try fm.moveItem(at: stage, to: target); installed = true
            try verify(target)
        } catch {
            if installed { try? fm.removeItem(at: target) }
            if previous {
                do { try fm.moveItem(at: backup, to: target) }
                catch { throw problem("Installation failed. Your previous version is safe at:\n\(backup.path)\n\nMove it back in Finder.") }
            }
            throw error
        }
        if previous {
            let saved = backups.appendingPathComponent(UUID().uuidString).appendingPathComponent(pluginName)
            do { try fm.createDirectory(at: saved.deletingLastPathComponent(), withIntermediateDirectories: true); try fm.moveItem(at: backup, to: saved); return saved }
            catch { return backup } // Keep the backup safe if it cannot be relocated.
        }
        return nil
    }
    static func openHosts() -> [String] {
        let ids = ["com.ableton.live", "com.apple.logic10", "com.cockos.reaper", "com.presonus.studioone2", "com.bitwig.BitwigStudio", "com.image-line.flstudio"]
        return NSWorkspace.shared.runningApplications.filter {
            ids.contains($0.bundleIdentifier ?? "") || ["Live", "REAPER", "BitwigStudio", "FL Studio", "Logic Pro"].contains($0.executableURL?.lastPathComponent ?? "")
        }.compactMap { $0.localizedName }
    }
}

#if INSTALLER_TEST
let args = CommandLine.arguments
if args.count != 4 { exit(2) }
do {
    let backup = try PluginInstaller.install(source: URL(fileURLWithPath: args[1]), target: URL(fileURLWithPath: args[2]), backups: URL(fileURLWithPath: args[3]))
    print("Installed and verified; backup: \(backup?.path ?? "none")")
} catch { fputs("\(error.localizedDescription)\n", stderr); exit(1) }
#else
final class InstallerDelegate: NSObject, NSApplicationDelegate {
    weak var installButton: NSButton?
    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.activate(ignoringOtherApps: true)
        let payload = Bundle.main.bundleURL.appendingPathComponent("Contents/PlugIns/\(PluginInstaller.pluginName)")
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
        do {
            try PluginInstaller.verify(payload)
            let target = try PluginInstaller.destination()
            let alert = NSAlert()
            alert.messageText = "Install Slice Groups \(version)"
            alert.informativeText = "Close Ableton and any other audio host before installing.\n\nInstall location:\n\(target.path)\n\nYour samples, tags and settings will stay unchanged. Any existing plug-in will be backed up."
            alert.addButton(withTitle: "Install")
            alert.addButton(withTitle: "Cancel")
            alert.showsSuppressionButton = true
            alert.suppressionButton?.title = "I have closed my audio host"
            alert.buttons[0].isEnabled = false
            installButton = alert.buttons[0]
            alert.suppressionButton?.target = self
            alert.suppressionButton?.action = #selector(confirmClosed(_:))
            guard alert.runModal() == .alertFirstButtonReturn else { NSApp.terminate(nil); return }
            let hosts = PluginInstaller.openHosts()
            guard hosts.isEmpty else { throw PluginInstaller.problem("Please close \(hosts.joined(separator: ", ")), then run the installer again.") }
            let backups = PluginInstaller.fm.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support/Slice Groups Installer/Backups")
            let saved = try PluginInstaller.install(source: payload, target: target, backups: backups)
            let done = NSAlert()
            done.messageText = "Slice Groups \(version) is installed"
            done.informativeText = "Reopen your audio host and rescan VST3 plug-ins if needed.\n\n\(target.path)" + (saved == nil ? "" : "\n\nPrevious version backed up at:\n\(saved!.path)")
            done.addButton(withTitle: "Done")
            done.runModal()
        } catch {
            let alert = NSAlert(error: error)
            alert.runModal()
        }
        NSApp.terminate(nil)
    }
    @objc func confirmClosed(_ sender: NSButton) { installButton?.isEnabled = sender.state == .on }
}
if CommandLine.arguments.contains("--verify-payload") {
    do { try PluginInstaller.verify(Bundle.main.bundleURL.appendingPathComponent("Contents/PlugIns/\(PluginInstaller.pluginName)")); print("Payload signature and developer identity verified"); exit(0) }
    catch { fputs("\(error.localizedDescription)\n", stderr); exit(1) }
}
let app = NSApplication.shared
let delegate = InstallerDelegate()
app.delegate = delegate
app.setActivationPolicy(.regular)
app.run()
#endif
