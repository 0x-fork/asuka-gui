package std

import (
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/egoist/asuka/internal/testserver"
)

// std/http's Swift (std/http/swift/Http.swift) on this Mac: its
// URLSession download, compiled with the generated glue's Response and
// Transport and a driver of the test's, against a local server: the
// progress as it comes, the file renamed into place, cancelling.
func TestHTTPSwiftDownload(t *testing.T) {
	if runtime.GOOS != "darwin" {
		t.Skip("URLSession: macOS")
	}
	if _, err := exec.LookPath("swiftc"); err != nil {
		t.Skip("no swiftc")
	}
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/big":
			w.Header().Set("Content-Length", "1048576")
			chunk := strings.Repeat("a", 64*1024)
			for i := 0; i < 16; i++ {
				fmt.Fprint(w, chunk)
				w.(http.Flusher).Flush()
				time.Sleep(30 * time.Millisecond)
			}
		case "/stall":
			w.Header().Set("Content-Length", "10")
			fmt.Fprint(w, "abc")
			w.(http.Flusher).Flush()
			select {
			case <-r.Context().Done():
			case <-time.After(10 * time.Second):
			}
		}
	}))
	defer srv.Close()
	dir := t.TempDir()
	out := filepath.Join(dir, "big.bin")
	driver := `import Foundation
let t = try makeTransport()
var reports: [(Int, Int)] = []
let lock = NSLock()
let r = try await t.download(method: "GET", url: "` + srv.URL + `/big", headers: [:], body: Data(), timeout: 5, path: "` + out + `", progress: { got, total in
    lock.lock(); reports.append((got, total)); lock.unlock()
})
let size = (try FileManager.default.attributesOfItem(atPath: "` + out + `")[.size] as! NSNumber).intValue
lock.lock()
print("status \(r.status) body \(r.body.count) size \(size) reports \(reports.count > 1) last \(reports.last!.0) \(reports.last!.1) totals \(reports.allSatisfy { $0.1 == 1048576 })")
lock.unlock()
let stalled = Task {
    try await t.download(method: "GET", url: "` + srv.URL + `/stall", headers: [:], body: Data(), timeout: 0, path: "` + out + `.2", progress: { _, _ in })
}
try await Task.sleep(nanoseconds: 300_000_000)
stalled.cancel()
do {
    _ = try await stalled.value
    print("cancel: finished")
} catch {
    print("cancel: \((error as? URLError)?.code == .cancelled) \(FileManager.default.fileExists(atPath: "` + out + `.2"))")
}
`
	got := runHTTPProbe(t, dir, driver)
	for _, want := range []string{"status 200 body 0 size 1048576 reports true last 1048576 1048576 totals true", "cancel: true false"} {
		if !strings.Contains(got, want) {
			t.Errorf("want %q in:\n%s", want, got)
		}
	}
}

// goneServer serves testserver's paths, and /slowhead (a head that never
// comes), /trickle (its head and "a", then nothing) and /big (32 MiB in
// 64 KiB writes, the i-th all "a" + i%26), counting the requests whose
// client went before their answer ended: /gone?path=/stall says how many
// of that path's.
func goneServer() *httptest.Server {
	var mu sync.Mutex
	gone := map[string]int{}
	h := testserver.Handler()
	return httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/gone":
			mu.Lock()
			fmt.Fprint(w, gone[r.URL.Query().Get("path")])
			mu.Unlock()
			return
		case "/slowhead":
			<-r.Context().Done()
		case "/trickle":
			w.Header().Set("Content-Type", "text/plain")
			fmt.Fprint(w, "a")
			w.(http.Flusher).Flush()
			<-r.Context().Done()
		case "/big":
			for i := 0; i < 512 && r.Context().Err() == nil; i++ {
				fmt.Fprint(w, strings.Repeat(string(rune('a'+i%26)), 64*1024))
				w.(http.Flusher).Flush()
			}
		default:
			h.ServeHTTP(w, r)
		}
		if r.Context().Err() != nil {
			mu.Lock()
			gone[r.URL.Path]++
			mu.Unlock()
		}
	}))
}

