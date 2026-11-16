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
	"testing"
	"time"
)

// asuka/http's Swift (std/http/swift/Http.swift) on this Mac: its
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

// runHTTPProbe compiles asuka/http's Swift with the generated glue's
// types (not its codec, which needs the runtime) and driver as main,
// runs it and gives what it printed.
func runHTTPProbe(t *testing.T, dir, driver string) string {
	glue, err := os.ReadFile("http/swift/Http.asuka.swift")
	if err != nil {
		t.Fatal(err)
	}
	var shim strings.Builder
	shim.WriteString("import Foundation\n")
	for _, re := range []string{`(?s)public struct Response: Sendable \{.*?\n\}\n`, `(?s)public protocol Transport: AnyObject \{.*?\n\}\n`, `(?s)public protocol LocalServer: AnyObject \{.*?\n\}\n`} {
		m := regexp.MustCompile(re).Find(glue)
		if m == nil {
			t.Fatalf("no %s in the glue", re)
		}
		shim.Write(m)
	}
	var args []string
	for name, src := range map[string]string{"shim.swift": shim.String(), "main.swift": driver} {
		p := filepath.Join(dir, name)
		if err := os.WriteFile(p, []byte(src), 0o644); err != nil {
			t.Fatal(err)
		}
		args = append(args, p)
	}
	abs, _ := filepath.Abs("http/swift/Http.swift")
	exe := filepath.Join(dir, "probe")
	args = append(args, abs, "-o", exe)
	if b, err := exec.Command("swiftc", args...).CombinedOutput(); err != nil {
		t.Fatalf("swiftc: %v\n%s", err, b)
	}
	b, err := exec.Command(exe).CombinedOutput()
	if err != nil {
		t.Fatalf("%v\n%s", err, b)
	}
	return string(b)
}

// asuka/http's loopback server (Network.framework): a request to another
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
