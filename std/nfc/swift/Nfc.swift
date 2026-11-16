// asuka/nfc on iOS: Core NFC's reader sessions, with the system's sheet.
// A session that polls for nothing is an NFCNDEFReaderSession (NDEF
// tags, no identifiers or raw commands); one that polls is an
// NFCTagReaderSession (ISO 7816, MIFARE, FeliCa, ISO 15693), which reads
// Info.plist's AIDs and FeliCa system codes. One tag at a time: next()
// restarts polling for another. iPhones have no NFC switch, and tags
// open apps only as their universal links (the app's openURL). A Mac
// has no NFC for apps: not available.
import Foundation
import Asuka
#if os(iOS)
@preconcurrency import CoreNFC
#endif

final class CoreNFCReader: Reader {
    func state() throws -> State {
        #if os(iOS)
        let on = NFCNDEFReaderSession.readingAvailable
        return State(supported: on, enabled: on)
        #else
        return State()
        #endif
    }

    func watchState(changed: @escaping (State) -> Void) async throws {
        changed(try state())
        while !Task.isCancelled {
            try? await Task.sleep(nanoseconds: 3_600_000_000_000)
        }
    }

    func begin(options: Options) async throws -> Int {
        #if os(iOS)
        return try await MainActor.run { try Sessions.shared.begin(options) }
        #else
        throw NfcError.notAvailable
        #endif
    }

    func next(session: Int) async throws -> Found {
        #if os(iOS)
        return try await withCheckedThrowingContinuation { c in
            Task { @MainActor in Sessions.shared.next(session) { c.resume(with: $0) } }
        }
        #else
        throw NfcError.notAvailable
        #endif
    }

    func setAlert(session: Int, alert: String) throws {
        #if os(iOS)
        Task { @MainActor in Sessions.shared.setAlert(session, alert) }
        #endif
    }

    func end(session: Int, message: String) throws {
        #if os(iOS)
        Task { @MainActor in Sessions.shared.end(session, message) }
        #endif
    }

    func read(tag: Int) async throws -> Data {
        #if os(iOS)
        return try await withCheckedThrowingContinuation { c in
            Task { @MainActor in Sessions.shared.read(tag) { c.resume(with: $0) } }
        }
        #else
        throw NfcError.notAvailable
        #endif
    }

    func write(tag: Int, message: Data) async throws {
        #if os(iOS)
        try await withCheckedThrowingContinuation { (c: CheckedContinuation<Void, Error>) in
            Task { @MainActor in Sessions.shared.write(tag, message) { c.resume(with: $0) } }
        }
        #else
        throw NfcError.notAvailable
        #endif
    }

    func makeReadOnly(tag: Int) async throws {
        #if os(iOS)
        try await withCheckedThrowingContinuation { (c: CheckedContinuation<Void, Error>) in
            Task { @MainActor in Sessions.shared.lock(tag) { c.resume(with: $0) } }
        }
        #else
        throw NfcError.notAvailable
        #endif
    }

    func transceive(tag: Int, data: Data) async throws -> Data {
        #if os(iOS)
        return try await withCheckedThrowingContinuation { c in
            Task { @MainActor in Sessions.shared.transceive(tag, data) { c.resume(with: $0) } }
        }
        #else
        throw NfcError.notAvailable
        #endif
    }

    // Tags open an iPhone app only as its universal links (openURL).
    func opened(tag: @escaping (Found) -> Void) async throws {
        while !Task.isCancelled {
            try? await Task.sleep(nanoseconds: 3_600_000_000_000)
        }
    }

    func openSettings() async throws { throw NfcError.notAvailable }
}

#if os(iOS)
@MainActor final class Sessions: NSObject, NFCNDEFReaderSessionDelegate, NFCTagReaderSessionDelegate {
    static let shared = Sessions()
    typealias Done<T> = (Swift.Result<T, Error>) -> Void

    enum Held: @unchecked Sendable {
        case ndef(NFCNDEFTag)
        case tag(NFCTag)
        var ndef: NFCNDEFTag {
            switch self {
            case .ndef(let t): return t
            case .tag(let t):
                switch t {
                case .miFare(let m): return m
                case .iso7816(let i): return i
                case .feliCa(let f): return f
                case .iso15693(let v): return v
                @unknown default: fatalError("a tag Core NFC added")
                }
            }
        }
    }

    final class Session {
        let id: Int
        let skipNDEF: Bool
        var reader: NFCReaderSession?
        var waiting: Done<Found>?
        var queue: [Found] = []
        var looking = true
        var held: Held?
        var handle = 0
        init(id: Int, skipNDEF: Bool) { self.id = id; self.skipNDEF = skipNDEF }
    }

