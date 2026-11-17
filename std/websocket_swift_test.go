package std

import (
	"crypto/sha1"
	"encoding/base64"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os/exec"
	"runtime"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/testserver"
)

// std/websocket's Swift (std/websocket/swift/Websocket.swift) on this
// Mac: URLSessionWebSocketTask against testserver's echo: text and
// binary, the app's header and protocol, the server's closes (4000 "bye",
// a normal one, one without a code), the app's, and a server hanging up
// without one (1006),
// a read cancelled (its message kept), a
// connection let go, a refused handshake, a handshake's timeout, and a
// connection that waits longer than that timeout for a message.
func TestWebSocketSwift(t *testing.T) {
	if runtime.GOOS != "darwin" {
		t.Skip("URLSession: macOS")
	}
	if _, err := exec.LookPath("swiftc"); err != nil {
		t.Skip("no swiftc")
	}
	h := testserver.Handler()
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/drop" && r.URL.Path != "/nocode" {
			h.ServeHTTP(w, r)
			return
		}
		// The handshake, then the connection dropped without a close
		// (/drop), or a close without a code (/nocode).
		sum := sha1.Sum([]byte(r.Header.Get("Sec-WebSocket-Key") + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
		conn, rw, err := http.NewResponseController(w).Hijack()
		if err != nil {
			return
		}
		fmt.Fprintf(rw, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", base64.StdEncoding.EncodeToString(sum[:]))
		if r.URL.Path == "/nocode" {
			rw.Write([]byte{0x88, 0})
			rw.Flush()
			rw.Read(make([]byte, 64)) // the client's answer
		}
		rw.Flush()
		conn.Close()
	}))
	defer srv.Close()
	l, err := testserver.Listener() // accepts, never answers: a handshake's timeout
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	go func() {
		for {
			c, err := l.Accept()
			if err != nil {
				return
			}
			defer c.Close()
		}
	}()
	ws := "ws" + strings.TrimPrefix(srv.URL, "http") + "/ws"
	driver := `import Foundation
let c = try makeClient()
let url = "` + ws + `"
func show(_ f: Frame) -> String {
    switch f.kind {
    case .text: return "text \(f.text)"
    case .binary: return "binary \(Array(f.data))"
    default: return "closed \(f.code) \(f.text.debugDescription)"
    }
}

let s = try await c.connect(url: url, headers: ["X-Token": "secret"], protocols: ["chat", "other"], timeout: 5)
print("protocol: \(try s.protocol())")
print("first: \(show(try await s.receive()))")
try await s.sendText(text: "hello")
print("echo: \(show(try await s.receive()))")
try await s.sendBinary(data: Data([1, 2, 255]))
print("echo: \(show(try await s.receive()))")
let big = String(repeating: "x", count: 3 << 20)
try await s.sendText(text: big)
print("big: \(try await s.receive().text.count)")

// A read cancelled: the message that comes after is the next read's.
let reading = Task { try await s.receive() }
try await Task.sleep(nanoseconds: 100_000_000)
reading.cancel()
do { _ = try await reading.value; print("cancel: read") } catch { print("cancel: \(error is CancellationError)") }
try await s.sendText(text: "kept")
print("after cancel: \(show(try await s.receive()))")

try await s.sendText(text: "close please")
print("server close: \(show(try await s.receive()))")
print("again: \(show(try await s.receive()))")

let n = try await c.connect(url: url, headers: [:], protocols: [], timeout: 5)
print("no protocol: \(try n.protocol().debugDescription)")
try await n.sendText(text: "close normally")
print("normal: \(show(try await n.receive()))")

let a = try await c.connect(url: url, headers: [:], protocols: [], timeout: 1)
// Idle longer than the handshake's timeout: still open.
try await Task.sleep(nanoseconds: 1_500_000_000)
try await a.sendText(text: "still")
print("idle: \(show(try await a.receive()))")
try a.shutdown(code: 4001, reason: "done here")
print("shutdown: \(show(try await a.receive()))")
print("again: \(show(try await a.receive()))")

let w = try await c.connect(url: url, headers: [:], protocols: [], timeout: 5)
let waiting = Task { try await w.receive() }
try await Task.sleep(nanoseconds: 100_000_000)
try w.close()
print("let go: \(show(try await waiting.value))")

let d = try await c.connect(url: url.replacingOccurrences(of: "/ws", with: "/drop"), headers: [:], protocols: [], timeout: 5)
do { print("dropped: \(show(try await d.receive()))") } catch { print("dropped: threw \(error)") }
do { print("again: \(show(try await d.receive()))") } catch { print("again: threw \(error)") }
let nc = try await c.connect(url: url.replacingOccurrences(of: "/ws", with: "/nocode"), headers: [:], protocols: [], timeout: 5)
print("no code: \(show(try await nc.receive()))")

do {
    _ = try await c.connect(url: url.replacingOccurrences(of: "/ws", with: "/status/404"), headers: [:], protocols: [], timeout: 5)
    print("refused: connected")
} catch {
    print("refused: \(error.localizedDescription)")
}

let t0 = Date()
do {
    _ = try await c.connect(url: "ws://` + l.Addr().String() + `/", headers: [:], protocols: [], timeout: 0.5)
    print("timeout: connected")
} catch {
    print("timeout: \((error as? URLError)?.code == .timedOut) soon \(Date().timeIntervalSince(t0) < 3)")
}

let connecting = Task { try await c.connect(url: "ws://` + l.Addr().String() + `/", headers: [:], protocols: [], timeout: 0) }
try await Task.sleep(nanoseconds: 200_000_000)
connecting.cancel()
do { _ = try await connecting.value; print("connect cancel: connected") } catch { print("connect cancel: \((error as? URLError)?.code == .cancelled)") }
`
	got := runSwiftProbe(t, t.TempDir(), "websocket/swift/Websocket.asuka.swift", "websocket/swift/Websocket.swift", []string{"FrameKind", "Frame", "Client", "Socket"}, driver)
	t.Logf("\n%s", got)
	for _, want := range []string{
		"protocol: chat",
		"first: text header:secret",
		"echo: text hello",
		"echo: binary [1, 2, 255]",
		"big: 3145728",
		"cancel: true",
		"after cancel: text kept",
		`server close: closed 4000 "bye"`,
		`again: closed 4000 "bye"`,
		`no protocol: ""`,
		`normal: closed 1000 ""`,
		"idle: text still",
		`shutdown: closed 4001 "done here"`,
		`again: closed 4001 "done here"`,
		`let go: closed 1000 ""`,
		`dropped: closed 1006 ""`,
		`again: closed 1006 ""`,
		`no code: closed 1005 ""`,
		"refused: the server answered 404",
		"timeout: true soon true",
		"connect cancel: true",
	} {
		if !strings.Contains(got, want) {
			t.Errorf("want %q in:\n%s", want, got)
		}
	}
}
