// asuka/preview on iOS and the Mac: Quick Look, QLPreviewController over
// the app's screen on iOS, QLPreviewPanel on the Mac (controlled from the
// key window's responder chain, where it looks for its controller).
import Foundation
import QuickLook
import Asuka
#if canImport(UIKit)
import UIKit
#else
import AppKit
import Quartz
#endif

final class QuickLook: Viewer {
    func show(files: [String], start: Int, edit: Bool) async throws {
        var urls: [URL] = []
        for f in files { urls.append(try await AsukaNative.localFile(f)) }
        #if canImport(UIKit)
        try await withCheckedThrowingContinuation { (c: CheckedContinuation<Void, Error>) in
            Task { @MainActor in
                guard urls.contains(where: { QLPreviewController.canPreview($0 as NSURL) }) else {
                    c.resume(throwing: PreviewError.noViewer)
                    return
                }
                guard let top = AsukaNative.presenter else {
                    c.resume(throwing: AsukaNativeError.unsupported("preview: no screen to show it on"))
                    return
                }
                let p = PreviewSource(urls, edit: edit) { c.resume() }
                let vc = QLPreviewController()
                vc.dataSource = p
                vc.delegate = p
                vc.currentPreviewItemIndex = min(max(start, 0), urls.count - 1)
                top.present(vc, animated: true)
            }
        }
        #else
        await withCheckedContinuation { (c: CheckedContinuation<Void, Never>) in
            Task { @MainActor in PanelController.show(urls, start: start) { c.resume() } }
        }
        #endif
    }
}

#if canImport(UIKit)
/// The files Quick Look shows, kept while it shows them.
@MainActor final class PreviewSource: NSObject, QLPreviewControllerDataSource, QLPreviewControllerDelegate {
    private static var showing = Set<PreviewSource>()
    private let items: [URL]
    private let edit: Bool
    private let done: () -> Void

    init(_ items: [URL], edit: Bool, _ done: @escaping () -> Void) {
        self.items = items
        self.edit = edit
        self.done = done
        super.init()
        Self.showing.insert(self)
    }

    nonisolated func numberOfPreviewItems(in c: QLPreviewController) -> Int { MainActor.assumeIsolated { items.count } }

    nonisolated func previewController(_ c: QLPreviewController, previewItemAt i: Int) -> QLPreviewItem {
        MainActor.assumeIsolated { items[i] as NSURL }
    }

    /// Markup, saving into the files, if the app asked.
    nonisolated func previewController(_ c: QLPreviewController, editingModeFor item: QLPreviewItem) -> QLPreviewItemEditingMode {
        MainActor.assumeIsolated { edit ? .updateContents : .disabled }
    }

    nonisolated func previewControllerDidDismiss(_ c: QLPreviewController) {
        MainActor.assumeIsolated {
            done()
            Self.showing.remove(self)
        }
    }
}
#else
/// Quick Look's panel's controller: a responder after the key window in
/// its chain while the panel shows the files.
@MainActor final class PanelController: NSResponder, QLPreviewPanelDataSource, QLPreviewPanelDelegate {
    private static var current: PanelController?
    private let urls: [URL]
    private let start: Int
    private var done: (() -> Void)?
    private weak var window: NSWindow?

    private init(_ urls: [URL], start: Int, _ done: @escaping () -> Void) {
        self.urls = urls
        self.start = min(max(start, 0), urls.count - 1)
        self.done = done
        super.init()
    }

    required init?(coder: NSCoder) { nil }

    static func show(_ urls: [URL], start: Int, _ done: @escaping () -> Void) {
        current?.end() // what showed before: closed for these
        let c = PanelController(urls, start: start, done)
        current = c
        if let w = AsukaNative.window {
            c.window = w
            c.nextResponder = w.nextResponder
            w.nextResponder = c
        }
        guard let panel = QLPreviewPanel.shared() else { c.end(); return }
        if panel.isVisible {
            panel.updateController()
            panel.reloadData()
            panel.currentPreviewItemIndex = c.start
        } else {
            panel.updateController()
            panel.makeKeyAndOrderFront(nil)
        }
    }

    func end() {
        if let w = window, w.nextResponder === self { w.nextResponder = nextResponder }
        window = nil
        let d = done
        done = nil
        if Self.current === self { Self.current = nil }
        d?()
    }

    override func acceptsPreviewPanelControl(_ panel: QLPreviewPanel!) -> Bool { done != nil }

    override func beginPreviewPanelControl(_ panel: QLPreviewPanel!) {
        panel.dataSource = self
        panel.delegate = self
        panel.reloadData()
        panel.currentPreviewItemIndex = start
    }

    override func endPreviewPanelControl(_ panel: QLPreviewPanel!) {
        panel.dataSource = nil
        panel.delegate = nil
        end()
    }

    nonisolated func numberOfPreviewItems(in panel: QLPreviewPanel!) -> Int { MainActor.assumeIsolated { urls.count } }

    nonisolated func previewPanel(_ panel: QLPreviewPanel!, previewItemAt index: Int) -> QLPreviewItem! {
        MainActor.assumeIsolated { urls[min(max(index, 0), urls.count - 1)] as NSURL }
    }
}
#endif

func makeViewer() throws -> any Viewer { QuickLook() }
