// TLS.swift: std/http's TLS settings (http.TLS) as URLSession applies
// them, on iOS and macOS. The same file is std/websocket's
// (std/websocket/swift/TLS.swift; std's tests keep them alike).
//
// The authorities to trust are SecTrust's anchors, set on the server's
// trust as its challenge comes (SecTrustSetAnchorCertificates, and
// SecTrustSetAnchorCertificatesOnly unless the system's are trusted
// too), the host's name checked as ever. The client's certificate and key
// become a SecIdentity without the user's keychain: a PKCS#12 made here,
// in memory, imported with SecPKCS12Import (in memory on iOS, and on
// macOS 15 and later; before 15, macOS imports into a keychain, so into
// one of the process's own in its temporary folder, deleted as it exits).
import Foundation
import Security
import CommonCrypto

/// A client's TLS settings, read once: the same settings, the same one.
final class TLSPolicy: @unchecked Sendable {
    let anchors: [SecCertificate]
    let anchorsOnly: Bool
    let identity: SecIdentity?
    /// The client certificate's chain after it: what it's shown with.
    let intermediates: [SecCertificate]
    let insecure: Bool

    /// For tests: macOS's import into a keychain of the process's own,
    /// as before macOS 15, on any.
    nonisolated(unsafe) static var keychainImport = false

    private static let lock = NSLock()
    nonisolated(unsafe) private static var made: [String: TLSPolicy] = [:]

    private init(anchors: [SecCertificate], anchorsOnly: Bool, identity: SecIdentity?, intermediates: [SecCertificate], insecure: Bool) {
        self.anchors = anchors
        self.anchorsOnly = anchorsOnly
        self.identity = identity
        self.intermediates = intermediates
        self.insecure = insecure
    }

    /// The policy for these settings (http.TLS's fields), made the first
    /// time they're asked for. It throws for what it can't read.
    static func of(ca: String, trustSystem: Bool, certificate: String, key: String, insecure: Bool) throws -> TLSPolicy {
        let id = [ca, trustSystem ? "1" : "0", certificate, key, insecure ? "1" : "0"].joined(separator: "\u{0}")
        lock.lock()
        defer { lock.unlock() }
        if let p = made[id] { return p }
        var anchors: [SecCertificate] = []
        for b in PEM.blocks(ca) where b.label == "CERTIFICATE" {
            guard let c = SecCertificateCreateWithData(nil, b.der as CFData) else {
                throw TLSPolicy.error("ca: a certificate can't be read")
            }
            anchors.append(c)
        }
        if !ca.isEmpty && anchors.isEmpty {
            throw TLSPolicy.error("ca has no PEM certificate")
        }
        var identity: SecIdentity?
        var intermediates: [SecCertificate] = []
        if !certificate.isEmpty || !key.isEmpty {
            let chain = PEM.blocks(certificate).filter { $0.label == "CERTIFICATE" }.map(\.der)
            guard let leaf = chain.first else { throw TLSPolicy.error("certificate has no PEM certificate") }
            for der in chain.dropFirst() {
                guard let c = SecCertificateCreateWithData(nil, der as CFData) else {
                    throw TLSPolicy.error("certificate: a certificate of its chain can't be read")
                }
                intermediates.append(c)
            }
            guard SecCertificateCreateWithData(nil, leaf as CFData) != nil else {
                throw TLSPolicy.error("certificate can't be read")
            }
            identity = try PKCS12.identity(certificate: leaf, key: try PEM.privateKey(key))
        }
        let p = TLSPolicy(anchors: anchors, anchorsOnly: !trustSystem, identity: identity, intermediates: intermediates, insecure: insecure)
        made[id] = p
        return p
    }

