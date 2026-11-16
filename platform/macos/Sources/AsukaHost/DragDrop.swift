// Drag and drop. Drops come to AsukaView as an NSDraggingDestination:
// AppKit gives a drag to the innermost view registered for its types, and
// doesn't pass it on if that view refuses it, so the innermost target above
// it that takes the drag's types (DropAccept's MIME patterns) gets it here;
// DragOver follows it, Drop has its items (ops/item.go's encoding): another
// app's files, file promises (received into the caches), image data
// (written there), links and texts, or the app's own drag's source. Drags
// go from AsukaView as an NSDraggingSource (DragItems): other apps get each
// item's text, link or file, the app's own drop targets its source view in
// the private type; Dragging reports Began, then Dropped or Cancelled.
// Ports host/darwin's interaction.go.
import AppKit
import UniformTypeIdentifiers
#if canImport(AsukaOps)
import AsukaOps
#endif

/// The private pasteboard type of the app's own drags: its process's ID
/// and the source view's, "pid:id".
let asukaDragType = NSPasteboard.PasteboardType("dev.asuka.drag")

/// A thing shared or dragged (ops.Item): a text, a link or a file; an item
/// of a Drop event, or of a view's DragItems.
struct AsukaItem {
    var kind: Int32 = 0 // ShareKind
    var text = "" // a text's text, a link's URL
    var path = "" // a file's
    var name = "" // the file's name where it came from
    var mime = ""
    var size: Int64 = -1 // Drop: the file's size in bytes, -1 if unknown
    /// A DragItems item that carries an app's value, which only the app's
    /// own drops get.
    var value = false

    /// DragItems: u32 count, then per item u8 ShareKind, string text,
    /// string path, string name, string MIME type, u8 flags (1: Value); a
    /// short list ends where it's cut.
    static func decodeDragItems(_ b: [UInt8]) -> [AsukaItem] {
        var r = AsukaByteReader(b)
        let n = r.u32()
        var out: [AsukaItem] = []
        for _ in 0..<n {
            var it = AsukaItem(kind: Int32(r.u8()))
            it.text = r.string()
            it.path = r.string()
            it.name = r.string()
            it.mime = r.string()
            it.value = r.u8() & 1 != 0
            if r.bad { break }
            out.append(it)
        }
        return out
    }
}

/// A Drop event's payload: f32 x, y, u32 source, u8 flags (1: Copied),
/// then the items: u32 count, per item u8 ShareKind, string text, string
/// path, string name, string MIME type, i64 size.
func asukaDropPayload(x: CGFloat, y: CGFloat, source: UInt32, copied: Bool, items: [AsukaItem]) -> [UInt8] {
    var w = AsukaByteWriter()
    w.f32(Float(x))
    w.f32(Float(y))
    w.u32(source)
    w.u8(copied ? 1 : 0)
    w.u32(UInt32(items.count))
    for it in items {
        w.u8(UInt8(truncatingIfNeeded: it.kind))
        w.string(it.text)
        w.string(it.path)
        w.string(it.name)
        w.string(it.mime)
        w.i64(it.size)
    }
    return w.bytes
}

// MIME types: asukaMatchMIME (ops.MatchMIME), asukaUTIMIME, asukaUTIExt,
// asukaIsImageType, asukaURLMIME and asukaFileKind are TextInput.swift's
// (a paste reads the pasteboard as a drop does).

/// A way to read a dragged item: as a file, a file promise, image data, a
/// link or a text.
struct AsukaRep {
    enum Kind { case file, promise, image, link, text }
    var kind: Kind
    var mime: String
    var pbType: NSPasteboard.PasteboardType? = nil // image data's, a text's
    var path = "" // a file's
    var promise: NSFilePromiseReceiver? = nil
    var item: NSPasteboardItem
    var linkText = false // a link, read as text
}

/// The drag over the app's drop targets.
struct AsukaDropState {
    /// The drag's sequence number, and whether its items' types are read.
    var seq = 0
    var read = false
    /// Its source view, for the app's own drag.
    var local: UInt32 = 0
    /// Per item, the types it can be read as.
    var mimes: [[String]] = []
    /// The drop target it's over, and where, last reported.
    var target: AsukaNode?
    var at = CGPoint.zero
}

/// The app's drag under way.
@MainActor final class AsukaDragSource {
    let id: UInt32
    /// The session's source, until it ends.
    let view: NSView
    var items: [AsukaItem]
    var began = false

