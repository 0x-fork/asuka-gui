// asuka/photos on a Mac, with PhotoKit as on iOS ("photoLibrary"):
// "current" and "request" answer what's allowed, "list" JSON (id, width,
// height, created in ms, screenshot), "thumbnail" a JPEG's path in the
// caches and "export" JSON (path, name). macOS has no limited library
// (nor its picker): "manage" does nothing. Before the user has allowed
// it, "list" answers none and the others "not allowed", without asking:
// request asks, with the app's NSPhotoLibraryUsageDescription.
//
// asuka/library ("library": "save"): movies and pictures go to Photos
// (after asking to add to it, NSPhotoLibraryAddUsageDescription),
// answering with the asset's identifier; downloads to ~/Downloads and
// music to ~/Music, under the folder asked, answering with the file's
// path.
import AppKit
import CryptoKit
import Photos

@MainActor final class AsukaPhotoLibrary {
    static let shared = AsukaPhotoLibrary()

    // MARK: Access

    /// PhotoKit's authorization status for an access level (the
    /// automation's seam's, under it: "photos" read and write, "photosAdd"
    /// add only).
    static func status(_ level: PHAccessLevel) -> Int {
        if let s = AsukaAccessFake.status(level == .addOnly ? "photosAdd" : "photos") { return s }
        return PHPhotoLibrary.authorizationStatus(for: level).rawValue
    }

    /// Asks the user, then calls done on the main thread with the status.
    static func request(_ level: PHAccessLevel, _ done: @escaping (Int) -> Void) {
        let name = level == .addOnly ? "photosAdd" : "photos"
        if AsukaAccessFake.request(name) {
            let s = status(level)
            DispatchQueue.main.async { done(s) }
            return
        }
        PHPhotoLibrary.requestAuthorization(for: level) { s in DispatchQueue.main.async { done(s.rawValue) } }
    }

    /// A status as asuka/photos' wire names it.
    static func access(_ s: Int) -> String {
        switch s {
        case 3: return "allowed"
        case 4: return "limited"
        case 1, 2: return "denied"
        default: return "notAsked"
        }
    }

    static func readable() -> Bool {
        let s = status(.readWrite)
        return s == 3 || s == 4
    }

    nonisolated static func asset(_ id: String) throws -> PHAsset {
        guard let a = PHAsset.fetchAssets(withLocalIdentifiers: [id], options: nil).firstObject else { throw AsukaModuleError("no such photo") }
        return a
    }

    /// A thumbnail's file name: the asset's and its edit's, hashed, and
    /// the size.
    nonisolated static func thumbName(_ id: String, _ modified: Double, _ size: Int) -> String {
        let stamp = modified == modified.rounded() && abs(modified) < 1e15 ? String(Int64(modified)) : String(modified)
        let sum = SHA256.hash(data: Data((id + stamp).utf8))
        return sum.prefix(16).map { String(format: "%02x", $0) }.joined() + "-\(size).jpg"
    }

    /// Writes an NSImage as a JPEG.
    nonisolated static func writeJPEG(_ img: NSImage, _ path: String, _ quality: Double) throws {
        guard let tiff = img.tiffRepresentation, let rep = NSBitmapImageRep(data: tiff),
              let data = rep.representation(using: .jpeg, properties: [.compressionFactor: quality]) else {
            throw AsukaModuleError("no thumbnail")
        }
        let tmp = path + ".tmp"
        try data.write(to: URL(fileURLWithPath: tmp))
        _ = try? FileManager.default.removeItem(atPath: path)
        try FileManager.default.moveItem(atPath: tmp, toPath: path)
    }

    // MARK: The module