    /// Answers an authentication challenge: the server's trust checked
    /// against the anchors (or not at all, insecure), the client's
    /// certificate shown when it's asked for, anything else the system's.
    /// A server that isn't trusted has its challenge cancelled: this
    /// returns why, for the task's error (untrusted).
    func answer(_ c: URLAuthenticationChallenge, _ done: @escaping (URLSession.AuthChallengeDisposition, URLCredential?) -> Void) -> String? {
        let space = c.protectionSpace
        switch space.authenticationMethod {
        case NSURLAuthenticationMethodServerTrust:
            guard let trust = space.serverTrust else {
                done(.performDefaultHandling, nil)
                return nil
            }
            if insecure {
                done(.useCredential, URLCredential(trust: trust))
                return nil
            }
            if anchors.isEmpty {
                done(.performDefaultHandling, nil)
                return nil
            }
            SecTrustSetAnchorCertificates(trust, anchors as CFArray)
            SecTrustSetAnchorCertificatesOnly(trust, anchorsOnly)
            var e: CFError?
            if SecTrustEvaluateWithError(trust, &e) {
                done(.useCredential, URLCredential(trust: trust))
                return nil
            }
            done(.cancelAuthenticationChallenge, nil)
            let why = e.map { CFErrorCopyDescription($0) as String } ?? "it isn't signed by an authority trusted"
            return "\(space.host)'s certificate isn't trusted: \(why)"
        case NSURLAuthenticationMethodClientCertificate:
            if let identity {
                done(.useCredential, URLCredential(identity: identity, certificates: intermediates, persistence: .forSession))
            } else {
                done(.performDefaultHandling, nil)
            }
            return nil
        default:
            done(.performDefaultHandling, nil)
            return nil
        }
    }

    /// The error of a task whose server wasn't trusted (why: answer's).
    static func untrusted(_ why: String) -> Error {
        URLError(.serverCertificateUntrusted, userInfo: [NSLocalizedDescriptionKey: why])
    }

    static func error(_ message: String) -> Error {
        NSError(domain: "std/http TLS", code: 1, userInfo: [NSLocalizedDescriptionKey: "tls: " + message])
    }
}

/// What tasks' challenges said of their servers, for their errors: a
/// task whose server wasn't trusted ends cancelled, and its error is why.
final class TrustFailures: @unchecked Sendable {
    private let lock = NSLock()
    private var why: [Int: String] = [:]

    func record(_ task: URLSessionTask, _ reason: String) {
        lock.lock()
        why[task.taskIdentifier] = reason
        lock.unlock()
    }

    /// The task's error, as it ended: why its server wasn't trusted when
    /// that's why it ended. Each task's once.
    func error(_ task: URLSessionTask, _ error: Error?) -> Error? {
        lock.lock()
        let w = why.removeValue(forKey: task.taskIdentifier)
        lock.unlock()
        if let w, error != nil { return TLSPolicy.untrusted(w) }
        return error
    }
}

// ---- PEM and DER ----

enum PEM {
    /// The blocks of text: each one's label and its bytes (headers, as
    /// an encrypted key's, skipped).
    static func blocks(_ text: String) -> [(label: String, der: Data)] {
        var out: [(String, Data)] = []
        var label: String?
        var body = ""
        for raw in text.split(omittingEmptySubsequences: false, whereSeparator: { $0 == "\n" || $0 == "\r" }) {
            let line = raw.trimmingCharacters(in: .whitespaces)
            if let l = label {
                if line == "-----END \(l)-----" {
                    if let d = Data(base64Encoded: body) { out.append((l, d)) }
                    label = nil
                } else if !line.contains(":") {
                    body += line
                }
            } else if line.hasPrefix("-----BEGIN "), line.hasSuffix("-----") {
                label = String(line.dropFirst(11).dropLast(5))
                body = ""
            }
        }
        return out
    }

    /// The private key of text as PKCS#8's PrivateKeyInfo: PRIVATE KEY
    /// as it is, RSA PRIVATE KEY (PKCS#1) and EC PRIVATE KEY (SEC 1)
    /// wrapped in one. (An encrypted key, ENCRYPTED PRIVATE KEY, would be
    /// decrypted here.)
    static func privateKey(_ text: String) throws -> Data {
        for b in blocks(text) {
            switch b.label {
            case "PRIVATE KEY":
                return b.der
            case "RSA PRIVATE KEY":
                return DER.seq(DER.int(0) + DER.seq(DER.oid("1.2.840.113549.1.1.1") + DER.null) + DER.octets(b.der))
            case "EC PRIVATE KEY":
                guard let curve = DER.curve(ofSEC1: b.der) else {
                    throw TLSPolicy.error("key: an EC key of a curve that isn't P-256, P-384 or P-521")
                }
                return DER.seq(DER.int(0) + DER.seq(DER.oid("1.2.840.10045.2.1") + curve) + DER.octets(b.der))
            default:
                continue
            }
        }
        throw TLSPolicy.error("key has no PEM private key (PRIVATE KEY, RSA PRIVATE KEY or EC PRIVATE KEY)")
    }
}