// std/http's streamed bodies in Swift (Transport.open, Body) on this Mac:
// the pieces as they come (the first well before the last), a 404 that
// still opens, a request's body and headers, a stall's timeout (for the
// head, then for a piece), a read
// cancelled (the request going on until closed), an open cancelled, a
// body let go, and a reader slower than the network (the task suspended
// past 1 MiB unread, and resumed: nothing lost, in order).
func TestHTTPSwiftStream(t *testing.T) {
	if runtime.GOOS != "darwin" {
		t.Skip("URLSession: macOS")
	}
	if _, err := exec.LookPath("swiftc"); err != nil {
		t.Skip("no swiftc")
	}
	srv := goneServer()
	defer srv.Close()
	driver := `import Foundation
let t = try makeTransport()
let base = "` + srv.URL + `"
func now() -> Double { Date().timeIntervalSince1970 }
func readAll(_ b: any Body) async throws -> Data {
    var all = Data()
    while true {
        let d = try await b.read()
        if d.isEmpty { return all }
        all.append(d)
    }
}
// Waits for the server to see path's requests gone (the client hung up).
func gone(_ path: String, _ want: Int, tries: Int = 200) async -> Bool {
    for _ in 0..<tries {
        if let (d, _) = try? await URLSession.shared.data(from: URL(string: base + "/gone?path=" + path)!), Int(String(decoding: d, as: UTF8.self)) ?? 0 >= want {
            return true
        }
        try? await Task.sleep(nanoseconds: 25_000_000)
    }
    return false
}

let b = try await t.open(method: "GET", url: base + "/stream", headers: [:], body: Data(), timeout: 5)
let h = try b.head()
var text = ""
var times: [Double] = []
while true {
    let d = try await b.read()
    if d.isEmpty { break }
    times.append(now())
    text += String(decoding: d, as: UTF8.self)
}
let end = now()
print("stream \(h.status) \(h.headers["content-type"] ?? "-") \(text.debugDescription) pieces \(times.count >= 2) spread \(times.last! - times.first! > 0.06) early \(end - times.first! > 0.08)")
print("stream timings first→last \(Int((times.last! - times.first!) * 1000)) ms, first→end \(Int((end - times.first!) * 1000)) ms, \(times.count) pieces")
print("after end: \(try await b.read().count)")

let nf = try await t.open(method: "GET", url: base + "/status/404", headers: [:], body: Data(), timeout: 5)
print("404: \(try nf.head().status) \(String(decoding: try await readAll(nf), as: UTF8.self))")

let echo = try await t.open(method: "POST", url: base + "/echo", headers: ["X-A": "b"], body: Data("hi".utf8), timeout: 5)
let j = try JSONSerialization.jsonObject(with: try await readAll(echo)) as! [String: Any]
print("echo: \(j["method"]!) \(j["body"]!) \((j["headers"] as! [String: String])["x-a"]!)")

// URLSession hands over a response's head with its body's first bytes
// (CFNetwork holds it till then): a head and nothing more times out in
// open there, a head and some in read.
let s0 = now()
do {
    let stall = try await t.open(method: "GET", url: base + "/stall", headers: [:], body: Data(), timeout: 0.5)
    _ = try await stall.read()
    print("stall: read")
} catch {
    print("stall: timedOut \((error as? URLError)?.code == .timedOut) soon \(now() - s0 < 5)")
}
print("stall gone: \(await gone("/stall", 1))")
let trickle = try await t.open(method: "GET", url: base + "/trickle", headers: [:], body: Data(), timeout: 0.5)
let s1 = now()
let first = try await trickle.read()
do {
    _ = try await trickle.read()
    print("trickle: read")
} catch {
    print("trickle: \(try trickle.head().status) \(String(decoding: first, as: UTF8.self)) timedOut \((error as? URLError)?.code == .timedOut) waited \(now() - s1 >= 0.4) soon \(now() - s1 < 5)")
}
print("trickle gone: \(await gone("/trickle", 1))")

let held = try await t.open(method: "GET", url: base + "/trickle", headers: [:], body: Data(), timeout: 0)
_ = try await held.read()
let reading = Task { try await held.read() }
try await Task.sleep(nanoseconds: 200_000_000)
reading.cancel()
do {
    _ = try await reading.value
    print("read cancel: read")
} catch {
    print("read cancel: \(error is CancellationError) still open \(!(await gone("/trickle", 2, tries: 12)))")
}
try held.close()
print("closed: gone \(await gone("/trickle", 2)) read \(try await held.read().count)")

let opening = Task { try await t.open(method: "GET", url: base + "/slowhead", headers: [:], body: Data(), timeout: 0) }
try await Task.sleep(nanoseconds: 200_000_000)
opening.cancel()
do {
    _ = try await opening.value
    print("open cancel: opened")
} catch {
    print("open cancel: \((error as? URLError)?.code == .cancelled) gone \(await gone("/slowhead", 1))")
}

do {
    let dropped = try await t.open(method: "GET", url: base + "/trickle", headers: [:], body: Data(), timeout: 0)
    _ = try dropped.head()
}
print("let go: gone \(await gone("/trickle", 3))")

let big = try await t.open(method: "GET", url: base + "/big", headers: [:], body: Data(), timeout: 5)
try await Task.sleep(nanoseconds: 500_000_000)
var count = 0
var inOrder = true
while true {
    let d = try await big.read()
    if d.isEmpty { break }
    for o in stride(from: 0, to: d.count, by: 4096) where d[d.startIndex + o] != UInt8(97 + ((count + o) >> 16) % 26) {
        inOrder = false
    }
    count += d.count
}
print("big: \(count) in order \(inOrder)")
`
	got := runHTTPProbe(t, t.TempDir(), driver)
	t.Logf("\n%s", got)
	for _, want := range []string{
		`stream 200 text/plain; charset=utf-8 "one\ntwo\nthree\n" pieces true spread true early true`,
		"after end: 0",
		"404: 404 missing",
		"echo: POST hi b",
		"stall: timedOut true soon true",
		"stall gone: true",
		"trickle: 200 a timedOut true waited true soon true",
		"trickle gone: true",
		"read cancel: true still open true",
		"closed: gone true read 0",
		"open cancel: true gone true",
		"let go: gone true",
		"big: 33554432 in order true",
	} {
		if !strings.Contains(got, want) {
			t.Errorf("want %q in:\n%s", want, got)
		}
	}
}