    func methods() -> [String: AsukaModuleMethod] {
        [
            "current": { _, reply in reply(.success(Data(Self.access(Self.status(.readWrite)).utf8))) },
            "request": { _, reply in
                let s = Self.status(.readWrite)
                if s != 0 {
                    reply(.success(Data(Self.access(s).utf8)))
                    return
                }
                Self.request(.readWrite) { reply(.success(Data(Self.access($0).utf8))) }
            },
            // macOS has no limited library to manage.
            "manage": { _, reply in reply(.success(Data())) },
            "list": { args, reply in
                guard Self.readable() else { reply(.success(Data("[]".utf8))); return }
                let o = AsukaModules2Kit.object(args) ?? [:]
                let screenshots = o["screenshots"] as? Bool ?? false
                let offset = o["offset"] as? Int ?? 0, limit = o["limit"] as? Int ?? 100
                AsukaModules2Kit.offMain(reply) {
                    let opts = PHFetchOptions()
                    opts.sortDescriptors = [NSSortDescriptor(key: "creationDate", ascending: false)]
                    if screenshots {
                        opts.predicate = NSPredicate(format: "(mediaSubtypes & %d) != 0", PHAssetMediaSubtype.photoScreenshot.rawValue)
                    }
                    let found = PHAsset.fetchAssets(with: .image, options: opts)
                    var out: [[String: Any]] = []
                    var i = max(offset, 0)
                    while i < min(offset + limit, found.count) {
                        let a = found.object(at: i)
                        let created = a.creationDate.map { Int64($0.timeIntervalSince1970 * 1000) } ?? 0
                        out.append(["id": a.localIdentifier, "width": a.pixelWidth, "height": a.pixelHeight,
                                    "created": created, "screenshot": a.mediaSubtypes.contains(.photoScreenshot)])
                        i += 1
                    }
                    return try JSONSerialization.data(withJSONObject: out)
                }
            },
            "thumbnail": { args, reply in
                guard Self.readable() else { reply(.failure(asukaNotAllowed)); return }
                let o = AsukaModules2Kit.object(args) ?? [:]
                let id = o["id"] as? String ?? "", size = o["size"] as? Int ?? 300
                AsukaModules2Kit.offMain(reply) {
                    let a = try Self.asset(id)
                    let stamp = a.modificationDate?.timeIntervalSince1970 ?? 0
                    let dir = (try AsukaModules2Kit.cacheFile("asuka-photo-thumbs", "") as NSString).deletingLastPathComponent
                    let path = (dir as NSString).appendingPathComponent(Self.thumbName(a.localIdentifier, stamp, size))
                    if FileManager.default.fileExists(atPath: path) { return Data(path.utf8) }
                    let po = PHImageRequestOptions()
                    po.deliveryMode = .highQualityFormat
                    po.resizeMode = .fast
                    po.isNetworkAccessAllowed = true
                    po.isSynchronous = true
                    var image: NSImage?
                    PHImageManager.default().requestImage(for: a, targetSize: CGSize(width: size, height: size), contentMode: .aspectFill, options: po) { img, _ in image = img }
                    guard let image else { throw AsukaModuleError("no thumbnail") }
                    try Self.writeJPEG(image, path, 0.8)
                    return Data(path.utf8)
                }
            },
            "export": { args, reply in
                guard Self.readable() else { reply(.failure(asukaNotAllowed)); return }
                let o = AsukaModules2Kit.object(args) ?? [:]
                let a: PHAsset
                do { a = try Self.asset(o["id"] as? String ?? "") } catch { reply(.failure(error)); return }
                let rs = PHAssetResource.assetResources(for: a)
                func find(_ t: PHAssetResourceType) -> PHAssetResource? { rs.first { $0.type == t } }
                // The photo as edited, if it was.
                guard let r = find(.fullSizePhoto) ?? find(.photo) ?? rs.first else { reply(.failure(AsukaModuleError("no such photo"))); return }
                let name = find(.photo)?.originalFilename ?? r.originalFilename
                var ext = (r.originalFilename as NSString).pathExtension
                if ext.isEmpty { ext = "jpg" }
                let path: String
                do { path = try AsukaModules2Kit.cacheFile("asuka-photo-exports", "." + ext) } catch { reply(.failure(error)); return }
                let wo = PHAssetResourceRequestOptions()
                wo.isNetworkAccessAllowed = true
                PHAssetResourceManager.default().writeData(for: r, toFile: URL(fileURLWithPath: path), options: wo) { e in
                    DispatchQueue.main.async {
                        if let e {
                            reply(.failure(AsukaModules2Kit.error(e, "couldn't export the photo")))
                            return
                        }
                        reply(Result { try JSONSerialization.data(withJSONObject: ["path": path, "name": name]) })
                    }
                }
            },
        ]
    }
}

