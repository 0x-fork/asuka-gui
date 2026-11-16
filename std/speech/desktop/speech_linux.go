//go:build linux && !android

package speech

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"math"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/egoist/asuka/native"
)

// Linux: Speech Dispatcher (the desktop's speech service, which Orca
// speaks through), over its socket in SSIP, its text protocol: no library
// to load, and the daemon is started as libspeechd starts it when it
// isn't running. The connection's reads and writes are goroutines';
// what they hear comes back to the main thread (native.Post), where the
// state is.

// ssipConn is a connection to Speech Dispatcher.
type ssipConn struct {
	conn    net.Conn
	r       *bufio.Reader
	mu      sync.Mutex      // one command at a time
	replies chan ssipReply  // command replies, in order
	events  func(ssipReply) // 7xx notifications (BEGIN, END...), from the reader
	closed  chan struct{}
}

// ssipReply is a reply: its code and its lines (the last's text after
// the code).
type ssipReply struct {
	code  int
	lines []string
}

func (r ssipReply) ok() bool { return r.code >= 200 && r.code < 300 }

func (r ssipReply) err() error {
	if r.ok() {
		return nil
	}
	msg := ""
	if len(r.lines) > 0 {
		msg = r.lines[len(r.lines)-1]
	}
	return fmt.Errorf("speech-dispatcher: %d %s", r.code, msg)
}

// ssipSocket is where the daemon listens.
func ssipSocket() string {
	if a := os.Getenv("SPEECHD_ADDRESS"); strings.HasPrefix(a, "unix_socket:") {
		return strings.TrimPrefix(a, "unix_socket:")
	}
	dir := os.Getenv("XDG_RUNTIME_DIR")
	if dir == "" {
		dir = filepath.Join(os.TempDir(), fmt.Sprintf("speech-dispatcher-%d", os.Getuid()))
	}
	return filepath.Join(dir, "speech-dispatcher", "speechd.sock")
}

// dialSSIP connects, starting the daemon if it isn't running. Not on the
// main thread.
func dialSSIP(events func(ssipReply)) (*ssipConn, error) {
	path := ssipSocket()
	c, err := net.Dial("unix", path)
	if err != nil {
		exe, lerr := exec.LookPath("speech-dispatcher")
		if lerr != nil {
			return nil, errors.New("speech: speech-dispatcher isn't installed")
		}
		// As libspeechd autospawns it: it forks, and exits once it listens.
		cmd := exec.Command(exe, "--spawn", "--communication-method", "unix_socket", "--socket-path", path)
		cmd.Run()
		for i := 0; i < 50; i++ {
			if c, err = net.Dial("unix", path); err == nil {
				break
			}
			time.Sleep(100 * time.Millisecond)
		}
		if err != nil {
			return nil, fmt.Errorf("speech: speech-dispatcher doesn't start: %v", err)
		}
	}
	s := &ssipConn{conn: c, r: bufio.NewReader(c), replies: make(chan ssipReply, 4), events: events, closed: make(chan struct{})}
	go s.read()
	user := os.Getenv("USER")
	if user == "" {
		user = "user"
	}
	app := strings.ReplaceAll(filepath.Base(os.Args[0]), ":", "_")
	for _, cmd := range []string{
		"SET self CLIENT_NAME " + user + ":" + app + ":main",
		"SET self NOTIFICATION ALL on",
	} {
		if r, err := s.command(cmd); err != nil || !r.ok() {
			c.Close()
			if err == nil {
				err = r.err()
			}
			return nil, err
		}
	}
	return s, nil
}

// read reads replies: notifications go to events, the rest to replies.
func (s *ssipConn) read() {
	defer close(s.closed)
	var lines []string
	for {
		line, err := s.r.ReadString('\n')
		if err != nil {
			return
		}
		line = strings.TrimRight(line, "\r\n")
		if len(line) < 4 {
			continue
		}
		code, _ := strconv.Atoi(line[:3])
		lines = append(lines, line[4:])
		if line[3] == '-' {
			continue
		}
		r := ssipReply{code: code, lines: lines}
		lines = nil
		if code >= 700 && code < 800 {
			s.events(r)
			continue
		}
		s.replies <- r
	}
}

