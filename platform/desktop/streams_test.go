package desktop_test

import (
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/egoist/asuka/internal/testserver"
)

// streams serves internal/testserver for TestStdLinux's std/http stream
// and std/websocket cases (testdata/streams), counting the /stall
// requests still being served: a body that timed out, or whose read was
// cancelled, has its request stopped.
type streams struct {
	srv    *httptest.Server
	stalls atomic.Int32
}

func newStreams(t *testing.T) *streams {
	s := &streams{}
	h := testserver.Handler()
	s.srv = httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/stall" {
			s.stalls.Add(1)
			defer s.stalls.Add(-1)
		}
		h.ServeHTTP(w, r)
	}))
	t.Cleanup(s.srv.Close)
	return s
}

// source is testdata/streams/<name>, its server's URLs filled in.
func (s *streams) source(t *testing.T, name string) string {
	b, err := os.ReadFile(filepath.Join("testdata", "streams", name))
	if err != nil {
		t.Fatal(err)
	}
	return strings.NewReplacer(`"BASE"`, `"`+s.srv.URL+`"`, `"WSBASE"`, `"ws`+strings.TrimPrefix(s.srv.URL, "http")+`"`, `"DONE_FILE"`, `""`).Replace(string(b))
}

// stopped checks, once the app says it cancelled a read of /stall, that
// the requests to /stall stop (the server holds each 30 s otherwise).
func (s *streams) stopped(t *testing.T, pid int, line string) {
	if !strings.HasPrefix(line, "host: cancelled") {
		return
	}
	for end := time.Now().Add(10 * time.Second); s.stalls.Load() > 0; {
		if time.Now().After(end) {
			t.Errorf("%d requests to /stall still served after their bodies timed out or were cancelled", s.stalls.Load())
			return
		}
		time.Sleep(20 * time.Millisecond)
	}
}

// What testdata/streams' apps show, on every desktop.
var (
	streamsHTTP = []string{
		"stream 200 text/plain; charset=utf-8 first one| all one|two|three|",
		"lines <one><two><three>",
		"events message:first: price:1+2:2 message:last:2",
		"posted 200 true true true",
		"long true false",
		"missing 404 false missing",
		"stall timed out true",
		"cancelled 200",
		"again one|two|three|",
		"done",
	}
	streamsWebSocket = []string{
		"echo chat: header:t1 hello binary bin closed 4000 bye",
		"normal [] ended",
		"client still ended, then refused",
		"refused yes",
		"done",
	}
)

func hostLines(lines []string) []string {
	out := make([]string, len(lines))
	for i, l := range lines {
		out[i] = "host: " + l
	}
	return out
}
