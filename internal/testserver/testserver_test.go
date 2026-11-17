package testserver

import (
	"bufio"
	"encoding/binary"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

// writeClientFrame writes a masked frame, as a client must.
func writeClientFrame(w io.Writer, op byte, payload []byte) {
	mask := [4]byte{1, 2, 3, 4}
	h := []byte{0x80 | op}
	if len(payload) < 126 {
		h = append(h, 0x80|byte(len(payload)))
	} else {
		h = append(h, 0x80|126, byte(len(payload)>>8), byte(len(payload)))
	}
	h = append(h, mask[:]...)
	b := append([]byte{}, payload...)
	for i := range b {
		b[i] ^= mask[i%4]
	}
	w.Write(append(h, b...))
}

func readServerFrame(t *testing.T, r *bufio.Reader) (byte, []byte) {
	t.Helper()
	var h [2]byte
	if _, err := io.ReadFull(r, h[:]); err != nil {
		t.Fatal(err)
	}
	n := int(h[1] & 0x7f)
	if n == 126 {
		var b [2]byte
		io.ReadFull(r, b[:])
		n = int(binary.BigEndian.Uint16(b[:]))
	}
	p := make([]byte, n)
	io.ReadFull(r, p)
	return h[0] & 0x0f, p
}

// The WebSocket echo answers as a client of any platform expects.
func TestWebSocketEcho(t *testing.T) {
	s := httptest.NewServer(Handler())
	defer s.Close()
	c, err := net.Dial("tcp", strings.TrimPrefix(s.URL, "http://"))
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	io.WriteString(c, "GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: other, chat\r\nX-Token: t1\r\n\r\n")
	r := bufio.NewReader(c)
	resp, err := http.ReadResponse(r, nil)
	if err != nil {
		t.Fatal(err)
	}
	if resp.StatusCode != 101 || resp.Header.Get("Sec-WebSocket-Accept") != "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" || resp.Header.Get("Sec-WebSocket-Protocol") != "chat" {
		t.Fatalf("handshake: %d %v", resp.StatusCode, resp.Header)
	}
	if op, p := readServerFrame(t, r); op != opText || string(p) != "header:t1" {
		t.Fatalf("first: %d %q", op, p)
	}
	writeClientFrame(c, opText, []byte("hi"))
	if op, p := readServerFrame(t, r); op != opText || string(p) != "hi" {
		t.Fatalf("echo: %d %q", op, p)
	}
	big := []byte(strings.Repeat("b", 300))
	writeClientFrame(c, opBinary, big)
	if op, p := readServerFrame(t, r); op != opBinary || string(p) != string(big) {
		t.Fatalf("binary echo: %d %d bytes", op, len(p))
	}
	writeClientFrame(c, opPing, []byte("p"))
	if op, p := readServerFrame(t, r); op != opPong || string(p) != "p" {
		t.Fatalf("pong: %d %q", op, p)
	}
	writeClientFrame(c, opText, []byte("close please"))
	op, p := readServerFrame(t, r)
	if op != opClose || binary.BigEndian.Uint16(p) != 4000 || string(p[2:]) != "bye" {
		t.Fatalf("close: %d %v", op, p)
	}
}