    private var session: Session?
    /// A session the app ended, until its delegate hears it's over, and
    /// the next one, which begins then (Core NFC begins none while
    /// another closes).
    private var closing: NFCReaderSession?
    private var toBegin: NFCReaderSession?
    private var ids = 0
    private var handles = 0
    private var lastEnded: (id: Int, error: Error)?

    func begin(_ o: Options) throws -> Int {
        guard NFCNDEFReaderSession.readingAvailable else { throw NfcError.notAvailable }
        if let s = session { close(s, "") }
        ids += 1
        let s = Session(id: ids, skipNDEF: o.skipNDEF)
        let reader: NFCReaderSession
        if o.poll.isEmpty {
            reader = NFCNDEFReaderSession(delegate: self, queue: .main, invalidateAfterFirstRead: false)
        } else {
            var opts: NFCTagReaderSession.PollingOption = []
            if o.poll.contains(.a) || o.poll.contains(.b) { opts.insert(.iso14443) }
            if o.poll.contains(.f) { opts.insert(.iso18092) }
            if o.poll.contains(.v) { opts.insert(.iso15693) }
            guard let r = NFCTagReaderSession(pollingOption: opts, delegate: self, queue: .main) else { throw NfcError.notSupported }
            reader = r
        }
        if !o.alert.isEmpty { reader.alertMessage = o.alert }
        s.reader = reader
        session = s
        if closing != nil { toBegin = reader } else { reader.begin() }
        return s.id
    }

    func next(_ id: Int, _ done: @escaping Done<Found>) {
        guard let s = session, s.id == id else {
            if let l = lastEnded, l.id == id { done(.failure(l.error)) } else { done(.failure(NfcError.canceled)) }
            return
        }
        if !s.queue.isEmpty { done(.success(s.queue.removeFirst())); return }
        if s.held != nil {
            // The app is done with the tag it held: look for another.
            s.held = nil
            s.handle = 0
            s.looking = true
            if let r = s.reader as? NFCTagReaderSession { r.restartPolling() }
            if let r = s.reader as? NFCNDEFReaderSession { r.restartPolling() }
        }
        s.waiting = done
    }

    func setAlert(_ id: Int, _ alert: String) {
        if let s = session, s.id == id { s.reader?.alertMessage = alert }
    }

    func end(_ id: Int, _ failure: String) {
        if let s = session, s.id == id { close(s, failure) }
    }

    /// close ends s as the app asks: the sheet closes with its tick, or
    /// with failure.
    private func close(_ s: Session, _ failure: String) {
        guard let reader = s.reader else { finish(s, NfcError.canceled); return }
        finish(s, NfcError.canceled)
        if toBegin === reader {
            toBegin = nil
            return
        }
        closing = reader
        if failure.isEmpty { reader.invalidate() } else { reader.invalidate(errorMessage: failure) }
    }

    /// finish ends s (once): its waiting next fails with e, which a next
    /// after gets too.
    private func finish(_ s: Session, _ e: Error) {
        guard session === s else { return }
        session = nil
        lastEnded = (s.id, e)
        s.held = nil
        s.reader = nil
        if let w = s.waiting {
            s.waiting = nil
            w(.failure(e))
        }
    }

    private func deliver(_ s: Session, _ held: Held, _ f: Found) {
        guard session === s else { return }
        handles += 1
        s.held = held
        s.handle = handles
        var found = f
        found.handle = handles
        if let w = s.waiting {
            s.waiting = nil
            w(.success(found))
        } else {
            s.queue.append(found)
        }
    }

    private func held(_ h: Int) -> Held? {
        guard let s = session, h != 0, s.handle == h else { return nil }
        return s.held
    }

    // The delegates (on the main queue, the sessions').

    nonisolated func readerSession(_ session: NFCNDEFReaderSession, didInvalidateWithError error: Error) {
        MainActor.assumeIsolated { self.invalidated(session, error) }
    }

    nonisolated func readerSession(_ session: NFCNDEFReaderSession, didDetectNDEFs messages: [NFCNDEFMessage]) {}

    nonisolated func readerSession(_ session: NFCNDEFReaderSession, didDetect tags: [NFCNDEFTag]) {
        MainActor.assumeIsolated {
            guard let s = self.session, s.reader === session, s.looking, let tag = tags.first else { return }
            s.looking = false
            session.connect(to: tag) { err in
                DispatchQueue.main.async {
                    MainActor.assumeIsolated {
                        if err != nil {
                            s.looking = true
                            session.restartPolling()
                            return
                        }
                        self.found(s, .ndef(tag), Found(techs: ["ndef"]))
                    }
                }
            }
        }
    }