// command sends a command and waits for its reply. Not on the main
// thread.
func (s *ssipConn) command(cmd string) (ssipReply, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.commandLocked(cmd)
}

func (s *ssipConn) commandLocked(cmd string) (ssipReply, error) {
	if _, err := s.conn.Write([]byte(cmd + "\r\n")); err != nil {
		return ssipReply{}, err
	}
	select {
	case r := <-s.replies:
		return r, nil
	case <-s.closed:
		return ssipReply{}, errors.New("speech-dispatcher: the connection closed")
	}
}

// speak sends SPEAK and the text; the reply has the message's ID.
func (s *ssipConn) speak(settings []string, text string) (int, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, c := range settings {
		if r, err := s.commandLocked(c); err != nil {
			return 0, err
		} else if !r.ok() && !strings.Contains(c, "SYNTHESIS_VOICE") && !strings.Contains(c, "LANGUAGE") {
			return 0, r.err()
		}
	}
	r, err := s.commandLocked("SPEAK")
	if err != nil {
		return 0, err
	}
	if r.code != 230 {
		return 0, r.err()
	}
	// The text: lines, a lone "." escaped as "..", then ".".
	var b strings.Builder
	for _, l := range strings.Split(strings.ReplaceAll(text, "\r\n", "\n"), "\n") {
		if strings.HasPrefix(l, ".") {
			b.WriteString(".")
		}
		b.WriteString(l)
		b.WriteString("\r\n")
	}
	b.WriteString(".")
	r, err = s.commandLocked(b.String())
	if err != nil {
		return 0, err
	}
	if !r.ok() || len(r.lines) == 0 {
		return 0, r.err()
	}
	id, _ := strconv.Atoi(r.lines[0])
	return id, nil
}

// dispatcher is the synthesizer's state, on the main thread.
type dispatcher struct {
	conn    *ssipConn
	dialing []func(*ssipConn, error)
	waiting map[int]chan error // by message ID
	early   map[int]bool       // ended before its ID came back
	pending int                // speaks sent, their IDs not back yet
	paused  bool
}

var speech = &dispatcher{waiting: map[int]chan error{}, early: map[int]bool{}}

func newSynthesizer() (Synthesizer, error) { return speech, nil }

// connect calls f with the connection on the main thread, connecting the
// first time (off it).
func (s *dispatcher) connect(f func(*ssipConn, error)) {
	if s.conn != nil {
		f(s.conn, nil)
		return
	}
	s.dialing = append(s.dialing, f)
	if len(s.dialing) > 1 {
		return
	}
	go func() {
		c, err := dialSSIP(func(r ssipReply) { native.Post(func() { s.event(r) }) })
		native.Post(func() {
			if err == nil {
				s.conn = c
				go func() {
					<-c.closed
					native.Post(func() {
						if s.conn == c {
							s.conn = nil
							s.ended(errors.New("speech-dispatcher: the connection closed"))
						}
					})
				}()
			}
			fs := s.dialing
			s.dialing = nil
			for _, f := range fs {
				f(c, err)
			}
		})
	}()
}

// event is a notification: 702 END, 703 CANCELED end a message.
func (s *dispatcher) event(r ssipReply) {
	if len(r.lines) < 1 {
		return
	}
	id, _ := strconv.Atoi(r.lines[0])
	switch r.code {
	case 702, 703:
		c := s.waiting[id]
		if c == nil {
			if s.pending > 0 {
				s.early[id] = r.code == 702
			}
			return
		}
		delete(s.waiting, id)
		if r.code == 702 {
			c <- nil
		} else {
			c <- ErrStopped
		}
	case 704:
		s.paused = true
	case 705:
		s.paused = false
	}
}

// ended fails every message waiting.
func (s *dispatcher) ended(err error) {
	cs := s.waiting
	s.waiting = map[int]chan error{}
	for _, c := range cs {
		c <- err
	}
}

// ssipScale is a multiplier (0.5 to 2, 1 normal) as SSIP's -100 to 100.
func ssipScale(x float64) int {
	if x <= 0 {
		x = 1
	}
	return int(math.Round(min(max(100*math.Log2(x), -100), 100)))
}

