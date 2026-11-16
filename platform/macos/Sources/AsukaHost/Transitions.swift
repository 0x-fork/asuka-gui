// Style.Transition: a view inserted during an animation comes in from its
// transition's state (faded, scaled, offset), and one destroyed during an
// animation goes out to it before it's removed. The view's frame carries
// the scale (around its center) and the offset; a fade is its alpha.
//
// Style.Match, as on iOS: when a view with a key is destroyed during an
// animation and another with the key is inserted, the new one grows from
// the old one's place and size, and the old one moves along to the new
// one's, fading. Where the views with keys are is noted as the animated
// batch starts destroying; the old view goes into its window's content
// view, where it was, until the batch's frames are applied; then they're
// matched. A view going with no match fades.
//
// AppKit's views take no transform that holds (AppKit sets their layers'
// own), so the new view's frame carries the motion, as a transition's
// does: its subviews keep their places in it.
import AppKit
#if canImport(AsukaOps)
import AsukaOps
#endif

/// A view with a key going out, waiting for its match.
struct AsukaMatchOut {
    let view: NSView
    let frame: CGRect // in its window's content view
    weak var content: NSView?
}

/// What matched views need across a batch.
struct AsukaMatching {
    /// Nodes with a key.
    var count = 0
    /// Where they were as the batch began destroying (content
    /// coordinates), and each one's window's content view.
    var frames: [String: CGRect]?
    var contents: [String: NSView] = [:]
    var outs: [String: AsukaMatchOut] = [:]
    var ins: [AsukaNode] = []
    var duration: Double = 0
}

/// The frame f is in at the transition t's state.
func asukaTransitionFrame(_ t: [UInt8], _ f: CGRect) -> CGRect {
    guard t.count >= 13 else { return f }
    let scale = CGFloat(t.f32(1)), dx = CGFloat(t.f32(5)), dy = CGFloat(t.f32(9))
    var r = f
    if scale > 0 && scale != 1 {
        let w = f.width * scale, h = f.height * scale
        r.origin.x += (f.width - w) / 2
        r.origin.y += (f.height - h) / 2
        r.size = CGSize(width: w, height: h)
    }
    r.origin.x += dx
    r.origin.y += dy
    return r
}

@MainActor extension AsukaRuntime {
    /// Puts n at its transition's state, at once, then animates it to r (in
    /// the animation the batch is in).
    func enterWithTransition(_ n: AsukaNode, _ r: CGRect) {
        guard let v = n.view, n.transition.count >= 13 else {
            n.view?.frame = r
            return
        }
        let alpha = v.alphaValue
        let fade = n.transition[0] != 0
        withoutAnimation {
            v.frame = asukaTransitionFrame(n.transition, r)
            if fade { v.alphaValue = 0 }
        }
        v.animator().frame = r
        if fade { v.animator().alphaValue = alpha }
    }

    /// Animates a node going out with its transition and removes its view
    /// once it has; false: no transition to go out with.
    func exit(_ n: AsukaNode) -> Bool {
        guard animating, n.transition.count >= 13, let v = n.view, v.window != nil else { return false }
        let t = n.transition, f = n.frame
        navTarget.exiting[ObjectIdentifier(v)] = v
        let d = animation.duration
        NSAnimationContext.runAnimationGroup({ ctx in
            ctx.duration = d
            ctx.allowsImplicitAnimation = true
            v.animator().frame = asukaTransitionFrame(t, f)
            if t[0] != 0 { v.animator().alphaValue = 0 }
        }, completionHandler: {
            MainActor.assumeIsolated {
                AsukaRuntime.shared.navTarget.exiting.removeValue(forKey: ObjectIdentifier(v))
                v.removeFromSuperview()
            }
        })
        return true
    }

    /// Whether v is inside a view going out (a transition, a match, a
    /// popped screen): it leaves with it.
    func inExiting(_ v: NSView?) -> Bool {
        let ex = navTarget.exiting
        guard !ex.isEmpty, let v else { return false }
        var s = v.superview
        while let x = s {
            if ex[ObjectIdentifier(x)] != nil { return true }
            s = x.superview
        }
        return false
    }

    /// Sets n's key.
    func setMatch(_ n: AsukaNode, _ key: String) {
        if !n.match.isEmpty { navTarget.match.count -= 1 }
        n.match = key
        if !key.isEmpty { navTarget.match.count += 1 }
    }