    init(id: UInt32, view: NSView, items: [AsukaItem]) {
        self.id = id
        self.view = view
        self.items = items
    }
}

/// A dropped item being read; copied: its file is the app's copy.
struct AsukaDropItem {
    var item: AsukaItem
    var copied = false
    var ok = true
}

@MainActor extension AsukaRuntime {
    // MARK: Drop targets

    /// The types a drop target takes: files, file promises, links, texts,
    /// images, and the app's own drags.
    var asukaDragTypes: [NSPasteboard.PasteboardType] {
        var t: [NSPasteboard.PasteboardType] = [.fileURL, .URL, .string, asukaDragType]
        t += NSImage.imageTypes.map { NSPasteboard.PasteboardType($0) }
        t += NSFilePromiseReceiver.readableDraggedTypes.map { NSPasteboard.PasteboardType($0) }
        return t
    }

    func setDropAccept(_ n: AsukaNode, _ b: [UInt8]) {
        let x = ix(n)
        x.dropAccept = asukaStrings(b)
        if x.dropAccept.isEmpty { n.view?.unregisterDraggedTypes() } else { n.view?.registerForDraggedTypes(asukaDragTypes) }
    }

    func setDragItems(_ n: AsukaNode, _ b: [UInt8]) {
        ix(n).dragItems = AsukaItem.decodeDragItems(b)
    }

    /// The drag's types, read once per drag; a new drag ends the last one's
    /// time over a target.
    func dropSession(_ info: NSDraggingInfo) -> AsukaDropState {
        let seq = info.draggingSequenceNumber
        if visual.over.read && visual.over.seq == seq { return visual.over }
        leaveTarget()
        var d = AsukaDropState(seq: seq, read: true)
        let pb = info.draggingPasteboard
        if let src = localSource(pb) {
            d.local = src
            var items: [AsukaItem] = []
            if let drag = visual.drag, drag.id == src { items = drag.items } else if let n = nodes[src] { items = n.interaction?.dragItems ?? [] }
            d.mimes = items.map { [$0.mime] }
        } else {
            d.mimes = readDrag(pb).map { $0.map { $0.mime } }
        }
        visual.over = d
        return d
    }

    /// The source view of a drag from this process.
    func localSource(_ pb: NSPasteboard) -> UInt32? {
        guard let s = pb.string(forType: asukaDragType) else { return nil }
        let parts = s.split(separator: ":", maxSplits: 1)
        guard parts.count == 2, parts[0] == "\(getpid())", let id = UInt32(parts[1]), id != 0 else { return nil }
        return id
    }

    /// Another app's dragged items, each the ways it can be read, the best
    /// first: a file, a file promise, image data, a link, a text.
    func readDrag(_ pb: NSPasteboard) -> [[AsukaRep]] {
        var promises = (pb.readObjects(forClasses: [NSFilePromiseReceiver.self], options: nil) as? [NSFilePromiseReceiver]) ?? []
        let promiseTypes = Set(NSFilePromiseReceiver.readableDraggedTypes)
        var out: [[AsukaRep]] = []
        for item in pb.pasteboardItems ?? [] {
            let order = item.types
            let types = Set(order.map { $0.rawValue })
            var reps: [AsukaRep] = []
            if types.contains(NSPasteboard.PasteboardType.fileURL.rawValue) {
                if let s = item.string(forType: .fileURL), let u = URL(string: s), u.isFileURL {
                    let path = (u as NSURL).filePathURL?.path ?? u.path
                    reps.append(AsukaRep(kind: .file, mime: asukaURLMIME(u, path), path: path, item: item))
                }
                out.append(reps)
                continue
            }
            if order.contains(where: { promiseTypes.contains($0.rawValue) }), !promises.isEmpty {
                let p = promises.removeFirst()
                let m = p.fileTypes.first.map { asukaUTIMIME($0) } ?? "application/octet-stream"
                reps.append(AsukaRep(kind: .promise, mime: m, promise: p, item: item))
            }
            if let t = order.first(where: { asukaIsImageType($0.rawValue) }) {
                reps.append(AsukaRep(kind: .image, mime: asukaUTIMIME(t.rawValue), pbType: t, item: item))
            }
            if types.contains(NSPasteboard.PasteboardType.URL.rawValue) {
                reps.append(AsukaRep(kind: .link, mime: "text/uri-list", pbType: .URL, item: item))
            }
            if types.contains(NSPasteboard.PasteboardType.string.rawValue) {
                reps.append(AsukaRep(kind: .text, mime: "text/plain", pbType: .string, item: item))
            } else if types.contains(NSPasteboard.PasteboardType.URL.rawValue) {
                reps.append(AsukaRep(kind: .link, mime: "text/plain", pbType: .URL, item: item, linkText: true))
            }
            if !reps.isEmpty { out.append(reps) }
        }
        return out
    }

