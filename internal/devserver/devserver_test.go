package devserver

import (
	"encoding/binary"
	"io"
	"net"
	"strconv"
	"testing"
)

func frame(kind byte, b []byte) []byte {
	h := make([]byte, 5)
	binary.LittleEndian.PutUint32(h, uint32(len(b)+1))
	h[4] = kind
	return append(h, b...)
}

func TestPlayer(t *testing.T) {
	hello := make(chan string, 1)
	logs := make(chan string, 4)
	reloaded := make(chan uint32, 1)
	s, err := Listen("127.0.0.1:0", Events{
		Hello:    func(p *Player, b []byte) { hello <- string(b) },
		Log:      func(p *Player, stderr bool, b []byte) { logs <- strconv.FormatBool(stderr) + ":" + string(b) },
		Reloaded: func(p *Player, kept, us uint32) { reloaded <- kept },
	})
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	s.Send([]byte("v1"))
	c, err := net.Dial("tcp", "127.0.0.1:"+strconv.Itoa(s.Port()))
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	read := func() (byte, string) {
		var h [5]byte
		io.ReadFull(c, h[:])
		b := make([]byte, binary.LittleEndian.Uint32(h[:4])-1)
		io.ReadFull(c, b)
		return h[4], string(b)
	}
	// The latest build, at once.
	if k, b := read(); k != KindModule || b != "v1" {
		t.Fatalf("%d %q", k, b)
	}
	c.Write(frame(KindHello, []byte(`{"platform":"ios"}`)))
	if h := <-hello; h != `{"platform":"ios"}` {
		t.Fatal(h)
	}
	s.Send([]byte("v2"))
	if k, b := read(); k != KindModule || b != "v2" {
		t.Fatalf("%d %q", k, b)
	}
	c.Write(frame(KindLog, append([]byte{2}, "boom\n"...)))
	if l := <-logs; l != "true:boom\n" {
		t.Fatal(l)
	}
	c.Write(frame(KindReloaded, []byte{3, 0, 0, 0, 9, 0, 0, 0}))
	if k := <-reloaded; k != 3 {
		t.Fatal(k)
	}
}
