package native

import (
	"context"
	"errors"
	"fmt"
	"sync"
)

// Native packages implemented in Go, behind the C core. A package's
// generated code (asuka native generate, desktop/<name>_native.go)
// registers a Server, and Serve answers the core's native calls with it.
// No host runs Go any more (RFC 0008): what's left here goes as the
// standard library's desktop Go is ported to C (G1).

// Server is a package's Go implementation.
type Server struct {
	// Class is the package's name, what the core makes objects of.
	Class string
	// Errors are its declared errors, by index.
	Errors []error
	// New makes an object of the package's interface iface.
	New func(iface uint32) (any, error)
	// Call runs method on obj, reading its arguments from r and writing
	// its result to w. ctx is cancelled when an async call is.
	Call func(ctx context.Context, s *Session, obj any, method uint32, r *Reader, w *Writer) error
}

var (
	serversMu sync.Mutex
	servers   = map[string]*Server{}
)

// RegisterServer makes a package's Go implementation available to the
// core (generated code calls it in init).
func RegisterServer(s *Server) {
	serversMu.Lock()
	servers[s.Class] = s
	serversMu.Unlock()
}

// Session is the objects the core holds, by handle, and its way back
// (callbacks). The host's bridge makes one.
type Session struct {
	mu      sync.Mutex
	objects map[uint64]object
	next    uint64
	calls   map[uint64]context.CancelFunc
	// Invoke calls the core's callback id with its encoded arguments and
	// returns its encoded result (asuka_native_invoke).
	Invoke func(id uint64, args []byte) []byte
	// Done answers a started call (asuka_native_done). Any goroutine.
	Done func(call uint64, answer []byte)
}

type object struct {
	srv *Server
	v   any
}

// NewSession is a session whose callbacks go through invoke and whose
// started calls answer through done.
func NewSession(invoke func(id uint64, args []byte) []byte, done func(call uint64, answer []byte)) *Session {
	return &Session{objects: map[uint64]object{}, calls: map[uint64]context.CancelFunc{}, Invoke: invoke, Done: done}
}

// Handle gives obj (of srv's package) a handle the core holds.
func (s *Session) Handle(srv *Server, obj any) uint64 {
	if obj == nil {
		return 0
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	s.next++
	s.objects[s.next] = object{srv, obj}
	return s.next
}

// Object is the object of a handle (nil for 0 or one let go).
func (s *Session) Object(h uint64) any {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.objects[h].v
}

// answer statuses (module.go's).
func status(w *Writer, srv *Server, err error) {
	switch {
	case err == nil:
	case errors.Is(err, context.Canceled):
		w.b = append(w.b[:0], 4)
	case errors.Is(err, errors.ErrUnsupported):
		w.b = append(w.b[:0], 3)
		w.String(err.Error())
	default:
		for i, e := range srv.Errors {
			if errors.Is(err, e) {
				w.b = append(w.b[:0], 1)
				w.Uint32(uint32(i))
				return
			}
		}
		var ne *Error
		w.b = append(w.b[:0], 2)
		if errors.As(err, &ne) {
			w.String(ne.Domain)
			w.Int64(ne.Code)
			w.String(ne.Message)
			return
		}
		w.String("go")
		w.Int64(0)
		w.String(err.Error())
	}
}

// New answers native_new: u8 status, then u64 handle.
func (s *Session) New(class string, iface uint32) []byte {
	w := &Writer{b: []byte{0}}
	serversMu.Lock()
	srv := servers[class]
	serversMu.Unlock()
	if srv == nil {
		w.b = append(w.b[:0], 3)
		w.String(fmt.Sprintf("%s has no Go implementation (its desktop/ package isn't in the build)", class))
		return w.b
	}
	obj, err := srv.New(iface)
	if err != nil {
		status(w, srv, err)
		return w.b
	}
	w.Uint64(s.Handle(srv, obj))
	return w.b
}

// Call answers native_call.
func (s *Session) Call(handle uint64, method uint32, args []byte) []byte {
	s.mu.Lock()
	o, ok := s.objects[handle]
	s.mu.Unlock()
	w := &Writer{b: []byte{0}}
	if !ok {
		w.b = append(w.b[:0], 3)
		w.String("the object was let go")
		return w.b
	}
	err := o.srv.Call(context.Background(), s, o.v, method, NewReader(args), w)
	status(w, o.srv, err)
	return w.b
}

// Start answers native_start: the call runs on a goroutine of its own,
// and answers through Done.
func (s *Session) Start(handle uint64, method uint32, args []byte, call uint64) {
	in := append([]byte(nil), args...)
	ctx, cancel := context.WithCancel(context.Background())
	s.mu.Lock()
	o, ok := s.objects[handle]
	s.calls[call] = cancel
	s.mu.Unlock()
	go func() {
		w := &Writer{b: []byte{0}}
		if !ok {
			w.b = append(w.b[:0], 3)
			w.String("the object was let go")
		} else {
			status(w, o.srv, o.srv.Call(ctx, s, o.v, method, NewReader(in), w))
		}
		s.mu.Lock()
		delete(s.calls, call)
		s.mu.Unlock()
		cancel()
		s.Done(call, w.b)
	}()
}

// Cancel answers native_cancel.
func (s *Session) Cancel(call uint64) {
	s.mu.Lock()
	c := s.calls[call]
	s.mu.Unlock()
	if c != nil {
		c()
	}
}

// Releaser is an object that hears the core let it go (Released, on the
// main thread): to free what it holds, as a Swift object's deinit does.
type Releaser interface{ Released() }

// Release answers native_release: the core let the object go.
func (s *Session) Release(handle uint64) {
	s.mu.Lock()
	o, ok := s.objects[handle]
	delete(s.objects, handle)
	s.mu.Unlock()
	if r, is := o.v.(Releaser); ok && is {
		Post(r.Released)
	}
}
