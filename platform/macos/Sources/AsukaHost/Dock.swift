// asuka/dock on a Mac. The progress is drawn on the Dock tile, under its
// badge: the tile's content view becomes the app's icon with a bar over its
// bottom, a track and a fill. NSProgressIndicator doesn't draw in a Dock
// tile (the tile renders its view offscreen, once per display), so the bar
// is two NSBoxes (custom boxes), which draw themselves: the fill in the
// accent color, red for an error, yellow while paused; an indeterminate one
// a third of the track sliding across, the tile drawn again 15 times a
// second. Attention is NSApp's requestUserAttention (critical bounces until
// the app is active, informational once), which does nothing while the app
// is active.
import AppKit

@MainActor enum AsukaDock {
    // The progress's states.
    static let none: UInt8 = 0
    static let normal: UInt8 = 1
    static let indeterminate: UInt8 = 2
    static let paused: UInt8 = 3
    static let error: UInt8 = 4

    static var view: NSView?
    static var icon: NSImageView?
    static var track: NSBox?
    static var fill: NSBox?
    static var state: UInt8 = 0
    static var value: Double = 0
    static var timer: Timer?
    static var started = Date()

    static func methods() -> [String: AsukaModuleMethod] {
        [
            // args: u8 state (none, normal, indeterminate, paused, error),
            // f64 value.
            "progress": { args, reply in
                var state: UInt8 = 0
                var v = 0.0
                if args.count >= 9 {
                    var r = AsukaByteReader(args)
                    state = r.u8()
                    v = r.f64()
                }
                setProgress(state, v)
                reply(.success(Data()))
            },
            // args: u8 critical; result: u32 the request (0: none, the
            // app's active).
            "attention": { args, reply in
                var id = 0
                if !NSApp.isActive {
                    id = NSApp.requestUserAttention(args.first == 1 ? .criticalRequest : .informationalRequest)
                }
                var w = AsukaByteWriter()
                w.u32(UInt32(truncatingIfNeeded: id))
                reply(.success(w.data))
            },
            "cancelAttention": { args, reply in
                if args.count >= 4 {
                    var r = AsukaByteReader(args)
                    NSApp.cancelUserAttentionRequest(Int(Int32(bitPattern: r.u32())))
                }
                reply(.success(Data()))
            },
        ]
    }

    /// Shows a progress on the Dock tile, or takes it off.
    static func setProgress(_ s: UInt8, _ v: Double) {
        let tile = NSApp.dockTile
        if s == none || s > error {
            stopTimer()
            if view != nil {
                tile.contentView = nil
                view = nil; icon = nil; track = nil; fill = nil
                tile.display()
            }
            state = none
            return
        }
        if view == nil {
            let frame = CGRect(origin: .zero, size: tile.size)
            let v = NSView(frame: frame)
            let i = NSImageView(frame: frame)
            i.imageScaling = .scaleProportionallyUpOrDown
            v.addSubview(i)
            func box() -> NSBox {
                let b = NSBox(frame: .zero)
                b.boxType = .custom
                b.titlePosition = .noTitle
                v.addSubview(b)
                return b
            }
            let t = box(), f = box()
            t.fillColor = NSColor(srgbRed: 0.15, green: 0.15, blue: 0.15, alpha: 0.8)
            t.borderColor = NSColor(srgbRed: 0.7, green: 0.7, blue: 0.7, alpha: 1)
            t.borderWidth = 1
            f.borderWidth = 0
            view = v; icon = i; track = t; fill = f
            tile.contentView = v
        }
        state = s
        value = min(max(v, 0), 1)
        icon?.image = NSApp.applicationIconImage
        let c: NSColor = s == error ? .systemRed : s == paused ? .systemYellow : .controlAccentColor
        fill?.fillColor = c
        fill?.borderColor = c
        if s == indeterminate {
            if timer == nil {
                started = Date()
                let t = Timer(timeInterval: 1.0 / 15, repeats: true) { _ in
                    MainActor.assumeIsolated {
                        layout()
                        NSApp.dockTile.display()
                    }
                }
                RunLoop.main.add(t, forMode: .common)
                timer = t
            }
        } else {
            stopTimer()
        }
        layout()
        tile.display()
    }

    /// Places the track and the fill: a bar across the bottom of the icon,
    /// as Finder's copies show.
    static func layout() {
        guard view != nil, let track, let fill else { return }
        let size = NSApp.dockTile.size
        let h = (size.height * 0.11).rounded()
        let inset = (size.width * 0.1).rounded()
        let tr = CGRect(x: inset, y: (size.height * 0.08).rounded(), width: size.width - 2 * inset, height: h)
        track.frame = tr
        track.cornerRadius = h / 2
        fill.cornerRadius = h / 2
        var f = tr
        if state == indeterminate {
            // A third of the track, sliding across once a second and a half.
            let w = tr.width / 3
            let t = (Date().timeIntervalSince(started) / 1.5).truncatingRemainder(dividingBy: 1)
            let x = tr.minX - w + t * (tr.width + w)
            let left = max(x, tr.minX), right = min(x + w, tr.maxX)
            f.origin.x = left
            f.size.width = max(right - left, 0)
        } else {
            f.size.width = max(h, tr.width * value)
        }
        fill.isHidden = f.width <= 0 || state != indeterminate && value <= 0
        fill.frame = f
    }

    static func stopTimer() {
        timer?.invalidate()
        timer = nil
    }

    /// The tile's progress for the automation's JSON (nil: none).
    static func json() -> [String: Any]? {
        guard view != nil, let track, let fill else { return nil }
        let a = AsukaAutomation.shared
        return ["state": state, "value": value, "track": a.rectJSON(track.frame), "fill": a.rectJSON(fill.frame),
                "fillHidden": fill.isHidden, "sliding": timer != nil, "onTile": NSApp.dockTile.contentView === view,
                "fillColor": fill.fillColor.usingColorSpace(.sRGB).map(colorHex) ?? ""]
    }
}