    /// Takes a view with a key destroyed during an animation into its
    /// window, where it was, until its match comes. True if it did.
    func matchOut(_ n: AsukaNode) -> Bool {
        let t = navTarget
        if !animating || t.match.count == 0 && n.match.isEmpty {
            if !n.match.isEmpty {
                t.match.count -= 1
                n.match = ""
            }
            return false
        }
        if t.match.frames == nil {
            // Where every placed view with a key is, before the batch takes
            // any away (one may go with an ancestor, destroyed first).
            var frames: [String: CGRect] = [:]
            var contents: [String: NSView] = [:]
            func note(_ x: AsukaNode) {
                guard !x.match.isEmpty, x.placed, let v = x.view, let w = v.window, let content = w.contentView else { return }
                frames[x.match] = v.convert(v.bounds, to: content)
                contents[x.match] = content
            }
            note(n) // already gone from the nodes
            for x in nodes.values { note(x) }
            t.match.frames = frames
            t.match.contents = contents
        }
        let key = n.match
        if key.isEmpty { return false }
        t.match.count -= 1
        n.match = ""
        guard let f = t.match.frames?[key], let v = n.view, f.width > 0, f.height > 0, let content = t.match.contents[key] else { return false }
        if let old = t.match.outs[key] { fadeOut(old.view, 0) } // two going with one key: the last waits
        withoutAnimation {
            v.removeFromSuperview()
            v.frame = f
            content.addSubview(v)
        }
        t.exiting[ObjectIdentifier(v)] = v // what's in it leaves with it
        t.match.outs[key] = AsukaMatchOut(view: v, frame: f, content: content)
        t.match.duration = animation.duration
        // If no frames ever come, it goes anyway.
        DispatchQueue.main.asyncAfter(deadline: .now() + t.match.duration + 1) {
            MainActor.assumeIsolated {
                let rt = AsukaRuntime.shared
                if let o = rt.navTarget.match.outs[key], o.view === v {
                    rt.navTarget.match.outs.removeValue(forKey: key)
                    rt.fadeOut(v, 0)
                }
            }
        }
        return true
    }

    /// Notes a view inserted during an animation: it, or a view in it, may
    /// be one an old one's going to.
    func matchIn(_ n: AsukaNode) {
        let m = navTarget.match
        if animating && (m.count > 0 || !m.outs.isEmpty) { navTarget.match.ins.append(n) }
    }

    /// Runs the matches once a batch's frames are applied; a props batch
    /// only ends what the destroying noted.
    func matchBatchApplied(_ frames: Bool) {
        let t = navTarget
        t.match.frames = nil
        t.match.contents = [:]
        guard frames else { return }
        let ins = t.match.ins
        t.match.ins = []
        if t.match.outs.isEmpty { return }
        var coming: [AsukaNode] = []
        func collect(_ n: AsukaNode) {
            if !n.match.isEmpty && t.match.outs[n.match] != nil { coming.append(n) }
            for c in n.children { collect(c) }
        }
        for n in ins { collect(n) }
        let d = t.match.duration
        for n in coming {
            guard let out = t.match.outs[n.match], let v = n.view, let w = v.window, let oc = out.content, w.contentView === oc else { continue }
            t.match.outs.removeValue(forKey: n.match)
            guard let sup = v.superview else {
                fadeOut(out.view, d)
                continue
            }
            let final = v.frame
            let to = v.convert(v.bounds, to: oc)
            if to.width <= 0 || to.height <= 0 {
                fadeOut(out.view, d)
                continue
            }
            let from = sup.convert(out.frame, from: oc)
            withoutAnimation { v.frame = from }
            let old = out.view
            asukaAnimate(d, {
                v.animator().frame = final
                old.animator().frame = to
                old.animator().alphaValue = 0
            }, { AsukaRuntime.shared.dropOut(old) })
        }
        for (k, out) in t.match.outs {
            t.match.outs.removeValue(forKey: k)
            fadeOut(out.view, d)
        }
    }

    /// Fades an old view out and lets it go.
    func fadeOut(_ v: NSView, _ d: Double) {
        if d <= 0 {
            dropOut(v)
            return
        }
        asukaAnimate(d, { v.animator().alphaValue = 0 }, { AsukaRuntime.shared.dropOut(v) })
    }

    func dropOut(_ v: NSView) {
        guard navTarget.exiting.removeValue(forKey: ObjectIdentifier(v)) != nil else { return }
        v.removeFromSuperview()
    }
}
