//go:build darwin || linux || windows

// Package bridge drives Asuka's C core from a host written in Go: the
// desktop hosts (host/darwin, host/windows, host/linux). The core is a
// library (libasuka: the core, its VM, and an app compiled to C or
// loaded as bytecode) that the host loads with purego, without cgo, and
// talks to through core/include/asuka.h, as the Swift and Kotlin hosts
// do: the core sends encoded batches and asks for measurements; the
// host sends encoded events and answers the lists' questions.
package bridge

import (
	"fmt"
	"math"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"unsafe"

	"github.com/ebitengine/purego"

	"github.com/egoist/asuka/host"
	"github.com/egoist/asuka/layout"
	"github.com/egoist/asuka/native"
	"github.com/egoist/asuka/ops"
)

// callbacks mirrors asuka_host_callbacks (core/include/asuka.h): the
// function pointers, in order.
type callbacks struct {
	apply, measure, post                                                        uintptr
	nativeNew, nativeCall, nativeStart, nativeCancel, nativeRelease, nativeFree uintptr
	measureV                                                                    uintptr
}

// Core is a loaded libasuka. It implements host.Core for the host that
// started it.
type Core struct {
	h     host.Host
	cbs   *callbacks
	batch ops.Batch
	ev    []byte

	start      func(cbs *callbacks)
	load       func(p *byte, n uintptr, err **byte) int32
	run        func(err **byte) int32
	devConnect func(host string, port int32, hello string, err **byte) int32
	freeString func(p *byte)
	event      func(p *byte, n uintptr)
	listCount  func(list uint32) int64
	listBind   func(list uint32, index int64, cell uint32)
	listUnbind func(list uint32, cell uint32)
	runPosted  func()
	attach     func()
	nativeDone func(call uint64, p *byte, n uintptr)
	invoke     func(id uint64, p *byte, n uintptr, out **byte) uintptr
	cFree      func(p unsafe.Pointer)

	natives *native.Session
}

var (
	mu      sync.Mutex
	current *Core // the core whose callbacks are installed (one per process)
)

// LibName is the core library's file name on this system.
func LibName() string {
	switch runtime.GOOS {
	case "darwin":
		return "libasuka.dylib"
	case "windows":
		return "asuka.dll"
	}
	return "libasuka.so"
}

// Find returns the core library's path: ASUKA_CORE_LIB, or LibName
// beside the executable (or in a .app's Frameworks).
func Find() (string, error) {
	if p := os.Getenv("ASUKA_CORE_LIB"); p != "" {
		return p, nil
	}
	exe, err := os.Executable()
	if err != nil {
		return "", err
	}
	dir := filepath.Dir(exe)
	for _, p := range []string{
		filepath.Join(dir, LibName()),
		filepath.Join(dir, "..", "Frameworks", LibName()),
		filepath.Join(dir, "lib", LibName()),
	} {
		if _, err := os.Stat(p); err == nil {
			return p, nil
		}
	}
	return "", fmt.Errorf("bridge: %s not found beside %s (set ASUKA_CORE_LIB)", LibName(), exe)
}

// Open loads the core library at path.
func Open(path string) (*Core, error) {
	lib, err := openLib(path)
	if err != nil {
		return nil, fmt.Errorf("bridge: loading %s: %w", path, err)
	}
	c := &Core{}
	purego.RegisterLibFunc(&c.start, lib, "asuka_start")
	purego.RegisterLibFunc(&c.load, lib, "asuka_load")
	purego.RegisterLibFunc(&c.run, lib, "asuka_run")
	purego.RegisterLibFunc(&c.devConnect, lib, "asuka_dev_connect")
	purego.RegisterLibFunc(&c.freeString, lib, "asuka_free_string")
	purego.RegisterLibFunc(&c.event, lib, "asuka_event")
	purego.RegisterLibFunc(&c.listCount, lib, "asuka_list_count")
	purego.RegisterLibFunc(&c.listBind, lib, "asuka_list_bind")
	purego.RegisterLibFunc(&c.listUnbind, lib, "asuka_list_unbind")
	purego.RegisterLibFunc(&c.runPosted, lib, "asuka_run_posted")
	purego.RegisterLibFunc(&c.attach, lib, "asuka_attach")
	purego.RegisterLibFunc(&c.nativeDone, lib, "asuka_native_done")
	purego.RegisterLibFunc(&c.invoke, lib, "asuka_native_invoke")
	purego.RegisterLibFunc(&c.cFree, lib, "asuka_free")
	// Native packages implemented in Go (their desktop/ packages, built
	// into the host) answer the core's native calls.
	c.natives = native.NewSession(c.invokeCallback, c.done)
	return c, nil
}