// do runs a command on the connection, if there's one, off the main
// thread.
func (s *dispatcher) do(cmd string) {
	if c := s.conn; c != nil {
		go c.command(cmd)
	}
}

func (s *dispatcher) Speak(ctx context.Context, text string, o Options) error {
	settings := []string{
		fmt.Sprintf("SET self RATE %d", ssipScale(o.Rate)),
		fmt.Sprintf("SET self PITCH %d", ssipScale(o.Pitch)),
		fmt.Sprintf("SET self VOLUME %d", int(math.Round(min(max(o.Volume, 0), 1)*200-100))),
	}
	lang := o.Language
	if lang == "" {
		lang = userLanguage()
	}
	if lang != "" {
		settings = append(settings, "SET self LANGUAGE "+strings.ToLower(strings.ReplaceAll(lang, "_", "-")))
	}
	if o.Voice != "" {
		settings = append(settings, "SET self SYNTHESIS_VOICE "+o.Voice)
	}
	done := make(chan error, 1)
	native.Post(func() {
		s.connect(func(c *ssipConn, err error) {
			if err != nil {
				done <- err
				return
			}
			s.pending++
			go func() {
				id, err := c.speak(settings, text)
				native.Post(func() {
					s.pending--
					if err != nil {
						done <- err
						return
					}
					if ok, was := s.early[id]; was {
						delete(s.early, id)
						if ok {
							done <- nil
						} else {
							done <- ErrStopped
						}
						return
					}
					s.waiting[id] = done
					if s.pending == 0 {
						s.early = map[int]bool{}
					}
				})
			}()
		})
	})
	return waitSpoken(ctx, done, func() { native.Post(func() { s.Stop() }) })
}

func (s *dispatcher) Stop() error {
	s.ended(ErrStopped)
	s.paused = false
	s.do("CANCEL self")
	return nil
}

func (s *dispatcher) Pause() error {
	if s.conn != nil {
		s.paused = true
		s.do("PAUSE self")
	}
	return nil
}

func (s *dispatcher) Resume() error {
	if s.conn != nil {
		s.paused = false
		s.do("RESUME self")
	}
	return nil
}

func (s *dispatcher) Speaking() (bool, error) { return len(s.waiting) > 0 || s.pending > 0, nil }

func (s *dispatcher) Voices(ctx context.Context) ([]Voice, error) {
	type answer struct {
		vs  []Voice
		err error
	}
	got := make(chan answer, 1)
	native.Post(func() {
		s.connect(func(c *ssipConn, err error) {
			if err != nil {
				got <- answer{nil, err}
				return
			}
			go func() {
				r, err := c.command("LIST SYNTHESIS_VOICES")
				if err == nil {
					err = r.err()
				}
				if err != nil {
					got <- answer{nil, err}
					return
				}
				got <- answer{ssipVoices(r.lines), nil}
			}()
		})
	})
	select {
	case a := <-got:
		return a.vs, a.err
	case <-ctx.Done():
		return nil, ctx.Err()
	}
}

// userLanguage is the user's language (LANGUAGE, LC_ALL, LC_MESSAGES,
// LANG), as a BCP 47 tag.
func userLanguage() string {
	for _, k := range []string{"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"} {
		v := os.Getenv(k)
		v, _, _ = strings.Cut(v, ":")
		v, _, _ = strings.Cut(v, ".")
		if v != "" && v != "C" && v != "POSIX" {
			return strings.ReplaceAll(v, "_", "-")
		}
	}
	return ""
}

// ssipVoices is LIST SYNTHESIS_VOICES's lines (name, language, variant
// by tabs; the last line the status) as the module's voices.
func ssipVoices(lines []string) []Voice {
	vs := []Voice{}
	for i, l := range lines {
		if i == len(lines)-1 {
			break // "OK VOICE LIST SENT"
		}
		f := strings.Split(l, "\t")
		if len(f) < 2 || f[0] == "" {
			continue
		}
		lang := f[1]
		if parts := strings.SplitN(lang, "-", 2); len(parts) == 2 {
			lang = strings.ToLower(parts[0]) + "-" + strings.ToUpper(parts[1])
		}
		vs = append(vs, Voice{Id: f[0], Name: f[0], Language: lang})
	}
	return vs
}
