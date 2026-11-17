// platform/scan on iOS and macOS: the Vision framework's barcode detection.
// Off the main thread.
import Asuka
import Foundation
import ImageIO
import Vision

enum Read {
    static let symbologies: [(String, [VNBarcodeSymbology])] = [
        ("qr", [.qr]), ("aztec", [.aztec]), ("datamatrix", [.dataMatrix]), ("pdf417", [.pdf417]),
        ("ean13", [.ean13]), ("ean8", [.ean8]), ("upca", [.ean13]), ("upce", [.upce]),
        ("code128", [.code128]), ("code39", [.code39]), ("code93", [.code93]), ("codabar", [.codabar]),
        ("itf", [.itf14, .i2of5]),
    ]

    static func kind(_ s: VNBarcodeSymbology) -> String? {
        symbologies.first { $0.0 != "upca" && $0.1.contains(s) }?.0
    }
}

/// An image's failure, with what went wrong.
struct Failure: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}

final class VisionReader: Reader {
    func file(path: String, kinds: [String]) async throws -> [Code] {
        let want = Set(kinds)
        let wants = { (k: String) in want.isEmpty || want.contains(k) }
        var wanted: [VNBarcodeSymbology] = []
        for (k, syms) in Read.symbologies where wants(k) {
            for x in syms where !wanted.contains(x) { wanted.append(x) }
        }
        let url = AsukaAssets.url(path) ?? URL(fileURLWithPath: path)
        return try await withCheckedThrowingContinuation { c in
            DispatchQueue.global(qos: .userInitiated).async {
                let req = VNDetectBarcodesRequest()
                req.symbologies = wanted
                #if targetEnvironment(simulator)
                // The simulator can't run the learned detectors (revisions 2
                // and later find nothing there): the first, on the CPU.
                req.revision = VNDetectBarcodesRequestRevision1
                req.usesCPUOnly = true
                req.symbologies = wanted.filter { (try? req.supportedSymbologies())?.contains($0) ?? false }
                #endif
                do {
                    try VNImageRequestHandler(url: url).perform([req])
                    var codes: [Code] = []
                    for o in req.results ?? [] {
                        guard let v = o.payloadStringValue, var k = Read.kind(o.symbology) else { continue }
                        var value = v
                        // Apple reads a UPC-A as an EAN-13 with a leading 0:
                        // it's a UPC-A again if the app asked for those.
                        if k == "ean13", v.count == 13, v.hasPrefix("0"), want.contains("upca") {
                            k = "upca"
                            value = String(v.dropFirst())
                        }
                        if wants(k), !codes.contains(where: { $0.kind == k && $0.value == value }) { codes.append(Code(kind: k, value: value)) }
                    }
                    c.resume(returning: codes)
                } catch {
                    c.resume(throwing: Failure(message: "can't read the image: \(error.localizedDescription)"))
                }
            }
        }
    }
}

func makeReader() throws -> any Reader { VisionReader() }