// answer hands an answer to the core: in out when it fits, else in a
// buffer the core frees with native_free (Go memory, kept alive until
// then).
var (
	bigMu sync.Mutex
	bigs  = map[uintptr][]byte{}
)

func answer(b []byte, out unsafe.Pointer, capacity uintptr, big unsafe.Pointer) uintptr {
	if uintptr(len(b)) <= capacity {
		copy(unsafe.Slice((*byte)(out), capacity), b)
		return uintptr(len(b))
	}
	buf := append([]byte(nil), b...)
	p := uintptr(unsafe.Pointer(&buf[0]))
	bigMu.Lock()
	bigs[p] = buf
	bigMu.Unlock()
	*(*uintptr)(big) = p
	return uintptr(len(b))
}

func (c *Core) invokeCallback(id uint64, args []byte) []byte {
	var out *byte
	var p *byte
	if len(args) > 0 {
		p = &args[0]
	}
	n := c.invoke(id, p, uintptr(len(args)), &out)
	if out == nil || n == 0 {
		return nil
	}
	b := append([]byte(nil), unsafe.Slice(out, n)...)
	c.cFree(unsafe.Pointer(out))
	return b
}

func (c *Core) done(call uint64, b []byte) {
	var p *byte
	if len(b) > 0 {
		p = &b[0]
	}
	// asuka_native_done copies the answer and posts it to the main thread.
	c.nativeDone(call, p, uintptr(len(b)))
}

// The callbacks, made once: purego's are never freed.
var cbs = func() *callbacks {
	return &callbacks{
		apply: purego.NewCallback(func(buf unsafe.Pointer, n uintptr) uintptr {
			current.applyBatch(buf, n)
			return 0
		}),
		post: purego.NewCallback(func() uintptr {
			c := current
			c.h.Post(func() { c.runPosted() })
			return 0
		}),
		// Floats can't cross purego's callbacks everywhere (Windows), so
		// the host takes measure_v: the four inputs and two outputs by
		// pointer.
		measureV: purego.NewCallback(func(id uintptr, in, out unsafe.Pointer) uintptr {
			current.measureV(uint32(id), in, out)
			return 0
		}),
		nativeNew: purego.NewCallback(func(cls unsafe.Pointer, clsLen, iface uintptr, out unsafe.Pointer, capacity uintptr, big unsafe.Pointer) uintptr {
			name := string(unsafe.Slice((*byte)(cls), clsLen))
			return answer(current.natives.New(name, uint32(iface)), out, capacity, big)
		}),
		nativeCall: purego.NewCallback(func(handle, method uintptr, in unsafe.Pointer, n uintptr, out unsafe.Pointer, capacity uintptr, big unsafe.Pointer) uintptr {
			return answer(current.natives.Call(uint64(handle), uint32(method), bytesAt(in, n)), out, capacity, big)
		}),
		nativeStart: purego.NewCallback(func(handle, method uintptr, in unsafe.Pointer, n, call uintptr) uintptr {
			current.natives.Start(uint64(handle), uint32(method), bytesAt(in, n), uint64(call))
			return 0
		}),
		nativeCancel: purego.NewCallback(func(call uintptr) uintptr {
			current.natives.Cancel(uint64(call))
			return 0
		}),
		nativeRelease: purego.NewCallback(func(handle uintptr) uintptr {
			current.natives.Release(uint64(handle))
			return 0
		}),
		nativeFree: purego.NewCallback(func(p uintptr) uintptr {
			bigMu.Lock()
			delete(bigs, p)
			bigMu.Unlock()
			return 0
		}),
	}
}()

func bytesAt(p unsafe.Pointer, n uintptr) []byte {
	if p == nil || n == 0 {
		return nil
	}
	return unsafe.Slice((*byte)(p), n)
}

// Start gives the core its host: from then on it may apply batches and
// measure. Main thread.
func (c *Core) Start(h host.Host) {
	mu.Lock()
	c.h = h
	current = c
	mu.Unlock()
	c.cbs = cbs
	c.start(c.cbs)
	if conn, ok := h.(host.Connector); ok {
		conn.Connect(c)
	}
}

