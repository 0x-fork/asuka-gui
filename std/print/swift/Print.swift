// asuka/print on iOS and the Mac. iOS: UIPrintInteractionController, with
// WebKit's print formatter for markup and a WebView's page (paginated, as
// Safari prints), and UIPrintPageRenderer for PDFs. The Mac:
// NSPrintOperation, its panel a sheet on the window: PDFKit's for files,
// WKWebView's for markup and a WebView's page, and a save job (no panel)
// for PDFs.
import Foundation
import WebKit
import Asuka
#if canImport(UIKit)
import UIKit
#else
import AppKit
import PDFKit
#endif

final class SystemPrinter: Printer {
    func file(source: String, job: Job) async throws -> Outcome {
        let url = try await AsukaNative.localFile(source)
        #if canImport(UIKit)
        let photo = await MainActor.run { UIImage(contentsOfFile: url.path) != nil }
        guard await MainActor.run(body: { UIPrintInteractionController.canPrint(url) }) else { throw PrintError.unsupported }
        return try await present(job, fallback: url.lastPathComponent, photo: photo) { $0.printingItem = url }
        #else
        return try await MainActor.run { () -> NSPrintOperation in
            let info = Self.info(job)
            guard let doc = Self.document(url) else { throw PrintError.unsupported }
            // A PDF as it is, smaller pages fit; an image to fill the page.
            guard let op = doc.printOperation(for: info, scalingMode: url.pathExtension.lowercased() == "pdf" ? .pageScaleDownToFit : .pageScaleToFit, autoRotate: true) else {
                throw PrintError.unsupported
            }
            return op
        }.runPanel(job, fallback: url.lastPathComponent)
        #endif
    }

    func html(markup: String, job: Job) async throws -> Outcome {
        let page = try await PrintPage.load(markup, width: 612, height: 792)
        defer { Task { @MainActor in page.close() } }
        #if canImport(UIKit)
        let title = await MainActor.run { page.web.title ?? "" }
        return try await present(job, fallback: title, photo: false) { $0.printFormatter = page.web.viewPrintFormatter() }
        #else
        return await MainActor.run { () -> NSPrintOperation in
            page.operation(Self.info(job))
        }.runPanel(job, fallback: await MainActor.run { page.web.title ?? "" }, window: nil)
        #endif
    }

    func pdf(layout l: Layout) async throws -> String {
        let page = try await PrintPage.load(l.html, width: l.width - l.left - l.right, height: l.height - l.top - l.bottom)
        defer { Task { @MainActor in page.close() } }
        let title = await MainActor.run { page.web.title ?? "" }
        let url = try Self.pdfURL(title)
        #if canImport(UIKit)
        try await MainActor.run {
            let paper = CGRect(x: 0, y: 0, width: l.width, height: l.height)
            let printable = paper.inset(by: UIEdgeInsets(top: l.top, left: l.left, bottom: l.bottom, right: l.right))
            let r = UIPrintPageRenderer()
            r.addPrintFormatter(page.web.viewPrintFormatter(), startingAtPageAt: 0)
            // The renderer's paper: read-only properties, set as UIKit's
            // own print sheet sets them (what Expo and others do too).
            r.setValue(NSValue(cgRect: paper), forKey: "paperRect")
            r.setValue(NSValue(cgRect: printable), forKey: "printableRect")
            let data = NSMutableData()
            UIGraphicsBeginPDFContextToData(data, paper, [kCGPDFContextCreator as String: AsukaNative.appName])
            r.prepare(forDrawingPages: NSRange(location: 0, length: r.numberOfPages))
            let bounds = UIGraphicsGetPDFContextBounds()
            for i in 0..<max(r.numberOfPages, 1) {
                UIGraphicsBeginPDFPage()
                if i < r.numberOfPages { r.drawPage(at: i, in: bounds) }
            }
            UIGraphicsEndPDFContext()
            try (data as Data).write(to: url)
        }
        #else
        let op = await MainActor.run { () -> NSPrintOperation in
            let info = NSPrintInfo.shared.copy() as! NSPrintInfo
            info.paperSize = NSSize(width: l.width, height: l.height)
            info.orientation = .portrait // the size says it
            info.topMargin = l.top
            info.bottomMargin = l.bottom
            info.leftMargin = l.left
            info.rightMargin = l.right
            info.horizontalPagination = .fit
            info.verticalPagination = .automatic
            info.isHorizontallyCentered = false
            info.isVerticallyCentered = false
            info.jobDisposition = .save
            info.dictionary()[NSPrintInfo.AttributeKey.jobSavingURL] = url
            let op = page.operation(info)
            op.showsPrintPanel = false
            op.showsProgressPanel = false
            return op
        }
        let ok = await op.run(window: page.window)
        let size = (try? FileManager.default.attributesOfItem(atPath: url.path)[.size] as? NSNumber)?.intValue ?? 0
        guard ok, size > 0 else {
            try? FileManager.default.removeItem(at: url.deletingLastPathComponent())
            throw NSError(domain: "print", code: 0, userInfo: [NSLocalizedDescriptionKey: "print: the PDF couldn't be written"])
        }
        #endif
        return url.path
    }

