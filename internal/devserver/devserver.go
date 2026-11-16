// Package devserver is asuka dev's side of a player's connection
// (core/src/abi/dev.h): players connect, get the app's latest bytecode
// at once and again on every build, and send back their logs and how
// each reload went.
package devserver

import (
	"encoding/binary"
	"errors"
	"io"
	"net"
	"sync"
)

// Frame kinds (core/src/abi/dev.h).
const (
	KindModule   = 1
	KindPing     = 2
	KindLog      = 3
	KindHello    = 4
	KindReloaded = 5
	KindError    = 6
)

// Events are what players report.
type Events struct {
	Hello    func(p *Player, json []byte)
	Log      func(p *Player, stderr bool, text []byte)
	Reloaded func(p *Player, kept uint32, micros uint32)
	Error    func(p *Player, text []byte)
	Gone     func(p *Player, err error)
}

// Server serves players.
type Server struct {
	ln     net.Listener
	ev     Events
	mu     sync.Mutex
	module []byte
	ps     map[*Player]bool
}

// Player is a connected player.
type Player struct {
	conn net.Conn
	wmu  sync.Mutex
	Info []byte // its hello
}

// Listen serves on addr (":0" picks a port).
func Listen(addr string, ev Events) (*Server, error) {
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		return nil, err
	}
	s := &Server{ln: ln, ev: ev, ps: map[*Player]bool{}}
	go s.accept()
	return s, nil
}

// Port is the port players connect to.
func (s *Server) Port() int { return s.ln.Addr().(*net.TCPAddr).Port }

// Close stops serving and disconnects every player.
func (s *Server) Close() error {
	err := s.ln.Close()
	s.mu.Lock()
	for p := range s.ps {
		p.conn.Close()
	}
	s.mu.Unlock()
	return err
}

// Players is how many are connected.
func (s *Server) Players() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return len(s.ps)
}

// Send sends a build's bytecode to every player, and to every player
// that connects from now on.
func (s *Server) Send(module []byte) {
	s.mu.Lock()
	s.module = module
	ps := make([]*Player, 0, len(s.ps))
	for p := range s.ps {
		ps = append(ps, p)
	}
	s.mu.Unlock()
	for _, p := range ps {
		p.send(KindModule, module)
	}
}

func (s *Server) accept() {
	for {
		c, err := s.ln.Accept()
		if err != nil {
			return
		}
		go s.Serve(c)
	}
}

// Serve serves a player on c, a connection made either way: one it
// made to the server, or one made to it (an iPhone's, through the cable:
// usbmuxd reaches a port on the phone, the phone can't reach the Mac).
// It returns when the player goes.
func (s *Server) Serve(c net.Conn) {
	if tc, ok := c.(*net.TCPConn); ok {
		tc.SetNoDelay(true)
	}
	p := &Player{conn: c}
	s.mu.Lock()
	s.ps[p] = true
	m := s.module
	s.mu.Unlock()
	s.serve(p, m)
}

func (s *Server) serve(p *Player, module []byte) {
	if module != nil {
		p.send(KindModule, module)
	}
	err := p.read(s.ev)
	s.mu.Lock()
	delete(s.ps, p)
	s.mu.Unlock()
	p.conn.Close()
	if s.ev.Gone != nil {
		s.ev.Gone(p, err)
	}
}

func (p *Player) send(kind byte, b []byte) error {
	h := make([]byte, 5, 5+len(b))
	binary.LittleEndian.PutUint32(h, uint32(len(b)+1))
	h[4] = kind
	p.wmu.Lock()
	defer p.wmu.Unlock()
	_, err := p.conn.Write(append(h, b...))
	return err
}

func (p *Player) read(ev Events) error {
	var h [5]byte
	for {
		if _, err := io.ReadFull(p.conn, h[:]); err != nil {
			if errors.Is(err, io.EOF) {
				return nil
			}
			return err
		}
		n := binary.LittleEndian.Uint32(h[:4])
		if n == 0 || n > 64<<20 {
			return errors.New("devserver: a bad frame")
		}
		b := make([]byte, n-1)
		if _, err := io.ReadFull(p.conn, b); err != nil {
			return err
		}
		switch h[4] {
		case KindHello:
			p.Info = b
			if ev.Hello != nil {
				ev.Hello(p, b)
			}
		case KindLog:
			if ev.Log != nil && len(b) > 0 {
				ev.Log(p, b[0] == 2, b[1:])
			}
		case KindReloaded:
			if ev.Reloaded != nil && len(b) >= 8 {
				ev.Reloaded(p, binary.LittleEndian.Uint32(b), binary.LittleEndian.Uint32(b[4:]))
			}
		case KindError:
			if ev.Error != nil {
				ev.Error(p, b)
			}
		}
	}
}
