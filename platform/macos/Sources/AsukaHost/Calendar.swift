// asuka/calendar on a Mac, with EventKit as on iOS: "request" answers u8
// access (not asked, denied, write only, full), "calendars" and "events"
// (JSON: from, to in ms, calendars) JSON, "add" (an event's JSON) u8
// saved. macOS has no event editor for apps (EventKitUI is iOS's): "add"
// asks with an alert over the window (the title, its dates, its place)
// and saves it on Add, which needs only write access (macOS 14). macOS
// asks once, with the app's NSCalendarsFullAccessUsageDescription
// (NSCalendarsWriteOnlyAccessUsageDescription for adding only).
import AppKit
import EventKit

@MainActor final class AsukaCalendar {
    static let shared = AsukaCalendar()
    private lazy var store = EKEventStore()

    // MARK: Access

    /// EventKit's authorization status for events (the automation's
    /// seam's, under it).
    static func status() -> Int {
        if let s = AsukaAccessFake.status("calendar") { return s }
        return EKEventStore.authorizationStatus(for: .event).rawValue
    }

    /// Asks for full access (write only: for adding), then calls done on
    /// the main thread.
    func request(writeOnly: Bool, _ done: @escaping () -> Void) {
        if AsukaAccessFake.request("calendar", writeOnly ? "write" : "full") {
            DispatchQueue.main.async { done() }
            return
        }
        let finished: (Bool, Error?) -> Void = { _, _ in DispatchQueue.main.async { done() } }
        if #available(macOS 14.0, *) {
            if writeOnly {
                store.requestWriteOnlyAccessToEvents(completion: finished)
            } else {
                store.requestFullAccessToEvents(completion: finished)
            }
        } else {
            store.requestAccess(to: .event, completion: finished)
        }
    }

    /// The status as asuka/calendar's Access: not asked, denied, write
    /// only, full.
    static func access() -> UInt8 {
        switch status() {
        case 3: return 3 // fullAccess (authorized before macOS 14)
        case 4: return 2 // writeOnly
        case 0: return 0
        default: return 1
        }
    }

    // MARK: Times

    /// An event's start and end as the wire has them: all-day events'
    /// days at UTC midnight (as Android keeps them), their end the day
    /// after the last (EventKit's is the last day's end).
    nonisolated static func eventMs(_ start: Date, _ end: Date, allDay: Bool, _ tz: TimeZone) -> (Int64, Int64) {
        func ms(_ d: Date) -> Int64 { Int64((d.timeIntervalSince1970 * 1000).rounded(.down)) }
        guard allDay else { return (ms(start), ms(end)) }
        var local = Calendar(identifier: .gregorian)
        local.timeZone = tz
        var utc = Calendar(identifier: .gregorian)
        utc.timeZone = TimeZone(identifier: "UTC")!
        func day(_ d: Date) -> Date {
            let c = local.dateComponents([.year, .month, .day], from: d)
            return utc.date(from: DateComponents(year: c.year, month: c.month, day: c.day))!
        }
        return (ms(day(start)), ms(utc.date(byAdding: .day, value: 1, to: day(end))!))
    }

    /// "add"'s start and end as EventKit takes them: all-day days (UTC on
    /// the wire) the same days here, the end day inclusive; an end before
    /// the start an hour after it (the start's day, all day). No start:
    /// the next hour.
    nonisolated static func addTimes(_ startMs: Int64, _ endMs: Int64, allDay: Bool, _ tz: TimeZone, now: Date) -> (Date, Date) {
        var local = Calendar(identifier: .gregorian)
        local.timeZone = tz
        var utc = Calendar(identifier: .gregorian)
        utc.timeZone = TimeZone(identifier: "UTC")!
        var start = Date(timeIntervalSince1970: Double(startMs) / 1000), end = Date(timeIntervalSince1970: Double(endMs) / 1000)
        if startMs <= 0 {
            start = local.dateInterval(of: .hour, for: now)!.start.addingTimeInterval(3600)
            end = start
        }
        if allDay {
            func midnight(_ c: DateComponents) -> Date { local.date(from: DateComponents(year: c.year, month: c.month, day: c.day))! }
            if startMs > 0 {
                start = midnight(utc.dateComponents([.year, .month, .day], from: start))
                end = midnight(utc.dateComponents([.year, .month, .day], from: end))
            } else {
                start = midnight(local.dateComponents([.year, .month, .day], from: now))
                end = start
            }
            return end > start ? (start, end.addingTimeInterval(-1)) : (start, start)
        }
        return end > start ? (start, end) : (start, start.addingTimeInterval(3600))
    }

    /// "add"'s JSON.
    struct Args {
        var title = "", location = "", notes = "", url = "", calendarID = ""
        var startMs: Int64 = 0, endMs: Int64 = 0
        var allDay = false

        init() {}
        init(_ o: [String: Any]) {
            func s(_ k: String) -> String { o[k] as? String ?? "" }
            title = s("title"); location = s("location"); notes = s("notes"); url = s("url"); calendarID = s("calendarId")
            startMs = (o["startMs"] as? NSNumber)?.int64Value ?? 0
            endMs = (o["endMs"] as? NSNumber)?.int64Value ?? 0
            allDay = o["allDay"] as? Bool ?? false
        }
    }

    /// The add alert's message and informative text: the title, then the
    /// dates (as the user's locale writes a range) and the place.
    static func alertText(_ o: Args, _ start: Date, _ end: Date) -> (title: String, info: String) {
        let title = o.title.isEmpty ? asukaWord("newEvent") : o.title
        let f = DateIntervalFormatter()
        f.dateStyle = .long
        f.timeStyle = o.allDay ? .none : .short
        var info = f.string(from: start, to: end)
        if !o.location.isEmpty { info += "\n" + o.location }
        return (title, info)
    }

    /// An NSColor as #rrggbb in sRGB.
    nonisolated static func hex(_ c: NSColor?) -> String {
        guard let c = c?.usingColorSpace(.sRGB) else { return "" }
        func ch(_ v: CGFloat) -> Int { Int(min(max(v, 0), 1) * 255) }
        return String(format: "#%02x%02x%02x", ch(c.redComponent), ch(c.greenComponent), ch(c.blueComponent))
    }

    nonisolated static func eventJSON(_ e: EKEvent) -> [String: Any] {
        let (s, en) = eventMs(e.startDate, e.endDate, allDay: e.isAllDay, TimeZone.current)
        return [
            "id": e.eventIdentifier ?? "", "calendarId": e.calendar?.calendarIdentifier ?? "",
            "title": e.title ?? "", "location": e.location ?? "", "notes": e.notes ?? "", "url": e.url?.absoluteString ?? "",
            "startMs": s, "endMs": en, "allDay": e.isAllDay,
        ]
    }

    /// Saves the event, once the user said Add: write access is enough.
    func save(_ o: Args, _ start: Date, _ end: Date, _ reply: @escaping (Result<Data, Error>) -> Void) {
        let save = { [unowned self] in
            guard Self.access() >= 2 else { reply(.failure(asukaNotAllowed)); return }
            let s = self.store
            let e = EKEvent(eventStore: s)
            e.title = o.title
            if !o.location.isEmpty { e.location = o.location }
            if !o.notes.isEmpty { e.notes = o.notes }
            if !o.url.isEmpty, let u = URL(string: o.url) { e.url = u }
            e.isAllDay = o.allDay
            e.startDate = start
            e.endDate = end
            var c: EKCalendar?
            if !o.calendarID.isEmpty { c = s.calendar(withIdentifier: o.calendarID) }
            if c == nil { c = s.defaultCalendarForNewEvents }
            guard let c else { reply(.failure(AsukaModuleError("no calendar to add to"))); return }
            e.calendar = c
            do {
                try s.save(e, span: .thisEvent, commit: true)
                reply(.success(Data([1])))
            } catch {
                reply(.failure(AsukaModules2Kit.error(error, "couldn't save the event")))
            }
        }
        if Self.status() == 0 {
            request(writeOnly: true, save)
            return
        }
        save()
    }

    // MARK: The module

    func methods() -> [String: AsukaModuleMethod] {
        [
            "request": { [unowned self] _, reply in
                if Self.access() == 3 {
                    reply(.success(Data([3])))
                    return
                }
                self.request(writeOnly: false) { reply(.success(Data([Self.access()]))) }
            },
            "calendars": { [unowned self] _, reply in
                guard Self.access() == 3 else { reply(.failure(asukaNotAllowed)); return }
                let out: [[String: Any]] = self.store.calendars(for: .event).map { c in
                    ["id": c.calendarIdentifier, "title": c.title, "color": Self.hex(c.color),
                     "source": c.source?.title ?? "", "writable": c.allowsContentModifications]
                }
                reply(Result { try JSONSerialization.data(withJSONObject: out) })
            },
            "events": { [unowned self] args, reply in
                guard Self.access() == 3 else { reply(.failure(asukaNotAllowed)); return }
                let o = AsukaModules2Kit.object(args) ?? [:]
                let from = Date(timeIntervalSince1970: ((o["from"] as? Double) ?? 0).rounded(.towardZero) / 1000)
                let to = Date(timeIntervalSince1970: ((o["to"] as? Double) ?? 0).rounded(.towardZero) / 1000)
                let s = self.store
                var cals: [EKCalendar]?
                if let ids = o["calendars"] as? [String], !ids.isEmpty {
                    let want = Set(ids)
                    cals = s.calendars(for: .event).filter { want.contains($0.calendarIdentifier) }
                }
                let chosen = cals
                AsukaModules2Kit.offMain(reply) {
                    var out: [[String: Any]] = []
                    // EventKit's predicate spans at most four years: in steps.
                    var start = from
                    var cal = Calendar(identifier: .gregorian)
                    cal.timeZone = TimeZone(identifier: "UTC")!
                    while start < to {
                        var end = cal.date(byAdding: .year, value: 4, to: start)!
                        if end > to { end = to }
                        let p = s.predicateForEvents(withStart: start, end: end, calendars: chosen)
                        for e in s.events(matching: p) {
                            // An event over a step's edge comes in both: once.
                            if e.startDate < start && start != from { continue }
                            out.append(Self.eventJSON(e))
                        }
                        start = end
                    }
                    // Stable: by the start, the order EventKit gave otherwise.
                    let sorted = out.enumerated().sorted { a, b in
                        let x = a.element["startMs"] as? Int64 ?? 0, y = b.element["startMs"] as? Int64 ?? 0
                        return x != y ? x < y : a.offset < b.offset
                    }.map(\.element)
                    return try JSONSerialization.data(withJSONObject: sorted)
                }
            },
            "add": { [unowned self] args, reply in
                guard let json = AsukaModules2Kit.object(args) else { reply(.failure(AsukaModuleError("calendar.add: bad arguments"))); return }
                let o = Args(json)
                let (start, end) = Self.addTimes(o.startMs, o.endMs, allDay: o.allDay, TimeZone.current, now: Date())
                let (title, info) = Self.alertText(o, start, end)
                AsukaModules2Kit.confirm(title, info, asukaWord("addToCalendar")) { [weak self] ok in
                    guard ok else { reply(.success(Data([0]))); return }
                    self?.save(o, start, end, reply)
                }
            },
        ]
    }
}
