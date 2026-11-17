// The system's modules on a Mac that the app reaches: share's files (the
// core's ShareFiles: the sharing service picker), and the other files'
// modules: Documents.swift (compose), Periodic.swift (periodic),
// System2.swift (background, widget, liveActivity).
//
// Other system modules aren't here: their std packages have their own Swift
// (platform/device, platform/screen, platform/power, platform/appearance, platform/dialog,
// platform/shell, platform/preview, platform/print, platform/sensors, platform/secure...),
// or nothing calls them any more (the old sdk packages': secureStore,
// browser, photos, clipboard, files, network, biometric, review, displays,
// splash, tracking, integrity, ageRange, nfc).
import AppKit
import Security

/// A module method's reply.
typealias AsukaReply = (Result<Data, Error>) -> Void

@MainActor extension AsukaRuntime {
    /// The shell's system modules.
    func registerSystemModules() {
        registerModule("share", [
            "files": { [unowned self] args, reply in self.shareFiles(args, reply) },
        ])
        registerDocumentModules()
        registerModule("periodic", AsukaPeriodic.shared.methods())
        registerSystem2Modules()
    }

    /// The "share" module's "files" (args: u32 view, string MIME, string
    /// UTI, string title, u32 count, then the paths): the sharing service
    /// picker for the files, under the view (default: the window's top
    /// middle); it answers once the user picks a service or closes it.
    func shareFiles(_ args: Data, _ reply: @escaping AsukaReply) {
        guard args.count >= 20 else { return reply(.failure(AsukaModuleError("share.files: bad arguments"))) }
        var r = AsukaByteReader(args)
        let from = r.u32()
        for _ in 0..<3 { _ = r.string() } // MIME, UTI, title: a Mac's picker goes by the files
        let n = Int(r.u32())
        if r.bad { return reply(.failure(AsukaModuleError("share.files: bad arguments"))) }
        var items: [URL] = []
        for _ in 0..<n {
            let p = r.string()
            if r.bad { break }
            guard FileManager.default.fileExists(atPath: p) else { return reply(.failure(AsukaModuleError("no file at \(p)"))) }
            items.append(URL(fileURLWithPath: p))
        }
        let picker = NSSharingServicePicker(items: items)
        let d = AsukaSharePickerDelegate(reply)
        picker.delegate = d
        var v: NSView = main.content
        var rect = CGRect(x: v.bounds.midX, y: 0, width: 1, height: 1)
        if from != 0, let nv = nodes[from]?.view, nv.window != nil {
            v = nv
            rect = nv.bounds
        }
        picker.show(relativeTo: rect, of: v, preferredEdge: .minY)
    }
}

/// A file share picker on screen (ui.ShareFiles), and what it answers when
/// the user picks a service or closes it (a nil service). It keeps itself
/// until then: a picker doesn't keep its delegate.
final class AsukaSharePickerDelegate: NSObject, NSSharingServicePickerDelegate {
    private static var showing: Set<AsukaSharePickerDelegate> = []
    private var reply: AsukaReply?

    init(_ reply: @escaping AsukaReply) {
        self.reply = reply
        super.init()
        Self.showing.insert(self)
    }

    func sharingServicePicker(_ picker: NSSharingServicePicker, didChoose service: NSSharingService?) {
        let r = reply
        reply = nil
        Self.showing.remove(self)
        r?(.success(Data()))
    }
}

/// The app's folder in the user's caches, by its bundle ID or name.
@MainActor var asukaCacheDir: URL {
    let base = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask).first ?? URL(fileURLWithPath: NSTemporaryDirectory())
    return base.appendingPathComponent(Bundle.main.bundleIdentifier ?? AsukaRuntime.shared.appName, isDirectory: true)
}

/// A random file name (24 hex digits).
func asukaRandomName() -> String {
    var b = [UInt8](repeating: 0, count: 12)
    _ = SecRandomCopyBytes(kSecRandomDefault, b.count, &b)
    return b.map { String(format: "%02x", $0) }.joined()
}
