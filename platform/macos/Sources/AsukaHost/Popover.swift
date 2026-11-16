// A Popover is an NSPopover pointing at its anchor, below it (AppKit moves
// it where there's room), transient: a click outside or Escape closes it,
// reported as Dismissed. While ui.OnBack in it intercepts, it asks the core
// (Back) instead of closing.
import AppKit
#if canImport(AsukaOps)
import AsukaOps
#endif

@MainActor final class AsukaPopover {
    var pop: NSPopover?
    var anchor: UInt32 = 0
    var size = CGSize.zero
}

@MainActor extension AsukaRuntime {
    func setPopoverProp(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {
        guard let po = n.popover else { return }
        switch p {
        case AsukaProp.Popover.anchor: po.anchor = UInt32(truncatingIfNeeded: v.int)
        case AsukaProp.Popover.width: po.size.width = CGFloat(v.float)
        case AsukaProp.Popover.height: po.size.height = CGFloat(v.float)
        default: return
        }
        if let pop = po.pop, po.size.width > 0, po.size.height > 0 {
            pop.contentSize = po.size
            n.view?.frame = CGRect(origin: .zero, size: po.size)
            reportScreen(n, po.size)
        }
    }

    func presentPopover(_ n: AsukaNode) {
        // After the batch: its anchor, made with it, isn't in a window yet,
        // nor laid out.
        afterCore { [weak self] in
            guard let self, self.alive(n), !n.dying else { return }
            self.showPopover(n)
        }
    }

    func showPopover(_ n: AsukaNode) {
        guard let po = n.popover, po.pop == nil, let a = nodes[po.anchor], let av = a.view, av.window != nil, let view = n.view else { return }
        let vc = NSViewController()
        view.frame = CGRect(origin: .zero, size: po.size)
        vc.view = view
        let p = NSPopover()
        p.contentViewController = vc
        p.behavior = .transient
        p.animates = true
        p.delegate = navTarget
        p.contentSize = po.size
        po.pop = p
        n.presented = true
        reportScreen(n, po.size)
        p.show(relativeTo: av.bounds, of: av, preferredEdge: .maxY) // below, in a flipped view
    }

    /// The node whose NSPopover p is.
    func popoverOf(_ p: NSPopover) -> AsukaNode? {
        nodes.values.first { $0.popover?.pop === p }
    }

    /// A click outside or Escape; ui.OnBack may keep it.
    func popoverShouldClose(_ p: NSPopover) -> Bool {
        guard let n = popoverOf(p), !n.dying, n.interceptBack else { return true }
        emit(n, .back) { AsukaEvents.back($0) }
        return false
    }

    func popoverClosed(_ p: NSPopover) {
        guard let n = popoverOf(p) else { return }
        n.popover?.pop = nil
        if !n.dying {
            n.dying = true
            emit(n, .dismissed) { AsukaEvents.dismissed($0) }
        }
    }

    func closePopover(_ n: AsukaNode) {
        guard let p = n.popover?.pop else { return }
        p.close() // popoverDidClose: follows, and lets it go
        // Closed by the core: it's gone whether or not AppKit says so.
        if n.popover?.pop === p { n.popover?.pop = nil }
    }

    /// Closes the popover showing, as a click outside it would (the
    /// automation's "dismiss").
    func dismissPopover() -> Bool {
        guard let n = nodes.values.first(where: { $0.popover?.pop?.isShown == true }), let p = n.popover?.pop else { return false }
        p.performClose(nil)
        return true
    }

    func popoverJSON(_ n: AsukaNode, _ d: inout [String: Any]) {
        guard let po = n.popover else { return }
        d["popoverShown"] = po.pop?.isShown ?? false
        d["popoverSize"] = [Double(po.size.width), Double(po.size.height)]
        if let p = po.pop { d["popoverContentSize"] = [Double(p.contentSize.width), Double(p.contentSize.height)] }
        d["anchor"] = po.anchor
    }
}

extension AsukaNavTarget: NSPopoverDelegate {
    func popoverShouldClose(_ popover: NSPopover) -> Bool { rt.popoverShouldClose(popover) }

    func popoverDidClose(_ notification: Notification) {
        guard let p = notification.object as? NSPopover else { return }
        rt.popoverClosed(p)
    }
}
