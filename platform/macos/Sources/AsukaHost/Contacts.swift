// platform/contacts on a Mac, with the Contacts framework as on iOS:
// "request" answers u8 access (not asked, denied, limited, full), "list"
// (JSON: name, limit) and "pick" contacts as iOS's JSON, "add" (a
// contact's JSON) u8 saved. macOS has no new-contact screen: "add" asks
// with an alert over the window (the contact's name, phones and emails)
// and saves it on Add. "pick" is the system's CNContactPicker, a popover
// from the top of the window. macOS asks for access once, with the app's
// NSContactsUsageDescription.
import AppKit
import Contacts
import ContactsUI

@MainActor final class AsukaContacts: NSObject, CNContactPickerDelegate {
    static let shared = AsukaContacts()
    private lazy var store = CNContactStore()
    private var picker: CNContactPicker?
    private var picking: ((Result<Data, Error>) -> Void)?

    /// Whether the picker shows (the automation's JSON).
    var pickerShown: Bool { picking != nil }

    // MARK: Access

    /// CNContactStore's authorization status for contacts (the
    /// automation's seam's, under it).
    static func status() -> Int {
        if let s = AsukaAccessFake.status("contacts") { return s }
        return CNContactStore.authorizationStatus(for: .contacts).rawValue
    }

    /// Asks the user, then calls done on the main thread.
    func request(_ done: @escaping () -> Void) {
        if AsukaAccessFake.request("contacts") {
            DispatchQueue.main.async { done() }
            return
        }
        store.requestAccess(for: .contacts) { _, _ in DispatchQueue.main.async { done() } }
    }

    /// The status as platform/contacts' Access: not asked, denied, limited,
    /// full.
    static func access() -> UInt8 {
        switch status() {
        case 3: return 3 // authorized
        case 1, 2: return 1 // restricted, denied
        case 0: return 0
        default: return 2 // limited
        }
    }

    /// Asks if it's not been asked, then calls then with whether contacts
    /// may be read and written.
    func allowed(_ then: @escaping (Bool) -> Void) {
        if Self.status() == 0 {
            request { then(Self.access() >= 2) }
            return
        }
        then(Self.access() >= 2)
    }

    // MARK: Contacts as JSON

    /// The keys a contact is fetched with.
    nonisolated static var keys: [CNKeyDescriptor] {
        [CNContactFormatter.descriptorForRequiredKeys(for: .fullName),
         CNContactGivenNameKey as CNKeyDescriptor, CNContactFamilyNameKey as CNKeyDescriptor,
         CNContactOrganizationNameKey as CNKeyDescriptor, CNContactJobTitleKey as CNKeyDescriptor,
         CNContactPhoneNumbersKey as CNKeyDescriptor, CNContactEmailAddressesKey as CNKeyDescriptor,
         CNContactBirthdayKey as CNKeyDescriptor]
    }

    /// A contact as iOS's JSON (the keys it doesn't have empty).
    nonisolated static func json(_ c: CNContact) -> [String: Any] {
        func has(_ k: String) -> Bool { c.isKeyAvailable(k) }
        func label(_ l: String?) -> String { l.map { CNLabeledValue<NSString>.localizedString(forLabel: $0) } ?? "" }
        var birthday = ""
        if has(CNContactBirthdayKey), let b = c.birthday {
            birthday = Self.birthday(b.year, b.month, b.day)
        }
        return [
            "id": c.identifier,
            "name": CNContactFormatter.string(from: c, style: .fullName) ?? "",
            "givenName": has(CNContactGivenNameKey) ? c.givenName : "",
            "familyName": has(CNContactFamilyNameKey) ? c.familyName : "",
            "organization": has(CNContactOrganizationNameKey) ? c.organizationName : "",
            "jobTitle": has(CNContactJobTitleKey) ? c.jobTitle : "",
            "phones": has(CNContactPhoneNumbersKey) ? c.phoneNumbers.map { ["label": label($0.label), "value": $0.value.stringValue] } : [],
            "emails": has(CNContactEmailAddressesKey) ? c.emailAddresses.map { ["label": label($0.label), "value": $0.value as String] } : [],
            "birthday": birthday,
            "note": "", // the note needs an entitlement Apple grants on request
        ]
    }