/// asuka/library's module.
@MainActor enum AsukaLibrary {
    /// Where save puts a collection that isn't Photos': Downloads (0) and
    /// Music (2). Under the automation, ASUKA_TEST_LIBRARY_DIR's Downloads
    /// and Music: a test never writes to the user's.
    static func dir(_ collection: Int) -> String {
        var home = NSHomeDirectory()
        if AsukaAutomation.shared.active, let d = AsukaAutomation.shared.env["ASUKA_TEST_LIBRARY_DIR"], !d.isEmpty { home = d }
        return (home as NSString).appendingPathComponent(collection == 2 ? "Music" : "Downloads")
    }

    /// Copies (or moves) src into dir/folder as name, numbered
    /// "name (1).ext"... if it's taken, and answers the new path.
    nonisolated static func saveFile(_ src: String, _ dir: String, _ folder: String, _ name: String, move: Bool) throws -> String {
        let fm = FileManager.default
        var d = dir
        if !folder.isEmpty { d = (d as NSString).appendingPathComponent(folder) }
        try fm.createDirectory(atPath: d, withIntermediateDirectories: true)
        let ext = (name as NSString).pathExtension, base = (name as NSString).deletingPathExtension
        var dst = (d as NSString).appendingPathComponent(name)
        var i = 1
        // Taken, a dangling link too (lstat's: not what it points to).
        while (try? fm.attributesOfItem(atPath: dst)) != nil {
            dst = (d as NSString).appendingPathComponent("\(base) (\(i))" + (ext.isEmpty ? "" : "." + ext))
            i += 1
        }
        if move, rename(src, dst) == 0 { return dst }
        guard let input = FileHandle(forReadingAtPath: src) else { throw AsukaModuleError("can't read \(src)") }
        defer { input.closeFile() }
        let fd = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0o644)
        guard fd >= 0 else { throw AsukaModuleError(String(cString: strerror(errno))) }
        let out = FileHandle(fileDescriptor: fd, closeOnDealloc: true)
        do {
            while true {
                let chunk = input.readData(ofLength: 1 << 20)
                if chunk.isEmpty { break }
                out.write(chunk)
            }
            try out.synchronize()
        } catch {
            try? fm.removeItem(atPath: dst)
            throw error
        }
        if move { try? fm.removeItem(atPath: src) }
        return dst
    }

    /// Adds a video (1) or picture (3) to Photos.
    static func savePhotos(_ path: String, _ collection: Int, _ name: String, move: Bool, _ reply: @escaping (Result<Data, Error>) -> Void) {
        let save = { (status: Int) in
            guard status == 3 || status == 4 else { reply(.failure(asukaNotAllowed)); return }
            var id = ""
            PHPhotoLibrary.shared().performChanges({
                let r = PHAssetCreationRequest.forAsset()
                let opts = PHAssetResourceCreationOptions()
                opts.originalFilename = name
                opts.shouldMoveFile = move
                r.addResource(with: collection == 1 ? .video : .photo, fileURL: URL(fileURLWithPath: path), options: opts)
                id = r.placeholderForCreatedAsset?.localIdentifier ?? ""
            }) { ok, e in
                DispatchQueue.main.async {
                    reply(ok ? .success(Data(id.utf8)) : .failure(AsukaModules2Kit.error(e, "couldn't save")))
                }
            }
        }
        let s = AsukaPhotoLibrary.status(.addOnly)
        if s != 0 {
            save(s)
            return
        }
        AsukaPhotoLibrary.request(.addOnly, save)
    }

    static func methods() -> [String: AsukaModuleMethod] {
        ["save": { args, reply in
            guard let o = AsukaModules2Kit.object(args), let path = o["path"] as? String, !path.isEmpty else {
                reply(.failure(AsukaModuleError("bad arguments")))
                return
            }
            guard FileManager.default.fileExists(atPath: path) else { reply(.failure(AsukaModuleError("no file at " + path))); return }
            var name = o["name"] as? String ?? ""
            if name.isEmpty { name = (path as NSString).lastPathComponent }
            let collection = o["collection"] as? Int ?? 0, folder = o["folder"] as? String ?? "", move = o["move"] as? Bool ?? false
            if collection == 1 || collection == 3 {
                savePhotos(path, collection, name, move: move, reply)
                return
            }
            let target = Self.dir(collection)
            AsukaModules2Kit.offMain(reply) { Data(try saveFile(path, target, folder, name, move: move).utf8) }
        }]
    }
}
