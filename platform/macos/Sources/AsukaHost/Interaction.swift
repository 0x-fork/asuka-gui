// STUB (part: visual). Interaction states and drag and drop: presses
// (Press, Pressed: mouseDown and mouseUp on a view), hover (tracking
// areas), long presses (a secondary click too), pans, swipes (a quick pan,
// a trackpad's flick, three fingers), pinches and rotations, drops and
// drags. Ports host/darwin's interaction.go, swipe.go and views.go's
// gestures. The visual agent replaces this file, keeping these functions'
// signatures.
import AppKit
#if canImport(AsukaOps)
import AsukaOps
#endif

@MainActor final class AsukaInteraction {}

@MainActor extension AsukaRuntime {
    /// Adds what an event needs on a view: gesture recognizers, tracking
    /// areas, the press role.
    func listenGesture(_ n: AsukaNode, _ e: AsukaEventKind) {}
    func mouseDown(_ v: NSView, _ e: NSEvent) -> Bool { false }
    func mouseDragged(_ e: NSEvent) -> Bool { false }
    func mouseUp(_ e: NSEvent) -> Bool { false }
    func mouseEntered(_ v: NSView) {}
    func mouseExited(_ v: NSView) {}
    /// n left its window or was hidden: no longer pressed or hovered.
    func viewLeft(_ n: AsukaNode) {}
    /// Whether the pointer is over n, as it moved or n did.
    func checkHover(_ n: AsukaNode) {}
    func swipeEvent(_ v: NSView, _ e: NSEvent) -> Bool { false }
    func wheel(_ v: NSView, _ e: NSEvent) -> Bool { false }
    func dragOver(_ v: NSView, _ info: NSDraggingInfo) -> NSDragOperation { [] }
    func dragExited(_ v: NSView, _ info: NSDraggingInfo?) {}
    func prepareDrop(_ v: NSView, _ info: NSDraggingInfo) -> Bool { false }
    func performDrop(_ v: NSView, _ info: NSDraggingInfo) -> Bool { false }
    func draggingEnded(_ v: NSView, _ info: NSDraggingInfo) {}
    func dragMask(_ c: NSDraggingContext) -> NSDragOperation { .copy }
    func dragBegan() {}
    func endDrag(_ op: NSDragOperation) {}
    func setDropAccept(_ n: AsukaNode, _ b: [UInt8]) {}
    func setDragItems(_ n: AsukaNode, _ b: [UInt8]) {}
}