    /// A birthday as iOS's JSON has it: YYYY-MM-DD, or --MM-DD without a
    /// year; "" without a month or day.
    nonisolated static func birthday(_ year: Int?, _ month: Int?, _ day: Int?) -> String {
        let undefined = NSDateComponentUndefined
        guard let month, let day, month != undefined, day != undefined, month > 0, day > 0 else { return "" }
        var y = "-"
        if let year, year != undefined, year > 0 { y = String(format: "%04d", year) }
        return y + String(format: "-%02d-%02d", month, day)
    }

    // MARK: Adding

    /// "add"'s JSON.
    struct Args {
        var name = "", givenName = "", familyName = "", organization = "", jobTitle = "", birthday = ""
        var phones: [(label: String, value: String)] = []
        var emails: [(label: String, value: String)] = []

        init() {}
        init(_ o: [String: Any]) {
            func s(_ k: String) -> String { o[k] as? String ?? "" }
            func items(_ k: String) -> [(label: String, value: String)] {
                (o[k] as? [[String: Any]] ?? []).map { ($0["label"] as? String ?? "", $0["value"] as? String ?? "") }
            }
            name = s("name"); givenName = s("givenName"); familyName = s("familyName")
            organization = s("organization"); jobTitle = s("jobTitle"); birthday = s("birthday")
            phones = items("phones"); emails = items("emails")
        }
    }

    /// Reads YYYY-MM-DD or --MM-DD.
    nonisolated static func parseBirthday(_ s: String) -> (year: Int, month: Int, day: Int)? {
        let p = s.split(separator: "-", omittingEmptySubsequences: false).map { Int($0) ?? 0 }
        var y = 0, m = 0, d = 0
        switch p.count {
        case 3: (y, m, d) = (p[0], p[1], p[2])
        case 4: (m, d) = (p[2], p[3]) // --MM-DD
        default: return nil
        }
        return m > 0 && d > 0 ? (y, m, d) : nil
    }

    /// A new contact of "add"'s JSON.
    nonisolated static func build(_ o: Args) -> CNMutableContact {
        let c = CNMutableContact()
        var given = o.givenName
        if given.isEmpty && o.familyName.isEmpty { given = o.name }
        c.givenName = given
        c.familyName = o.familyName
        c.organizationName = o.organization
        c.jobTitle = o.jobTitle
        c.phoneNumbers = o.phones.map { CNLabeledValue(label: $0.label.isEmpty ? CNLabelPhoneNumberMobile : $0.label, value: CNPhoneNumber(stringValue: $0.value)) }
        c.emailAddresses = o.emails.map { CNLabeledValue(label: $0.label.isEmpty ? CNLabelHome : $0.label, value: $0.value as NSString) }
        if let b = parseBirthday(o.birthday) {
            var dc = DateComponents()
            if b.year > 0 { dc.year = b.year }
            dc.month = b.month
            dc.day = b.day
            c.birthday = dc
        }
        return c
    }

    /// The add alert's message and informative text: the name, then the
    /// organization, phones and emails, a line each.
    nonisolated static func alertText(_ o: Args) -> (title: String, info: String) {
        var title = (o.givenName + " " + o.familyName).trimmingCharacters(in: .whitespaces)
        if title.isEmpty { title = o.name }
        if title.isEmpty { title = o.organization }
        var lines: [String] = []
        if !o.organization.isEmpty && title != o.organization {
            lines.append(o.jobTitle.isEmpty ? o.organization : o.jobTitle + " · " + o.organization)
        }
        func item(_ label: String, _ v: String) -> String { label.isEmpty ? v : label + ": " + v }
        for p in o.phones { lines.append(item(p.label, p.value)) }
        for e in o.emails { lines.append(item(e.label, e.value)) }
        if !o.birthday.isEmpty { lines.append(o.birthday) }
        return (title, lines.joined(separator: "\n"))
    }

