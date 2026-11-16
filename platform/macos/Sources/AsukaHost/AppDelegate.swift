// The application's delegate: what happens to the app as a whole (it
// launches, opens files and links, comes to the front, is asked to quit, is
// opened again). Windows.swift and Shell.swift implement its methods.
import AppKit

@MainActor final class AsukaAppDelegate: NSObject, NSApplicationDelegate {
    static let shared = AsukaAppDelegate()
}
