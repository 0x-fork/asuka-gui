// asuka/dialog on the Mac: NSOpenPanel and NSSavePanel, as sheets on the
// window the user works in (or panels of their own, in front, with no
// window shown). iOS has no such dialogs: Asuka's side gives open to the
// document picker, and save and chooseFolder are unsupported.
import Foundation
import Asuka
#if os(macOS)
import AppKit
import UniformTypeIdentifiers

final class Panels: Chooser {
    func open(r: Request) async throws -> [String] { await show(r, kind: .open) }
    func save(r: Request) async throws -> String? { await show(r, kind: .save).first }
    func folder(r: Request) async throws -> [String] { await show(r, kind: .folder) }

    enum Kind { case open, save, folder }

    @MainActor func show(_ r: Request, kind: Kind) async -> [String] {
        let p: NSSavePanel
        if kind == .save {
            p = NSSavePanel()
        } else {
            let o = NSOpenPanel()
            o.canChooseFiles = kind == .open
            o.canChooseDirectories = kind == .folder
            o.allowsMultipleSelection = r.multiple
            o.resolvesAliases = true
            p = o
        }
        // A sheet has no title bar: the title is the panel's message too.
        if !r.title.isEmpty {
            p.title = r.title
            p.message = r.title
        }
        if !r.button.isEmpty { p.prompt = r.button }
        p.showsHiddenFiles = r.showHidden
        p.canCreateDirectories = kind != .open
        var (dir, name) = Self.startIn(r.start, folder: kind == .folder)
        var isDir: ObjCBool = false
        if kind == .open, FileManager.default.fileExists(atPath: r.start, isDirectory: &isDir), !isDir.boolValue {
            // A file's URL: the panel shows its folder with it selected.
            p.directoryURL = URL(fileURLWithPath: r.start)
        } else if !dir.isEmpty {
            p.directoryURL = URL(fileURLWithPath: dir, isDirectory: true)
        }
        if !r.name.isEmpty { name = r.name }
        if kind == .save, !name.isEmpty { p.nameFieldStringValue = name }
        if kind != .folder {
            var types: [UTType] = []
            for f in r.filters {
                guard let ts = Self.contentTypes(f) else { types = []; break } // a filter of any file
                types += ts
            }
            if !types.isEmpty { p.allowedContentTypes = types }
        }
        dir = ""
        let urls: [URL] = await withCheckedContinuation { c in
            let finish: (NSApplication.ModalResponse) -> Void = { resp in
                guard resp == .OK else { c.resume(returning: []); return }
                if let o = p as? NSOpenPanel { c.resume(returning: o.urls) } else { c.resume(returning: p.url.map { [$0] } ?? []) }
            }
            var w = AsukaNative.window
            while let s = w?.attachedSheet { w = s }
            if let w, w.isVisible {
                p.beginSheetModal(for: w, completionHandler: finish)
            } else {
                NSApp.activate(ignoringOtherApps: true)
                p.begin(completionHandler: finish)
            }
        }
        return urls.map(\.path)
    }

    /// The folder a dialog starts in, and the name it suggests, from a
    /// path: a folder, or a file (in a folder that's there).
    static func startIn(_ start: String, folder: Bool) -> (String, String) {
        if start.isEmpty { return ("", "") }
        var isDir: ObjCBool = false
        if FileManager.default.fileExists(atPath: start, isDirectory: &isDir), isDir.boolValue { return (start, "") }
        let u = URL(fileURLWithPath: start)
        return (u.deletingLastPathComponent().path, folder ? "" : u.lastPathComponent)
    }

    /// A filter's UTTypes ("image/*" the family's); nil for any file.
    static func contentTypes(_ f: Filter) -> [UTType]? {
        var ts: [UTType] = []
        for m in f.types {
            switch m {
            case "*/*", "*": return nil
            case "image/*": ts.append(.image)
            case "video/*": ts.append(.movie)
            case "audio/*": ts.append(.audio)
            case "text/*": ts.append(.text)
            case "font/*": ts.append(.font)
            case "application/*": ts.append(.data)
            default: if let t = UTType(mimeType: m) { ts.append(t) }
            }
        }
        for e in f.extensions {
            if e == "*" { return nil }
            // An extension no app declares is a dynamic type of its own.
            if let t = UTType(filenameExtension: e) { ts.append(t) }
        }
        return ts
    }
}

func makeChooser() throws -> any Chooser { Panels() }
#else
func makeChooser() throws -> any Chooser {
    throw AsukaNativeError.unsupported("dialog: iOS has no file dialogs (open is the document picker)")
}
#endif
