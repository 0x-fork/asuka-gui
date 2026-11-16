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
	glue, err := os.ReadFile("http/swift/Http.asuka.swift")
	if err != nil {
		t.Fatal(err)
	}
	// The glue's types, without the codec (which needs the runtime).
	var shim strings.Builder
	shim.WriteString("import Foundation\n")
	for _, re := range []string{`(?s)public struct Response: Sendable \{.*?\n\}\n`, `(?s)public protocol Transport: AnyObject \{.*?\n\}\n`} {
		m := regexp.MustCompile(re).Find(glue)
		if m == nil {
			t.Fatalf("no %s in the glue", re)
		}
		shim.Write(m)
	}
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
	files := map[string]string{"shim.swift": shim.String(), "main.swift": driver}
	args := []string{"-parse-as-library", "-o", filepath.Join(dir, "probe")}
	for name, src := range files {
		p := filepath.Join(dir, name)
		if err := os.WriteFile(p, []byte(src), 0o644); err != nil {
			t.Fatal(err)
		}
		args = append(args, p)
	}
	abs, _ := filepath.Abs("http/swift/Http.swift")
	args = append(args, abs)
	// main.swift's top-level code: not -parse-as-library.
	args = args[1:]
	if b, err := exec.Command("swiftc", args...).CombinedOutput(); err != nil {
		t.Fatalf("swiftc: %v\n%s", err, b)
	}
	b, err := exec.Command(filepath.Join(dir, "probe")).CombinedOutput()
	if err != nil {
		t.Fatalf("%v\n%s", err, b)
	}
	got := string(b)
	for _, want := range []string{"status 200 body 0 size 1048576 reports true last 1048576 1048576 totals true", "cancel: true false"} {
		if !strings.Contains(got, want) {
			t.Errorf("want %q in:\n%s", want, got)
		}
	}
}
