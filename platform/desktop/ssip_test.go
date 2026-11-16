package desktop_test

import (
	"bufio"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"
)

// ssipServer is a Speech Dispatcher of a test's: SSIP on a Unix socket
// (SPEECHD_ADDRESS), whose voices are kal (en-us) and anna (de), which
// speaks a message in 50 ms (BEGIN, then END) except "long", held until
// CANCEL; it logs what it hears, a line each.
type ssipServer struct {
	mu    sync.Mutex
	heard strings.Builder
}

func (s *ssipServer) log() string {
	s.mu.Lock()
	defer s.mu.Unlock()
	return "\n" + s.heard.String()
}

func fakeSSIP(t *testing.T) *ssipServer {
	dir, err := os.MkdirTemp("", "ssip")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { os.RemoveAll(dir) })
	path := filepath.Join(dir, "speechd.sock")
	l, err := net.Listen("unix", path)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { l.Close() })
	t.Setenv("SPEECHD_ADDRESS", "unix_socket:"+path)
	s := &ssipServer{}
	go func() {
		for {
			c, err := l.Accept()
			if err != nil {
				return
			}
			go s.serve(c)
		}
	}()
	return s
}

func (s *ssipServer) serve(c net.Conn) {
	defer c.Close()
	var wmu sync.Mutex
	send := func(lines ...string) {
		wmu.Lock()
		defer wmu.Unlock()
		c.Write([]byte(strings.Join(lines, "\r\n") + "\r\n"))
	}
	notify := func(code int, id int, what string) {
		send(fmt.Sprintf("%d-%d", code, id), fmt.Sprintf("%d-1", code), fmt.Sprintf("%d %s", code, what))
	}
	r := bufio.NewReader(c)
	var data []string
	receiving := false
	next := 0
	var held []int
	for {
		line, err := r.ReadString('\n')
		if err != nil {
			return
		}
		line = strings.TrimRight(line, "\r\n")
		s.mu.Lock()
		s.heard.WriteString(line + "\n")
		s.mu.Unlock()
		if receiving {
			if line != "." {
				data = append(data, line)
				continue
			}
			receiving = false
			next++
			id := next
			send(fmt.Sprintf("225-%d", id), "225 OK MESSAGE QUEUED")
			if len(data) == 1 && data[0] == "long" {
				held = append(held, id)
			} else {
				go func() {
					time.Sleep(50 * time.Millisecond)
					notify(701, id, "BEGIN")
					notify(702, id, "END")
				}()
			}
			data = nil
			continue
		}
		switch {
		case strings.HasPrefix(line, "SET "):
			send("208 OK SET")
		case line == "SPEAK":
			receiving = true
			send("230 OK RECEIVING DATA")
		case line == "LIST SYNTHESIS_VOICES":
			send("249-kal\ten-us\tnone", "249-anna\tde\tnone", "249 OK VOICE LIST SENT")
		case line == "CANCEL self":
			send("210 OK CANCELED")
			for _, id := range held {
				notify(703, id, "CANCELED")
			}
			held = nil
		case line == "PAUSE self", line == "RESUME self":
			send("210 OK")
		default:
			send("300 ERR UNKNOWN COMMAND")
		}
	}
}