    /// Whether n takes a drag of items of these types.
    func takes(_ n: AsukaNode, _ mimes: [[String]]) -> Bool {
        guard let accept = n.interaction?.dropAccept else { return false }
        return mimes.contains { $0.contains { asukaMatchMIME(accept, $0) } }
    }

    /// The innermost drop target from v up that takes the drag.
    func dropTarget(_ v: NSView, _ d: AsukaDropState) -> AsukaNode? {
        var at: NSView? = v
        while let x = at {
            if let n = node(of: x), !(n.interaction?.dropAccept.isEmpty ?? true), takes(n, d.mimes), !x.isHiddenOrHasHiddenAncestor { return n }
            at = x.superview
        }
        return nil
    }

    /// draggingEntered and draggingUpdated on v.
    func dragOver(_ v: NSView, _ info: NSDraggingInfo) -> NSDragOperation {
        let d = dropSession(info)
        let t = dropTarget(v, d)
        let loc = info.draggingLocation
        if t !== d.target {
            leaveTarget()
            if let t {
                visual.over.target = t
                visual.over.at = loc
                emitDragOver(t, .began, loc)
            }
        } else if let t, loc != d.at {
            visual.over.at = loc
            emitDragOver(t, .changed, loc)
        }
        guard t != nil else { return [] }
        return asukaDragOperation(d.local != 0, info.draggingSourceOperationMask)
    }

    /// draggingExited on v. AppKit sends it as the drag goes to another
    /// view, before that view's draggingEntered; a drag going from a
    /// target's view into one of its descendants that hands it on to the
    /// same target stays over it.
    func dragExited(_ v: NSView, _ info: NSDraggingInfo?) {
        let d = visual.over
        guard let info, d.read, d.seq == info.draggingSequenceNumber, let target = d.target else { return }
        if let frame = v.window?.contentView?.superview {
            var at = frame.hitTest(info.draggingLocation)
            while let x = at {
                at = x.superview
                if x.registeredDraggedTypes.isEmpty { continue }
                if node(of: x) != nil && dropTarget(x, d) === target { return }
                break
            }
        }
        leaveTarget()
    }

    /// Reports the drag leaving the target it's over.
    func leaveTarget() {
        guard let t = visual.over.target else { return }
        visual.over.target = nil
        if live(t) { emitDragOver(t, .cancelled, visual.over.at) }
    }

    func emitDragOver(_ t: AsukaNode, _ s: AsukaGestureState, _ loc: CGPoint) {
        guard let v = t.view else { return }
        let p = v.convert(loc, from: nil)
        var g = AsukaGesture()
        g.state = s.rawValue
        g.x = Float(p.x)
        g.y = Float(p.y)
        g.scale = 1
        emit(t, .dragOver) { AsukaEvents.dragOver($0, g) }
    }

    func prepareDrop(_ v: NSView, _ info: NSDraggingInfo) -> Bool {
        let d = dropSession(info)
        return d.target != nil || dropTarget(v, d) != nil
    }

    func draggingEnded(_ v: NSView, _ info: NSDraggingInfo) {
        if visual.over.read && visual.over.seq == info.draggingSequenceNumber { leaveTarget() }
    }

    /// performDragOperation on v: the app's own drag drops at once, its
    /// items the core's; another app's items the target takes are read
    /// (file promises received into the caches) and then dropped.
    func performDrop(_ v: NSView, _ info: NSDraggingInfo) -> Bool {
        let d = dropSession(info)
        guard let t = d.target ?? dropTarget(v, d), live(t), let tv = t.view else { return false }
        let p = tv.convert(info.draggingLocation, from: nil)
        visual.over.target = t
        if d.local != 0 {
            let b = asukaDropPayload(x: p.x, y: p.y, source: d.local, copied: false, items: [])
            emit(t, .drop) { AsukaEvents.drop($0, b) }
            leaveTarget()
            return true
        }
        let accept = t.interaction?.dropAccept ?? []
        var (items, promised) = readItems(info.draggingPasteboard, accept)
        guard !items.isEmpty else {
            leaveTarget()
            return false
        }
        if promised.isEmpty {
            let (out, copied) = finishItems(items)
            let b = asukaDropPayload(x: p.x, y: p.y, source: 0, copied: copied, items: out)
            emit(t, .drop) { AsukaEvents.drop($0, b) }
            leaveTarget()
            return true
        }
        leaveTarget()
        receivePromises(t, p, &items, promised)
        return true
    }