    /// Saves a contact, once the user said Add.
    func save(_ o: Args, _ reply: @escaping (Result<Data, Error>) -> Void) {
        allowed { [weak self] ok in
            guard let self else { return }
            guard ok else { reply(.failure(asukaNotAllowed)); return }
            let req = CNSaveRequest()
            req.add(Self.build(o), toContainerWithIdentifier: nil)
            do {
                try self.store.execute(req)
                reply(.success(Data([1])))
            } catch {
                reply(.failure(AsukaModules2Kit.error(error, "couldn't save the contact")))
            }
        }
    }

    // MARK: Picking

    /// Answers pick: the contact, fetched again with every key if access
    /// allows (the picker's carries only the ones it shows), or "" once
    /// the picker closes without one.
    private func picked(_ c: CNContact?) {
        guard let reply = picking else { return }
        picking = nil
        guard var c else { reply(.success(Data())); return }
        if Self.access() == 3, let full = try? store.unifiedContact(withIdentifier: c.identifier, keysToFetch: Self.keys) { c = full }
        reply(Result { try JSONSerialization.data(withJSONObject: Self.json(c)) })
    }

    func closePicker() { picker?.close() }

    nonisolated func contactPicker(_ picker: CNContactPicker, didSelect contact: CNContact) {
        MainActor.assumeIsolated { picked(contact) }
    }

    nonisolated func contactPickerDidClose(_ picker: CNContactPicker) {
        MainActor.assumeIsolated { picked(nil) }
    }

    // MARK: The module

    func methods() -> [String: AsukaModuleMethod] {
        [
            "request": { [unowned self] _, reply in
                if Self.status() != 0 {
                    reply(.success(Data([Self.access()])))
                    return
                }
                self.request { reply(.success(Data([Self.access()]))) }
            },
            "list": { [unowned self] args, reply in
                guard Self.access() >= 2 else { reply(.failure(asukaNotAllowed)); return }
                let o = AsukaModules2Kit.object(args) ?? [:]
                let name = o["name"] as? String ?? "", limit = o["limit"] as? Int ?? 0
                let store = self.store
                AsukaModules2Kit.offMain(reply) {
                    let req = CNContactFetchRequest(keysToFetch: Self.keys)
                    req.sortOrder = .userDefault
                    if !name.isEmpty { req.predicate = CNContact.predicateForContacts(matchingName: name) }
                    var out: [[String: Any]] = []
                    do {
                        try store.enumerateContacts(with: req) { c, stop in
                            out.append(Self.json(c))
                            if limit > 0 && out.count >= limit { stop.pointee = true }
                        }
                    } catch {
                        throw AsukaModules2Kit.error(error, "couldn't read the contacts")
                    }
                    return try JSONSerialization.data(withJSONObject: out)
                }
            },
            "pick": { [unowned self] _, reply in
                if let p = self.picking { // one at a time
                    self.picking = nil
                    p(.success(Data()))
                }
                if self.picker == nil {
                    let p = CNContactPicker()
                    p.delegate = self
                    self.picker = p
                }
                self.picking = reply
                let v = AsukaRuntime.shared.keyWin.content
                // From the top of the window, hanging down (the content is
                // flipped: its max y edge is below).
                self.picker?.showRelative(to: CGRect(x: v.bounds.width / 2, y: 0, width: 1, height: 1), of: v, preferredEdge: .maxY)
            },
            "add": { [unowned self] args, reply in
                guard let json = AsukaModules2Kit.object(args) else { reply(.failure(AsukaModuleError("contacts.add: bad arguments"))); return }
                let o = Args(json)
                let (title, info) = Self.alertText(o)
                AsukaModules2Kit.confirm(title, info, asukaWord("addToContacts")) { [weak self] ok in
                    guard ok else { reply(.success(Data([0]))); return }
                    self?.save(o, reply)
                }
            },
        ]
    }
}
