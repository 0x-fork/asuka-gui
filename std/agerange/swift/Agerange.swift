// asuka/agerange on iOS: Declared Age Range (iOS 26), asked over the
// app's screen. The Mac has none: notAvailable.
import Foundation
import Asuka
#if os(iOS) && canImport(DeclaredAgeRange)
import UIKit
import DeclaredAgeRange

final class DeclaredSignals: Signals {
    func request(gates: [Int]) async throws -> Range {
        guard #available(iOS 26.0, *) else { throw AgerangeError.notAvailable }
        let g = gates.isEmpty ? [18] : Array(gates.prefix(3))
        return try await MainActor.run { () -> UIViewController in
            guard let vc = AsukaNative.presenter else { throw AsukaNativeError.unsupported("agerange: no screen to ask on") }
            return vc
        }.ask(g)
    }
}

extension UIViewController {
    @available(iOS 26.0, *)
    @MainActor func ask(_ g: [Int]) async throws -> Range {
        do {
            let r = try await AgeRangeService.shared.requestAgeRange(ageGates: g[0], g.count > 1 ? g[1] : nil, g.count > 2 ? g[2] : nil, in: self)
            switch r {
            case .declinedSharing:
                return Range(status: .declined)
            case .sharing(let range):
                var source = Source.unknown
                switch range.ageRangeDeclaration {
                case .selfDeclared?: source = .selfDeclared
                case .guardianDeclared?: source = .guardianDeclared
                case nil: source = .unknown
                default: source = .confirmed // checked or confirmed
                }
                return Range(status: .shared, lower: range.lowerBound ?? 0, upper: range.upperBound ?? 0, source: source)
            @unknown default:
                return Range(status: .declined)
            }
        } catch let e as AgeRangeService.Error where e == .notAvailable {
            throw AgerangeError.notAvailable
        }
    }
}

func makeSignals() throws -> any Signals { DeclaredSignals() }
#else
func makeSignals() throws -> any Signals {
    throw AsukaNativeError.unsupported("agerange: Declared Age Range is iOS's")
}
#endif
