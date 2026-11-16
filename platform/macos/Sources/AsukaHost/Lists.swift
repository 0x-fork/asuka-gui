// STUB (part: list). Core-virtualized lists (List: a scroll view whose
// rows the host places, binds through asuka_list_bind and recycles as it
// scrolls), pull to refresh, a Scroll's paging and PageControl's dots.
// Ports host/darwin's list.go, refresh.go, pager.go. The list agent
// replaces this file, keeping these functions' signatures.
import AppKit
#if canImport(AsukaOps)
import AsukaOps
#endif

@MainActor final class AsukaList {}

/// A list's cell: a view of the host's the core binds rows into.
@MainActor final class AsukaListCell {
    /// The view its row's views go in.
    let content: NSView = AsukaView(frame: .zero)
    var swipe: [AsukaActionItem] = []
    var leading: [AsukaActionItem] = []
    func setHeight(_ h: CGFloat) {}
    func setSelected(_ on: Bool) {}
}

@MainActor final class AsukaRefresher {}
@MainActor final class AsukaPageDots {}

@MainActor extension AsukaRuntime {
    func makeList(_ n: AsukaNode) -> NSView { AsukaScrollView(frame: .zero) }
    func setListProp(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {}
    func listUpdate(_ n: AsukaNode, _ args: UnsafeRawBufferPointer) {}
    func listScrollTo(_ n: AsukaNode, _ index: Int, _ animated: Bool) {}
    /// Binds what a list has pending before it's scrolled.
    func listFlush(_ n: AsukaNode) {}
    /// A list's clip view scrolled: the rows shown bound.
    func listScrolled(_ clip: NSClipView) {}
    /// The rows a list shows, and their cells' nodes (for dumps).
    func listRows(_ n: AsukaNode) -> [(Int, [AsukaNode])] { [] }
    func listBackground(_ n: AsukaNode) {}
    /// A row's swipe actions as its context menu.
    func cellMenu(_ c: AsukaListCell) -> NSMenu? { nil }
    func setRefreshable(_ n: AsukaNode, _ on: Bool) {}
    func setRefreshing(_ n: AsukaNode, _ on: Bool) {}
    func setPaging(_ n: AsukaNode, _ on: Bool) { n.paging = on }
    func makePageDots(_ n: AsukaNode) -> NSView { AsukaView(frame: .zero) }
    func setPageDotsProp(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {}
    func measureDots(_ n: AsukaNode) -> CGSize { .zero }

    // Hooks (Parts.swift).
    func listDestroyed(_ n: AsukaNode) {}
    func listReleased(_ n: AsukaNode) {}
    func listPlaced(_ n: AsukaNode) {}
    func listColors(_ n: AsukaNode) {}
    func listDirection(_ n: AsukaNode, _ changed: Bool) {}
    func listBatchApplied(_ frames: Bool) {}
    func listJSON(_ n: AsukaNode, _ d: inout [String: Any]) {}
}