// runHTTPProbe compiles std/http's Swift with the generated glue's
// types (not its codec, which needs the runtime) and driver as main,
// runs it and gives what it printed.
func runHTTPProbe(t *testing.T, dir, driver string) string {
	return runSwiftProbe(t, dir, "http/swift/Http.asuka.swift", []string{"http/swift/Http.swift", "http/swift/TLS.swift"}, []string{"Response", "TLS", "Transport", "Body", "LocalServer"}, driver)
}

// runSwiftProbe compiles a std package's Swift (impls) with the types of
// its generated glue named (structs and protocols, not its codec, which
// needs the runtime), a stand-in for module Asuka (what the package's
// Swift uses of it) and driver as main, runs it and gives what it
// printed.
func runSwiftProbe(t *testing.T, dir, gluePath string, impls, types []string, driver string) string {
	t.Helper()
	glue, err := os.ReadFile(gluePath)
	if err != nil {
		t.Fatal(err)
	}
	var shim strings.Builder
	shim.WriteString("import Foundation\n")
	for _, name := range types {
		re := `(?s)public (?:struct|protocol) ` + name + `[:\s][^{]*\{.*?\n\}\n`
		m := regexp.MustCompile(re).Find(glue)
		if m == nil {
			t.Fatalf("no %s in the glue", name)
		}
		shim.Write(m)
	}
	// Module Asuka as the package's Swift sees it: its errors.
	stub := filepath.Join(dir, "stub")
	os.MkdirAll(stub, 0o755)
	asuka := filepath.Join(stub, "Asuka.swift")
	os.WriteFile(asuka, []byte("public enum AsukaNativeError: Error { case unsupported(String) }\n"), 0o644)
	if b, err := exec.Command("swiftc", "-parse-as-library", "-emit-module", "-module-name", "Asuka", "-emit-module-path", filepath.Join(stub, "Asuka.swiftmodule"), "-emit-object", "-o", filepath.Join(stub, "Asuka.o"), asuka).CombinedOutput(); err != nil {
		t.Fatalf("swiftc (Asuka): %v\n%s", err, b)
	}
	args := []string{"-swift-version", "5", "-I", stub}
	for name, src := range map[string]string{"shim.swift": shim.String(), "main.swift": driver} {
		p := filepath.Join(dir, name)
		if err := os.WriteFile(p, []byte(src), 0o644); err != nil {
			t.Fatal(err)
		}
		args = append(args, p)
	}
	for _, impl := range impls {
		abs, _ := filepath.Abs(impl)
		args = append(args, abs)
	}
	exe := filepath.Join(dir, "probe")
	args = append(args, filepath.Join(stub, "Asuka.o"), "-o", exe)
	if b, err := exec.Command("swiftc", args...).CombinedOutput(); err != nil {
		t.Fatalf("swiftc: %v\n%s", err, b)
	}
	b, err := exec.Command(exe).CombinedOutput()
	if err != nil {
		t.Fatalf("%v\n%s", err, b)
	}
	return string(b)
}