// Load runs a compiled module (bytecode, lang/bytecode): its app mounts
// as the host's root.
func (c *Core) Load(module []byte) error {
	if len(module) == 0 {
		return fmt.Errorf("bridge: an empty module")
	}
	var msg *byte
	if c.load(&module[0], uintptr(len(module)), &msg) != 0 {
		return c.failed(msg)
	}
	return nil
}

// Run runs the app compiled into the core library (a release build).
func (c *Core) Run() error {
	var msg *byte
	if c.run(&msg) != 0 {
		return c.failed(msg)
	}
	return nil
}

// DevConnect connects to asuka dev at host:port: the CLI sends the app's
// bytecode, and again on every save.
func (c *Core) DevConnect(host string, port int, hello string) error {
	var msg *byte
	if c.devConnect(host, int32(port), hello, &msg) != 0 {
		return c.failed(msg)
	}
	return nil
}

func (c *Core) failed(msg *byte) error {
	if msg == nil {
		return fmt.Errorf("asuka: the core failed (see its log)")
	}
	n := 0
	for *(*byte)(unsafe.Add(unsafe.Pointer(msg), n)) != 0 {
		n++
	}
	s := string(unsafe.Slice(msg, n))
	c.freeString(msg)
	return fmt.Errorf("%s", s)
}

// Attach tells the core its host's root was made again.
func (c *Core) Attach() { c.attach() }

func (c *Core) applyBatch(buf unsafe.Pointer, n uintptr) {
	data := unsafe.Slice((*byte)(buf), n)
	c.batch.Reset()
	if err := c.batch.Decode(data); err != nil {
		fmt.Fprintln(os.Stderr, "asuka: a batch the host can't read:", err)
		return
	}
	c.h.Apply(&c.batch)
}

func (c *Core) measureV(id uint32, in, out unsafe.Pointer) {
	f := unsafe.Slice((*float32)(in), 4)
	s := c.h.Measure(ops.NodeID(id), layout.Constraints{Width: f[0], Height: f[1], AvailW: f[2], AvailH: f[3]})
	o := unsafe.Slice((*float32)(out), 2)
	o[0], o[1] = s.W, s.H
	if math.IsNaN(float64(o[0])) {
		o[0] = 0
	}
	if math.IsNaN(float64(o[1])) {
		o[1] = 0
	}
}

// Dispatch implements host.Core.
func (c *Core) Dispatch(ev ops.Event) {
	c.ev = ops.AppendEvent(c.ev[:0], &ev)
	if len(c.ev) == 0 {
		return
	}
	c.event(&c.ev[0], uintptr(len(c.ev)))
}

// ListCount implements host.Core.
func (c *Core) ListCount(list ops.NodeID) int { return int(c.listCount(uint32(list))) }

// ListBind implements host.Core.
func (c *Core) ListBind(list ops.NodeID, index int, cell ops.NodeID) {
	c.listBind(uint32(list), int64(index), uint32(cell))
}

// ListUnbind implements host.Core.
func (c *Core) ListUnbind(list ops.NodeID, cell ops.NodeID) { c.listUnbind(uint32(list), uint32(cell)) }

var _ host.Core = (*Core)(nil)

// Mount starts the C core (Find's library) with h as its host and starts
// the app: the bytecode asuka dev sends when ASUKA_DEV (host:port) is
// set, else the app compiled into the library. The desktop hosts call it
// on the main thread, where they mounted a Go root before.
func Mount(h host.Host) (*Core, error) {
	path, err := Find()
	if err != nil {
		return nil, err
	}
	c, err := Open(path)
	if err != nil {
		return nil, err
	}
	c.Start(h)
	if dev := os.Getenv("ASUKA_DEV"); dev != "" {
		i := strings.LastIndexByte(dev, ':')
		port, perr := strconv.Atoi(dev[i+1:])
		if i < 0 || perr != nil {
			return nil, fmt.Errorf("ASUKA_DEV=%q: want host:port", dev)
		}
		hello := fmt.Sprintf(`{"platform":%q,"device":"desktop"}`, runtime.GOOS)
		return c, c.DevConnect(dev[:i], port, hello)
	}
	return c, c.Run()
}