    nonisolated func tagReaderSessionDidBecomeActive(_ session: NFCTagReaderSession) {}

    nonisolated func tagReaderSession(_ session: NFCTagReaderSession, didInvalidateWithError error: Error) {
        MainActor.assumeIsolated { self.invalidated(session, error) }
    }

    nonisolated func tagReaderSession(_ session: NFCTagReaderSession, didDetect tags: [NFCTag]) {
        MainActor.assumeIsolated {
            guard let s = self.session, s.reader === session, s.looking, let tag = tags.first else { return }
            s.looking = false
            session.connect(to: tag) { err in
                DispatchQueue.main.async {
                    MainActor.assumeIsolated {
                        if err != nil {
                            s.looking = true
                            session.restartPolling()
                            return
                        }
                        self.found(s, .tag(tag), Self.info(tag))
                    }
                }
            }
        }
    }

    private func invalidated(_ reader: NFCReaderSession, _ error: Error) {
        if reader === closing {
            closing = nil
            if let next = toBegin {
                toBegin = nil
                next.begin()
            }
            return
        }
        guard let s = session, s.reader === reader else { return }
        let e: Error
        switch (error as? NFCReaderError)?.code {
        case .readerSessionInvalidationErrorUserCanceled: e = NfcError.canceled
        case .readerSessionInvalidationErrorSessionTimeout: e = NfcError.timeout
        case .readerErrorUnsupportedFeature: e = NfcError.notAvailable
        case .readerErrorRadioDisabled: e = NfcError.disabled
        default: e = error
        }
        finish(s, e)
    }

    /// found reads the tag's NDEF status and message (unless the session
    /// skips them), then hands it over.
    private func found(_ s: Session, _ held: Held, _ info: Found) {
        var f = info
        if s.skipNDEF {
            deliver(s, held, f)
            return
        }
        let t = held.ndef
        t.queryNDEFStatus { status, capacity, err in
            DispatchQueue.main.async {
                MainActor.assumeIsolated {
                    if err != nil || status == .notSupported {
                        f.techs.removeAll { $0 == "ndef" }
                        self.deliver(s, held, f)
                        return
                    }
                    if !f.techs.contains("ndef") { f.techs.insert("ndef", at: 0) }
                    f.format = status == .readOnly ? .readOnly : .readWrite
                    f.capacity = capacity
                    f.canMakeReadOnly = status == .readWrite
                    t.readNDEF { m, err in
                        DispatchQueue.main.async {
                            MainActor.assumeIsolated {
                                if let m {
                                    f.hasMessage = true
                                    f.message = Self.encode(m)
                                } else if (err as? NFCReaderError)?.code == .ndefReaderSessionErrorZeroLengthMessage {
                                    f.hasMessage = true
                                    f.message = Data([0xd0, 0, 0])
                                }
                                self.deliver(s, held, f)
                            }
                        }
                    }
                }
            }
        }
    }

    /// A tag's identity and technologies.
    private static func info(_ tag: NFCTag) -> Found {
        switch tag {
        case .miFare(let t):
            var techs = ["ndef", "mifare"]
            if t.mifareFamily == .desfire || t.mifareFamily == .plus { techs.append("iso7816") }
            if t.mifareFamily == .ultralight { techs.append("mifareUltralight") }
            return Found(id: t.identifier, techs: techs)
        case .iso7816(let t): return Found(id: t.identifier, techs: ["ndef", "iso7816", "isoDep"])
        case .feliCa(let t): return Found(id: t.currentIDm, techs: ["ndef", "felica"])
        case .iso15693(let t): return Found(id: t.identifier, techs: ["ndef", "iso15693"])
        @unknown default: return Found(techs: ["ndef"])
        }
    }

    private static func failure(_ err: Error?) -> Error {
        guard let err else { return NfcError.tagLost }
        switch (err as? NFCReaderError)?.code {
        case .readerTransceiveErrorTagConnectionLost, .readerTransceiveErrorTagNotConnected, .readerTransceiveErrorSessionInvalidated:
            return NfcError.tagLost
        case .ndefReaderSessionErrorTagNotWritable: return NfcError.readOnly
        case .ndefReaderSessionErrorTagSizeTooSmall: return NfcError.tooLarge
        case .readerErrorUnsupportedFeature: return NfcError.notSupported
        default: return err
        }
    }

    func read(_ h: Int, _ done: @escaping Done<Data>) {
        guard let t = held(h) else { done(.failure(NfcError.tagLost)); return }
        t.ndef.readNDEF { m, err in
            DispatchQueue.main.async {
                if let m { done(.success(Self.encode(m))); return }
                if (err as? NFCReaderError)?.code == .ndefReaderSessionErrorZeroLengthMessage { done(.success(Data([0xd0, 0, 0]))); return }
                done(.failure(Self.failure(err)))
            }
        }
    }