// std/http's loopback server (Network.framework): a request to another
// path is a 404, the path's answers with the page and gives next its URL;
// close stops it.
func TestHTTPSwiftLoopback(t *testing.T) {
	if runtime.GOOS != "darwin" {
		t.Skip("Network.framework: macOS")
	}
	if _, err := exec.LookPath("swiftc"); err != nil {
		t.Skip("no swiftc")
	}
	driver := `import Foundation
let s = try makeLocalServer()
let port = try await s.listen(path: "/callback", page: "<p>Done</p>")
let base = "http://127.0.0.1:\(port)"
let waiting = Task { try await s.next(port: port) }
let (_, other) = try await URLSession.shared.data(from: URL(string: base + "/favicon.ico")!)
let (page, ok) = try await URLSession.shared.data(from: URL(string: base + "/callback?code=42&state=s")!)
let url = try await waiting.value
print("other \((other as! HTTPURLResponse).statusCode) ok \((ok as! HTTPURLResponse).statusCode) page \(String(decoding: page, as: UTF8.self)) url \(url == base + "/callback?code=42&state=s")")
let later = Task { try await s.next(port: port) }
try await Task.sleep(nanoseconds: 100_000_000)
later.cancel()
do { _ = try await later.value; print("cancel: answered") } catch { print("cancel: \(error is CancellationError)") }
try s.close(port: port)
try await Task.sleep(nanoseconds: 100_000_000)
do { _ = try await URLSession.shared.data(from: URL(string: base + "/callback")!); print("closed: answered") } catch { print("closed: refused") }
`
	got := runHTTPProbe(t, t.TempDir(), driver)
	for _, want := range []string{"other 404 ok 200 page <p>Done</p> url true", "cancel: true", "closed: refused"} {
		if !strings.Contains(got, want) {
			t.Errorf("want %q in:\n%s", want, got)
		}
	}
}