/// DER, as much as a PKCS#12 and a key need.
enum DER {
    static func tlv(_ tag: UInt8, _ content: Data) -> Data {
        var d = Data([tag])
        let n = content.count
        if n < 0x80 {
            d.append(UInt8(n))
        } else {
            var len: [UInt8] = []
            var m = n
            while m > 0 {
                len.insert(UInt8(m & 0xff), at: 0)
                m >>= 8
            }
            d.append(0x80 | UInt8(len.count))
            d.append(contentsOf: len)
        }
        d.append(content)
        return d
    }

    static func seq(_ c: Data) -> Data { tlv(0x30, c) }
    static func set(_ c: Data) -> Data { tlv(0x31, c) }
    static func octets(_ c: Data) -> Data { tlv(0x04, c) }
    /// [0] EXPLICIT.
    static func explicit0(_ c: Data) -> Data { tlv(0xa0, c) }
    static let null = Data([0x05, 0x00])

    static func int(_ v: Int) -> Data {
        var bytes: [UInt8] = []
        var m = v
        repeat {
            bytes.insert(UInt8(m & 0xff), at: 0)
            m >>= 8
        } while m > 0
        if bytes[0] & 0x80 != 0 { bytes.insert(0, at: 0) }
        return tlv(0x02, Data(bytes))
    }

    static func oid(_ dotted: String) -> Data {
        let arcs = dotted.split(separator: ".").map { Int($0)! }
        var d = Data([UInt8(arcs[0] * 40 + arcs[1])])
        for a in arcs.dropFirst(2) {
            var b: [UInt8] = [UInt8(a & 0x7f)]
            var m = a >> 7
            while m > 0 {
                b.insert(UInt8(0x80 | (m & 0x7f)), at: 0)
                m >>= 7
            }
            d.append(contentsOf: b)
        }
        return tlv(0x06, d)
    }

    /// The element at d[at...]: its tag, its content's range, and where
    /// the next one starts.
    static func element(_ d: [UInt8], _ at: Int) -> (tag: UInt8, content: Range<Int>, next: Int)? {
        guard at + 2 <= d.count else { return nil }
        let tag = d[at]
        var n = Int(d[at + 1])
        var p = at + 2
        if n & 0x80 != 0 {
            let k = n & 0x7f
            guard k > 0, k <= 4, p + k <= d.count else { return nil }
            n = 0
            for i in 0..<k { n = n << 8 | Int(d[p + i]) }
            p += k
        }
        guard p + n <= d.count else { return nil }
        return (tag, p..<(p + n), p + n)
    }

    /// The curve's OID (its whole element) of a SEC 1 ECPrivateKey: its
    /// [0] parameters, else the one its key's length says.
    static func curve(ofSEC1 der: Data) -> Data? {
        let d = [UInt8](der)
        guard let top = element(d, 0), top.tag == 0x30 else { return nil }
        var at = top.content.lowerBound
        var keyLength = 0
        while at < top.content.upperBound, let e = element(d, at) {
            if e.tag == 0x04 { keyLength = e.content.count }
            if e.tag == 0xa0, let o = element(d, e.content.lowerBound), o.tag == 0x06 {
                return Data(d[e.content.lowerBound..<o.next])
            }
            at = e.next
        }
        switch keyLength {
        case 32: return oid("1.2.840.10045.3.1.7")
        case 48: return oid("1.3.132.0.34")
        case 66: return oid("1.3.132.0.35")
        default: return nil
        }
    }
}

// ---- A PKCS#12 of a certificate and its key, for SecPKCS12Import ----