    func write(_ h: Int, _ bytes: Data, _ done: @escaping Done<Void>) {
        guard let t = held(h) else { done(.failure(NfcError.tagLost)); return }
        guard let m = NFCNDEFMessage(data: bytes) else { done(.failure(NfcError.badMessage)); return }
        let tag = t.ndef
        tag.queryNDEFStatus { status, capacity, err in
            DispatchQueue.main.async {
                if let err { done(.failure(Self.failure(err))); return }
                switch status {
                case .notSupported: done(.failure(NfcError.notSupported)); return
                case .readOnly: done(.failure(NfcError.readOnly)); return
                default: break
                }
                if bytes.count > capacity { done(.failure(NfcError.tooLarge)); return }
                tag.writeNDEF(m) { err in DispatchQueue.main.async { done(err.map { .failure(Self.failure($0)) } ?? .success(())) } }
            }
        }
    }

    func lock(_ h: Int, _ done: @escaping Done<Void>) {
        guard let t = held(h) else { done(.failure(NfcError.tagLost)); return }
        t.ndef.writeLock { err in DispatchQueue.main.async { done(err.map { .failure(Self.failure($0)) } ?? .success(())) } }
    }

    func transceive(_ h: Int, _ data: Data, _ done: @escaping Done<Data>) {
        guard let t = held(h) else { done(.failure(NfcError.tagLost)); return }
        guard case .tag(let tag) = t else { done(.failure(NfcError.notSupported)); return }
        let reply: (Data?, Error?) -> Void = { d, err in
            DispatchQueue.main.async { done(err.map { .failure(Self.failure($0)) } ?? .success(d ?? Data())) }
        }
        let apdu: (Data, UInt8, UInt8, Error?) -> Void = { d, sw1, sw2, err in reply(d + [sw1, sw2], err) }
        switch tag {
        case .iso7816(let c):
            guard let a = NFCISO7816APDU(data: data) else { done(.failure(NfcError.notSupported)); return }
            c.sendCommand(apdu: a, completionHandler: apdu)
        case .miFare(let c) where c.mifareFamily == .desfire || c.mifareFamily == .plus:
            guard let a = NFCISO7816APDU(data: data) else { done(.failure(NfcError.notSupported)); return }
            c.sendMiFareISO7816Command(a, completionHandler: apdu)
        case .miFare(let c):
            c.sendMiFareCommand(commandPacket: data, completionHandler: reply)
        case .feliCa(let c):
            c.sendFeliCaCommand(commandPacket: data, completionHandler: reply)
        case .iso15693(let c):
            guard data.count >= 2 else { done(.failure(NfcError.notSupported)); return }
            let rest = data.count > 2 ? data.subdata(in: data.startIndex + 2..<data.endIndex) : nil
            c.sendRequest(requestFlags: Int(data[data.startIndex]), commandCode: Int(data[data.startIndex + 1]), data: rest) { r in
                switch r {
                case .success(let (flags, d)): reply(Data([UInt8(truncatingIfNeeded: flags.rawValue)]) + (d ?? Data()), nil)
                case .failure(let e): reply(nil, e)
                }
            }
        @unknown default:
            done(.failure(NfcError.notSupported))
        }
    }

    /// A message's bytes (NFC Forum's layout: short records where they
    /// fit), which Core NFC doesn't give.
    static func encode(_ m: NFCNDEFMessage) -> Data {
        let rs = m.records
        if rs.isEmpty { return Data([0xd0, 0, 0]) }
        var b = Data()
        for (i, r) in rs.enumerated() {
            var h = UInt8(r.typeNameFormat.rawValue) & 0x07
            if i == 0 { h |= 0x80 }
            if i == rs.count - 1 { h |= 0x40 }
            let short = r.payload.count < 256
            if short { h |= 0x10 }
            if !r.identifier.isEmpty { h |= 0x08 }
            b.append(h)
            b.append(UInt8(r.type.count))
            if short {
                b.append(UInt8(r.payload.count))
            } else {
                let n = UInt32(r.payload.count)
                b.append(contentsOf: [UInt8(n >> 24), UInt8(n >> 16 & 0xff), UInt8(n >> 8 & 0xff), UInt8(n & 0xff)])
            }
            if !r.identifier.isEmpty { b.append(UInt8(r.identifier.count)) }
            b.append(r.type)
            b.append(r.identifier)
            b.append(r.payload)
        }
        return b
    }
}
#endif

func makeReader() throws -> any Reader { CoreNFCReader() }