// std/http's TLS settings in Swift (TLS.swift) on this Mac, against
// internal/testserver over HTTPS with a test authority's certificate:
// the system's trust refusing it, the authority trusted alone (another
// refused, why said), with the system's, no check at all; a client
// certificate shown when asked for (EC as PKCS#8 and SEC 1, RSA as
// PKCS#1), imported in memory and, as before macOS 15, into a keychain
// of the process's own; the same settings the same transport; a stream
// and a download secured; PEM that can't be read.
func TestHTTPSwiftTLS(t *testing.T) {
	if runtime.GOOS != "darwin" {
		t.Skip("URLSession: macOS")
	}
	if _, err := exec.LookPath("swiftc"); err != nil {
		t.Skip("no swiftc")
	}
	pki, err := testserver.NewPKI()
	if err != nil {
		t.Fatal(err)
	}
	dir := t.TempDir()
	if err := pki.WriteFiles(dir); err != nil {
		t.Fatal(err)
	}
	srv := testserver.StartTLS(testserver.Handler(), pki)
	defer srv.Close()
	driver := `import Foundation
let base = "` + srv.URL + `"
func read(_ n: String) -> String { try! String(contentsOfFile: "` + dir + `/" + n, encoding: .utf8) }
func said(_ e: Error) -> String { "error \((e as NSError).code) \(e.localizedDescription)" }
func get(_ t: any Transport, _ path: String = "/mtls") async -> String {
    do {
        let r = try await t.send(method: "GET", url: base + path, headers: [:], body: Data(), timeout: 10)
        return "\(r.status) \(String(decoding: r.body, as: UTF8.self).trimmingCharacters(in: .whitespacesAndNewlines))"
    } catch {
        return said(error)
    }
}
let plain = try makeTransport()
let ca = read("ca.pem")
print("system: \(await get(plain))")
let trusted = try await plain.secured(tls: TLS(ca: ca))
print("ca: \(await get(trusted))")
print("other: \(await get(try await plain.secured(tls: TLS(ca: read("other-ca.pem")))))")
print("both: \(await get(try await plain.secured(tls: TLS(ca: ca, trustSystem: true))))")
print("insecure: \(await get(try await plain.secured(tls: TLS(insecureSkipVerify: true))))")
print("ec: \(await get(try await plain.secured(tls: TLS(ca: ca, certificate: read("client.pem"), key: read("client.key")))))")
print("sec1: \(await get(try await plain.secured(tls: TLS(ca: ca, certificate: read("client.pem"), key: read("client-sec1.key")))))")
print("rsa: \(await get(try await plain.secured(tls: TLS(ca: ca, certificate: read("rsa-client.pem"), key: read("rsa-client.key")))))")
TLSPolicy.keychainImport = true
print("keychain: \(await get(try await plain.secured(tls: TLS(ca: ca, certificate: read("client.pem"), key: read("client.key") + "\n"))))")
TLSPolicy.keychainImport = false
print("same: \((try await plain.secured(tls: TLS(ca: ca)) as AnyObject) === (trusted as AnyObject))")
let b = try await trusted.open(method: "GET", url: base + "/stream", headers: [:], body: Data(), timeout: 10)
var all = Data()
while true {
    let d = try await b.read()
    if d.isEmpty { break }
    all.append(d)
}
print("stream: \(try b.head().status) \(String(decoding: all, as: UTF8.self).split(separator: "\n").joined(separator: " "))")
do {
    _ = try await plain.secured(tls: TLS(ca: read("other-ca.pem"))).open(method: "GET", url: base + "/stream", headers: [:], body: Data(), timeout: 10)
    print("stream other: opened")
} catch {
    print("stream other: \(said(error))")
}
let file = "` + dir + `/echo.json"
let d = try await trusted.download(method: "GET", url: base + "/echo", headers: [:], body: Data(), timeout: 10, path: file, progress: { _, _ in })
print("download: \(d.status) \(FileManager.default.fileExists(atPath: file))")
do {
    _ = try await plain.secured(tls: TLS(insecureSkipVerify: true)).download(method: "GET", url: "https://127.0.0.1:1/x", headers: [:], body: Data(), timeout: 10, path: file + ".2", progress: { _, _ in })
} catch {
    print("download refused: \((error as NSError).code)")
}
do {
    _ = try await plain.secured(tls: TLS(ca: "-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n"))
    print("bad ca: made")
} catch {
    print("bad ca: \(error.localizedDescription)")
}
do {
    _ = try await plain.secured(tls: TLS(certificate: read("client.pem"), key: read("rsa-client.key")))
    print("mismatch: made")
} catch {
    print("mismatch: \(error.localizedDescription)")
}
// After the rest let the server in: the system's trust and another
// authority's still refuse it (no session of theirs resumed unchecked).
print("again: \(await get(plain)) | \(await get(try await plain.secured(tls: TLS(ca: read("other-ca.pem")))))")
`
	got := runHTTPProbe(t, t.TempDir(), driver)
	for _, want := range []string{
		"system: error -1202 ",
		"ca: 403 no client certificate",
		"other: error -1202 127.0.0.1's certificate isn't trusted: ",
		"both: 403 no client certificate",
		"insecure: 403 no client certificate",
		"ec: 200 client: Asuka Test Client",
		"sec1: 200 client: Asuka Test Client",
		"rsa: 200 client: Asuka Test RSA Client",
		"keychain: 200 client: Asuka Test Client",
		"same: true",
		"stream: 200 one two three",
		"stream other: error -1202 127.0.0.1's certificate isn't trusted: ",
		"download: 200 true",
		"download refused: -1004",
		"bad ca: tls: ca: a certificate can't be read",
		"mismatch: tls: the key can't be used with the certificate (it isn't its key, or can't be read): ",
		"again: error -1202 The certificate for this server is invalid.",
		" | error -1202 127.0.0.1's certificate isn't trusted: ",
	} {
		if !strings.Contains(got, want) {
			t.Errorf("want %q in:\n%s", want, got)
		}
	}
}