    func webView(view: AsukaViewRef, job: Job) async throws -> Outcome {
        let (web, title) = try await MainActor.run { () throws -> (WKWebView, String) in
            guard let w = Self.web(in: view.view) else { throw PrintError.unsupported }
            return (w, w.title ?? "")
        }
        #if canImport(UIKit)
        return try await present(job, fallback: title, photo: false) { $0.printFormatter = web.viewPrintFormatter() }
        #else
        return await MainActor.run { () -> NSPrintOperation in
            let op = web.printOperation(with: Self.info(job))
            op.view?.frame = web.bounds
            return op
        }.runPanel(job, fallback: title, window: await MainActor.run { web.window })
        #endif
    }

    /// The WKWebView a view is, or holds.
    @MainActor static func web(in v: AsukaPlatformView?) -> WKWebView? {
        guard let v else { return nil }
        if let w = v as? WKWebView { return w }
        for s in v.subviews {
            if let w = web(in: s) { return w }
        }
        return nil
    }

    /// A new PDF's file in the caches (asuka-print/), in a folder of its
    /// own, named after the page's title, as it's shared or mailed.
    static func pdfURL(_ title: String) throws -> URL {
        let fm = FileManager.default
        let dir = fm.urls(for: .cachesDirectory, in: .userDomainMask)[0].appendingPathComponent("asuka-print/" + UUID().uuidString, isDirectory: true)
        try fm.createDirectory(at: dir, withIntermediateDirectories: true)
        var name = title.replacingOccurrences(of: "/", with: "-").replacingOccurrences(of: ":", with: "-").trimmingCharacters(in: .whitespacesAndNewlines)
        if name.isEmpty || name.hasPrefix("about-") || name.hasPrefix("data-") { name = "Document" }
        return dir.appendingPathComponent(name + ".pdf")
    }

    #if canImport(UIKit)
    /// Shows the print sheet for what set gives it; answers once it's gone.
    func present(_ job: Job, fallback: String, photo: Bool, _ set: @escaping @MainActor (UIPrintInteractionController) -> Void) async throws -> Outcome {
        try await withCheckedThrowingContinuation { (c: CheckedContinuation<Outcome, Error>) in
            Task { @MainActor in
                guard UIPrintInteractionController.isPrintingAvailable else { return c.resume(throwing: PrintError.unavailable) }
                guard let top = AsukaNative.presenter else {
                    return c.resume(throwing: AsukaNativeError.unsupported("print: no screen to show it on"))
                }
                let p = UIPrintInteractionController.shared
                let info = UIPrintInfo(dictionary: nil)
                info.jobName = !job.name.isEmpty ? job.name : !fallback.isEmpty ? fallback : AsukaNative.appName
                info.outputType = photo ? .photo : .general
                info.orientation = job.landscape ? .landscape : .portrait
                p.printInfo = info
                p.printingItem = nil
                p.printFormatter = nil
                p.showsNumberOfCopies = true
                p.showsPaperOrientation = true
                set(p)
                let done: UIPrintInteractionController.CompletionHandler = { _, completed, error in
                    if let error { c.resume(throwing: error) } else { c.resume(returning: completed ? .printed : .cancelled) }
                }
                if UIDevice.current.userInterfaceIdiom == .pad {
                    // A popover from the screen's middle.
                    let b = top.view.bounds
                    p.present(from: CGRect(x: b.midX, y: b.midY, width: 0, height: 0), in: top.view, animated: true, completionHandler: done)
                } else {
                    p.present(animated: true, completionHandler: done)
                }
            }
        }
    }
    #else
    /// The shared print info, turned as the job asks.
    @MainActor static func info(_ job: Job) -> NSPrintInfo {
        let info = NSPrintInfo.shared.copy() as! NSPrintInfo
        if job.landscape { info.orientation = .landscape }
        return info
    }

    /// A PDF as a document, or an image on a page of its size; nil if
    /// it's neither.
    @MainActor static func document(_ url: URL) -> PDFDocument? {
        if url.pathExtension.lowercased() == "pdf" { return PDFDocument(url: url) }
        guard let img = NSImage(contentsOf: url), let page = PDFPage(image: img) else { return nil }
        let doc = PDFDocument()
        doc.insert(page, at: 0)
        return doc
    }
    #endif
}

