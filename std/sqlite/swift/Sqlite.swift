// std/sqlite on iOS and macOS: where a database's relative name is.
// SQLite itself is the plugin's library, which the core calls.
import Foundation

final class AppPlace: Place {
    func dataDir() throws -> String {
        // asuka dev's runs keep their files apart from the installed app's.
        if let d = ProcessInfo.processInfo.environment["ASUKA_DATA"], !d.isEmpty { return d }
        let fm = FileManager.default
        guard var url = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask).first else { return "" }
        #if os(macOS)
        // A Mac's Application Support is shared by the user's apps.
        url.appendPathComponent(Bundle.main.bundleIdentifier ?? ProcessInfo.processInfo.processName)
        #endif
        try fm.createDirectory(at: url, withIntermediateDirectories: true)
        return url.path
    }
}

func makePlace() throws -> any Place { AppPlace() }
