// ui.Native's views on the Mac: a native view kind (the host's own, or an
// app's), an AppKit view in the node's view, filling it; and packages'
// typed views (//asuka:view, NativeModules.swift's AsukaNative.makeView).
// The names are the iOS host's: a view is an AsukaNativeView, here holding
// an NSView.
import AppKit
#if canImport(AsukaOps)
import AsukaOps
#endif

/// A value of a native view's prop or event.
public enum AsukaNativeValue {
    case bool(Bool), int(Int64), float(Double), string(String)
    /// A typed view's prop or event (//asuka:view): its values, encoded.
    case bytes(Data)

    public var bool: Bool { if case .bool(let v) = self { return v }; return false }
    public var int: Int64 {
        switch self { case .int(let v): return v; case .float(let v): return Int64(v); default: return 0 }
    }
    public var float: Double {
        switch self { case .float(let v): return v; case .int(let v): return Double(v); default: return 0 }
    }
    public var string: String { if case .string(let v) = self { return v }; return "" }
    public var bytes: Data { if case .bytes(let v) = self { return v }; return Data() }
}

/// A view an app registers for ui.Native.
@MainActor public protocol AsukaNativeView: AnyObject {
    /// The AppKit view shown. The host's own view holds it and sizes it
    /// to fill.
    var view: NSView { get }
    /// Sets one of its props, by name, as it's set or changes.
    func set(_ name: String, _ value: AsukaNativeValue)
    /// Its size for what the layout offers (.greatestFiniteMagnitude for
    /// either: as wide or tall as it likes). By default, its view's
    /// fittingSize.
    func sizeThatFits(_ size: CGSize) -> CGSize
    /// Frees what it holds, as the core destroys it. By default, nothing.
    func destroy()
}

extension AsukaNativeView {
    public func sizeThatFits(_ size: CGSize) -> CGSize { view.fittingSize }
    public func destroy() {}
}

/// What a native view is given: a way to send its events.
@MainActor public final class AsukaNativeContext {
    public let id: UInt32
    init(id: UInt32) { self.id = id }

    /// Sends an event to the view's ui.NativeProps.On handler.
    public func emit(_ name: String, _ values: [String: AsukaNativeValue] = [:]) {
        var w = AsukaWriter()
        w.string(name)
        w.u32(UInt32(values.count))
        for (k, v) in values {
            w.string(k)
            switch v {
            case .bool(let b): w.u8(1); w.u8(b ? 1 : 0)
            case .int(let i): w.u8(2); w.u64(UInt64(bitPattern: i))
            case .float(let f): w.u8(3); w.f64(f)
            case .string(let s): w.u8(4); w.string(s)
            case .bytes(let d): w.u8(5); w.u32(UInt32(d.count)); w.bytes.append(contentsOf: d)
            }
        }
        let id = self.id, b = w.bytes
        AsukaRuntime.shared.emit(id, .native) { AsukaEvents.native(id, b) }
    }
}

/// A Native node's: its view's type, and the view made of it.
@MainActor final class AsukaNativeHolder {
    var type = ""
    var native: AsukaNativeView?
    /// Props set before the view was made (its type comes first, but a
    /// typed view's own props may come in any order).
    var pending: [(String, AsukaNativeValue)] = []
}