enum PKCS12 {
    static let iterations = 2048

    /// The identity of certificate (DER) and key (PKCS#8 DER).
    static func identity(certificate: Data, key: Data) throws -> SecIdentity {
        let password = (0..<16).map { _ in String(format: "%02x", UInt8.random(in: 0...255)) }.joined()
        let p12 = try make(certificate: certificate, key: key, password: password)
        var options: [String: Any] = [kSecImportExportPassphrase as String: password]
        #if os(macOS)
        if #available(macOS 15.0, *), !TLSPolicy.keychainImport {
            options[kSecImportToMemoryOnly as String] = true
        } else {
            options[kSecImportExportKeychain as String] = try ProcessKeychain.shared()
        }
        #endif
        var items: CFArray?
        let status = SecPKCS12Import(p12 as CFData, options as CFDictionary, &items)
        guard status == errSecSuccess else {
            let why = SecCopyErrorMessageString(status, nil) as String? ?? "status \(status)"
            throw TLSPolicy.error("the key can't be used with the certificate (it isn't its key, or can't be read): \(why)")
        }
        guard let first = (items as? [[String: Any]])?.first, let any = first[kSecImportItemIdentity as String] else {
            throw TLSPolicy.error("the key isn't the certificate's")
        }
        return any as! SecIdentity
    }

    /// The PFX (RFC 7292): a bag of the certificate and one of the key
    /// shrouded with pbeWithSHAAnd3-KeyTripleDES-CBC, each with the same
    /// localKeyID, MAC'd with HMAC-SHA1, under password.
    static func make(certificate: Data, key: Data, password: String) throws -> Data {
        let pw = bmp(password)
        let keyID = sha1(certificate)
        let attrs = DER.set(DER.seq(DER.oid("1.2.840.113549.1.9.21") + DER.set(DER.octets(keyID))))
        let certBag = DER.seq(DER.oid("1.2.840.113549.1.12.10.1.3") + DER.explicit0(DER.seq(DER.oid("1.2.840.113549.1.9.22.1") + DER.explicit0(DER.octets(certificate)))) + attrs)
        let salt = random(8)
        let encrypted = try tripleDES(key, key: kdf(pw, salt, id: 1, count: 24), iv: kdf(pw, salt, id: 2, count: 8))
        let shrouded = DER.seq(DER.seq(DER.oid("1.2.840.113549.1.12.1.3") + DER.seq(DER.octets(Data(salt)) + DER.int(iterations))) + DER.octets(encrypted))
        let keyBag = DER.seq(DER.oid("1.2.840.113549.1.12.10.1.2") + DER.explicit0(shrouded) + attrs)
        func data(_ content: Data) -> Data { DER.seq(DER.oid("1.2.840.113549.1.7.1") + DER.explicit0(DER.octets(content))) }
        let authSafe = DER.seq(data(DER.seq(certBag)) + data(DER.seq(keyBag)))
        let macSalt = random(8)
        let mac = hmacSHA1(kdf(pw, macSalt, id: 3, count: 20), authSafe)
        let macData = DER.seq(DER.seq(DER.seq(DER.oid("1.3.14.3.2.26") + DER.null) + DER.octets(mac)) + DER.octets(Data(macSalt)) + DER.int(iterations))
        return DER.seq(DER.int(3) + data(authSafe) + macData)
    }

    /// A password as PKCS#12 hashes it: UTF-16BE, NUL-terminated.
    static func bmp(_ s: String) -> [UInt8] {
        var out: [UInt8] = []
        for u in s.utf16 {
            out.append(UInt8(u >> 8))
            out.append(UInt8(u & 0xff))
        }
        return out + [0, 0]
    }

    /// RFC 7292's appendix B key derivation, with SHA-1.
    static func kdf(_ password: [UInt8], _ salt: [UInt8], id: UInt8, count: Int) -> [UInt8] {
        let u = 20, v = 64
        func fill(_ s: [UInt8]) -> [UInt8] {
            if s.isEmpty { return [] }
            let n = v * ((s.count + v - 1) / v)
            return (0..<n).map { s[$0 % s.count] }
        }
        let d = [UInt8](repeating: id, count: v)
        var i = fill(salt) + fill(password)
        var out: [UInt8] = []
        while out.count < count {
            var a = sha1(Data(d + i))
            for _ in 1..<iterations { a = sha1(a) }
            let ab = [UInt8](a)
            out += ab
            let b = (0..<v).map { ab[$0 % u] }
            var j = 0
            while j < i.count {
                var carry = 1
                for k in stride(from: v - 1, through: 0, by: -1) {
                    let s = Int(i[j + k]) + Int(b[k]) + carry
                    i[j + k] = UInt8(s & 0xff)
                    carry = s >> 8
                }
                j += v
            }
        }
        return Array(out.prefix(count))
    }

    static func sha1(_ d: Data) -> Data {
        var out = [UInt8](repeating: 0, count: Int(CC_SHA1_DIGEST_LENGTH))
        d.withUnsafeBytes { _ = CC_SHA1($0.baseAddress, CC_LONG(d.count), &out) }
        return Data(out)
    }

    static func hmacSHA1(_ key: [UInt8], _ d: Data) -> Data {
        var out = [UInt8](repeating: 0, count: Int(CC_SHA1_DIGEST_LENGTH))
        d.withUnsafeBytes { CCHmac(CCHmacAlgorithm(kCCHmacAlgSHA1), key, key.count, $0.baseAddress, d.count, &out) }
        return Data(out)
    }

    static func tripleDES(_ plain: Data, key: [UInt8], iv: [UInt8]) throws -> Data {
        var out = [UInt8](repeating: 0, count: plain.count + kCCBlockSize3DES)
        var moved = 0
        let status = plain.withUnsafeBytes { p in
            CCCrypt(CCOperation(kCCEncrypt), CCAlgorithm(kCCAlgorithm3DES), CCOptions(kCCOptionPKCS7Padding), key, key.count, iv, p.baseAddress, plain.count, &out, out.count, &moved)
        }
        guard status == kCCSuccess else { throw TLSPolicy.error("the key can't be wrapped (\(status))") }
        return Data(out.prefix(moved))
    }

    static func random(_ n: Int) -> [UInt8] { (0..<n).map { _ in UInt8.random(in: 0...255) } }
}