    /// Reads the dragged items accept takes, each the first way it matches;
    /// promised are the indexes of file promises still to receive.
    func readItems(_ pb: NSPasteboard, _ accept: [String]) -> ([AsukaDropItem], [(Int, NSFilePromiseReceiver)]) {
        var items: [AsukaDropItem] = []
        var promised: [(Int, NSFilePromiseReceiver)] = []
        for reps in readDrag(pb) {
            for r in reps where asukaMatchMIME(accept, r.mime) {
                var it = AsukaDropItem(item: AsukaItem(mime: r.mime))
                switch r.kind {
                case .file:
                    it.item.kind = asukaFileKind(r.mime).rawValue
                    it.item.path = r.path
                    it.item.name = (r.path as NSString).lastPathComponent
                    if let a = try? FileManager.default.attributesOfItem(atPath: r.path), let s = a[.size] as? NSNumber { it.item.size = s.int64Value }
                case .promise:
                    it.ok = false
                    if let p = r.promise { promised.append((items.count, p)) }
                case .image:
                    guard let t = r.pbType, let data = r.item.data(forType: t),
                          let path = try? writeDropFile("Image" + asukaUTIExt(t.rawValue), data) else { continue }
                    it.item.kind = AsukaShareKind.image.rawValue
                    it.item.path = path
                    it.item.name = (path as NSString).lastPathComponent
                    it.item.size = Int64(data.count)
                    it.copied = true
                case .link:
                    it.item.kind = r.linkText ? AsukaShareKind.text.rawValue : AsukaShareKind.link.rawValue
                    it.item.text = r.pbType.flatMap { r.item.string(forType: $0) } ?? ""
                case .text:
                    it.item.kind = AsukaShareKind.text.rawValue
                    it.item.text = r.pbType.flatMap { r.item.string(forType: $0) } ?? ""
                }
                items.append(it)
                break
            }
        }
        return (items, promised)
    }

    /// The items read, and whether their files are all the app's copies
    /// (only then may the app remove them).
    func finishItems(_ items: [AsukaDropItem]) -> ([AsukaItem], Bool) {
        var out: [AsukaItem] = []
        var files = 0, copies = 0
        for it in items where it.ok {
            if !it.item.path.isEmpty {
                files += 1
                if it.copied { copies += 1 }
            }
            out.append(it.item)
        }
        return (out, files > 0 && copies == files)
    }

    /// Receives the file promises into the caches, then drops the items on
    /// t.
    func receivePromises(_ t: AsukaNode, _ p: CGPoint, _ items: inout [AsukaDropItem], _ promised: [(Int, NSFilePromiseReceiver)]) {
        final class Box { var items: [AsukaDropItem] = []; var pending = 0 }
        let box = Box()
        box.items = items
        let drop = { [weak self] in
            guard let self, self.live(t) else { return }
            let (out, copied) = self.finishItems(box.items)
            guard !out.isEmpty else { return }
            let b = asukaDropPayload(x: p.x, y: p.y, source: 0, copied: copied, items: out)
            self.emit(t, .drop) { AsukaEvents.drop($0, b) }
        }
        guard let dir = try? dropDir() else {
            drop()
            return
        }
        box.pending = promised.count
        for (index, receiver) in promised {
            let files = max(receiver.fileTypes.count, 1)
            var got = 0
            receiver.receivePromisedFiles(atDestination: dir, options: [:], operationQueue: .main) { url, err in
                MainActor.assumeIsolated {
                    if err == nil, !box.items[index].ok {
                        let path = url.path
                        var it = box.items[index]
                        it.item.path = path
                        it.item.name = url.lastPathComponent
                        it.item.mime = asukaURLMIME(url, path)
                        it.item.kind = asukaFileKind(it.item.mime).rawValue
                        if let a = try? FileManager.default.attributesOfItem(atPath: path), let s = a[.size] as? NSNumber { it.item.size = s.int64Value }
                        it.copied = true
                        it.ok = true
                        box.items[index] = it
                    }
                    got += 1
                    if got == files {
                        box.pending -= 1
                        if box.pending == 0 { drop() }
                    }
                }
            }
        }
    }