#if os(macOS)
extension NSPrintOperation {
    /// Runs the operation with its panel as a sheet on the app's window
    /// (none: on its own): the outcome.
    func runPanel(_ job: Job, fallback: String, window: NSWindow? = nil) async -> Outcome {
        await MainActor.run {
            jobTitle = !job.name.isEmpty ? job.name : !fallback.isEmpty ? fallback : AsukaNative.appName
            printPanel.options = [.showsCopies, .showsPageRange, .showsPaperSize, .showsOrientation, .showsScaling, .showsPreview]
        }
        let w = await MainActor.run { window ?? AsukaNative.window }
        return await run(window: w) ? .printed : .cancelled
    }

    /// Runs the operation, its panels on window: whether it printed.
    func run(window: NSWindow?) async -> Bool {
        await withCheckedContinuation { c in
            Task { @MainActor in
                // WebKit's print view lays its pages out from another
                // thread, while the main one answers its web process.
                self.canSpawnSeparateThread = true
                guard let window else {
                    // No window on screen: the panel on its own, app-modal.
                    c.resume(returning: self.run())
                    return
                }
                let d = PrintRunDelegate { c.resume(returning: $0) }
                self.runModal(for: window, delegate: d, didRun: #selector(PrintRunDelegate.printOperationDidRun(_:success:contextInfo:)), contextInfo: nil)
            }
        }
    }
}

/// An operation's end, kept until it's called. Made on the main thread,
/// where its end is answered.
final class PrintRunDelegate: NSObject {
    nonisolated(unsafe) private static var running = Set<PrintRunDelegate>()
    private var done: ((Bool) -> Void)?

    init(_ done: @escaping (Bool) -> Void) {
        self.done = done
        super.init()
        Self.running.insert(self)
    }

    @objc func printOperationDidRun(_ op: NSPrintOperation, success: Bool, contextInfo: UnsafeMutableRawPointer?) {
        DispatchQueue.main.async {
            self.done?(success)
            self.done = nil
            Self.running.remove(self)
        }
    }
}
#endif

/// Markup in a WKWebView that's never shown (on the Mac, in a window that
/// isn't either: printing wants one), loaded with its fonts.
@MainActor final class PrintPage: NSObject, WKNavigationDelegate {
    private static var loading = Set<PrintPage>()
    let web: WKWebView
    #if os(macOS)
    let window: NSWindow
    #endif
    private var loaded: CheckedContinuation<Void, Error>?

    private init(width: CGFloat, height: CGFloat) {
        let frame = CGRect(x: 0, y: 0, width: width, height: height)
        web = WKWebView(frame: frame, configuration: WKWebViewConfiguration())
        #if os(macOS)
        window = NSWindow(contentRect: frame, styleMask: [], backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false
        window.contentView?.addSubview(web)
        #endif
        super.init()
    }

    nonisolated static func load(_ html: String, width: CGFloat, height: CGFloat) async throws -> PrintPage {
        let p = await MainActor.run { PrintPage(width: width, height: height) }
        try await withCheckedThrowingContinuation { (c: CheckedContinuation<Void, Error>) in
            Task { @MainActor in
                loading.insert(p)
                p.loaded = c
                p.web.navigationDelegate = p
                p.web.loadHTMLString(html, baseURL: nil)
            }
        }
        return p
    }

    func webView(_ w: WKWebView, didFinish navigation: WKNavigation!) {
        // Its fonts and images, which the load doesn't wait for.
        w.evaluateJavaScript("document.fonts ? document.fonts.ready.then(() => true) : true") { [weak self] _, _ in
            MainActor.assumeIsolated { self?.finish(nil) }
        }
    }

    func webView(_ w: WKWebView, didFail navigation: WKNavigation!, withError error: Error) { finish(error) }
    func webView(_ w: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) { finish(error) }

    private func finish(_ e: Error?) {
        guard let l = loaded else { return }
        loaded = nil
        if let e { l.resume(throwing: e) } else { l.resume() }
    }

    #if os(macOS)
    /// The web view's print operation, its view the web view's size (else
    /// WebKit prints blank pages).
    func operation(_ info: NSPrintInfo) -> NSPrintOperation {
        let op = web.printOperation(with: info)
        op.view?.frame = web.bounds
        return op
    }
    #endif

    func close() {
        web.navigationDelegate = nil
        #if os(macOS)
        web.removeFromSuperview()
        window.close()
        #endif
        Self.loading.remove(self)
    }
}

func makePrinter() throws -> any Printer { SystemPrinter() }