#if os(macOS)
/// The keychain of the process's own that macOS before 15 imports a
/// PKCS#12 into: in its temporary folder, locked with a password no one
/// keeps, deleted as the process exits. (SecKeychain's functions are
/// deprecated, and found at run time: what's before macOS 15 has no
/// other way.)
enum ProcessKeychain {
    private typealias Create = @convention(c) (UnsafePointer<CChar>, UInt32, UnsafeRawPointer?, DarwinBoolean, CFTypeRef?, UnsafeMutablePointer<Unmanaged<CFTypeRef>?>) -> OSStatus
    private typealias Delete = @convention(c) (CFTypeRef) -> OSStatus
    nonisolated(unsafe) private static var keychain: CFTypeRef?

    static func shared() throws -> CFTypeRef {
        if let k = keychain { return k }
        guard let c = dlsym(UnsafeMutableRawPointer(bitPattern: -2), "SecKeychainCreate") else {
            throw TLSPolicy.error("no keychain for the client certificate")
        }
        let create = unsafeBitCast(c, to: Create.self)
        let path = (NSTemporaryDirectory() as NSString).appendingPathComponent("asuka-tls-\(ProcessInfo.processInfo.processIdentifier)-\(UUID().uuidString).keychain-db")
        let password = UUID().uuidString
        var out: Unmanaged<CFTypeRef>?
        let status = create(path, UInt32(password.utf8.count), password, false, nil, &out)
        guard status == errSecSuccess, let k = out?.takeRetainedValue() else {
            throw TLSPolicy.error("no keychain for the client certificate (\(status))")
        }
        keychain = k
        atexit {
            if let k = ProcessKeychain.keychain, let d = dlsym(UnsafeMutableRawPointer(bitPattern: -2), "SecKeychainDelete") {
                _ = unsafeBitCast(d, to: Delete.self)(k)
            }
        }
        return k
    }
}
#endif