    /// A new directory in the app's caches for a drop's files.
    func dropDir() throws -> URL {
        let base = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
        visual.dropSeq += 1
        let name = "\(UInt64(Date().timeIntervalSince1970 * 1e9))-\(visual.dropSeq)"
        let dir = base.appendingPathComponent(appID).appendingPathComponent("Drops").appendingPathComponent(name)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    func writeDropFile(_ name: String, _ data: Data) throws -> String {
        let u = try dropDir().appendingPathComponent(name)
        try data.write(to: u)
        return u.path
    }

    // MARK: Drag sources

    /// Begins dragging n's items, its image under the mouse: other apps get
    /// each item's text, link or file, the app's drop targets its source
    /// view in the private type.
    func beginDrag(_ n: AsukaNode, _ e: NSEvent) {
        endDrag([]) // one that never ended
        guard let v = n.view, let items = n.interaction?.dragItems else { return }
        let b = v.bounds
        let img = asukaSnapshot(v, b)
        let own = "\(getpid()):\(n.id)"
        var dis: [NSDraggingItem] = []
        for (i, it) in items.enumerated() {
            let pbi = NSPasteboardItem()
            if !it.path.isEmpty {
                pbi.setString(URL(fileURLWithPath: it.path).absoluteString, forType: .fileURL)
            } else if it.kind == AsukaShareKind.link.rawValue && !it.text.isEmpty {
                pbi.setString(it.text, forType: .URL)
                pbi.setString(it.text, forType: .string)
            } else if !it.text.isEmpty {
                pbi.setString(it.text, forType: .string)
            }
            pbi.setString(own, forType: asukaDragType)
            let di = NSDraggingItem(pasteboardWriter: pbi)
            di.setDraggingFrame(b, contents: i == 0 ? img : nil)
            dis.append(di)
        }
        guard !dis.isEmpty else { return }
        visual.drag = AsukaDragSource(id: n.id, view: v, items: items)
        if visual.fakeSessions {
            // What AppKit gets; a session needs the window server's mouse.
            for di in dis {
                let types = (di.item as? NSPasteboardItem)?.types.map { $0.rawValue } ?? []
                visual.dragLog.append(types.joined(separator: ","))
            }
            return
        }
        guard let src = v as? NSDraggingSource else {
            endDrag([])
            return
        }
        let s = v.beginDraggingSession(with: dis, event: e, source: src)
        s.animatesToStartingPositionsOnCancelOrFail = true
    }

    /// The operations the app's drag allows: within the app a move, a copy
    /// or whatever the target does; outside it a copy, or none if every
    /// item is only the app's value.
    func dragMask(_ c: NSDraggingContext) -> NSDragOperation {
        if c != .outsideApplication { return [.move, .copy, .generic] }
        if let d = visual.drag, d.items.contains(where: { !$0.text.isEmpty || !$0.path.isEmpty }) { return .copy }
        return []
    }

    func dragBegan() {
        guard let d = visual.drag, !d.began else { return }
        d.began = true
        if let n = nodes[d.id] { emit(n, .dragging) { AsukaEvents.dragging($0, Int64(AsukaDragPhase.began.rawValue)) } }
    }

    /// Ends the app's drag: Dropped if a target took it, else Cancelled
    /// (each once, after Began).
    func endDrag(_ op: NSDragOperation) {
        guard let d = visual.drag else { return }
        visual.drag = nil
        guard d.began else { return }
        let phase = op.isEmpty ? AsukaDragPhase.cancelled : .dropped
        if let n = nodes[d.id] { emit(n, .dragging) { AsukaEvents.dragging($0, Int64(phase.rawValue)) } }
    }
}

/// The operation a drop target shows: a move for the app's own drags, a
/// copy for other apps', or what the source allows.
func asukaDragOperation(_ local: Bool, _ mask: NSDragOperation) -> NSDragOperation {
    let want: [NSDragOperation] = local ? [.move, .generic, .copy, .link] : [.copy, .generic, .move, .link]
    for op in want where mask.contains(op) { return op }
    return []
}

/// An image of a view as it shows.
@MainActor func asukaSnapshot(_ v: NSView, _ b: CGRect) -> NSImage? {
    guard b.width > 0, b.height > 0, let rep = v.bitmapImageRepForCachingDisplay(in: b) else { return nil }
    v.cacheDisplay(in: b, to: rep)
    let img = NSImage(size: b.size)
    img.addRepresentation(rep)
    return img
}
