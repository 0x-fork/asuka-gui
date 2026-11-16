// asuka/shell on the Mac: NSWorkspace (Finder) and NSFileManager's trash.
// iOS has no file manager of these.
import Foundation
import Asuka
#if os(macOS)
import AppKit

final class Workspace: Desktop {
    func reveal(path: String) async throws {
        try exists(path, link: true)
        await MainActor.run { NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: path)]) }
    }

    func open(path: String) async throws {
        try exists(path, link: false)
        let u = URL(fileURLWithPath: path)
        let opened = await MainActor.run { () -> Bool? in
            guard NSWorkspace.shared.urlForApplication(toOpen: u) != nil else { return nil }
            return NSWorkspace.shared.open(u)
        }
        guard let opened else { throw ShellError.noApp }
        if !opened { throw Self.error("couldn't open \(path)") }
    }

    func trash(path: String) async throws {
        try exists(path, link: true)
        try FileManager.default.trashItem(at: URL(fileURLWithPath: path), resultingItemURL: nil)
    }

    func beep() throws { NSSound.beep() }

    func exists(_ path: String, link: Bool) throws {
        let there = link ? (try? FileManager.default.attributesOfItem(atPath: path)) != nil : FileManager.default.fileExists(atPath: path)
        if !there { throw Self.error("no file at \(path)") }
    }

    static func error(_ m: String) -> Error { NSError(domain: "shell", code: 0, userInfo: [NSLocalizedDescriptionKey: m]) }
}

func makeDesktop() throws -> any Desktop { Workspace() }
#else
func makeDesktop() throws -> any Desktop {
    throw AsukaNativeError.unsupported("shell: iOS has no file manager to show files in")
}
#endif
