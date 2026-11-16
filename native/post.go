package native

import "sync"

var (
	postMu sync.Mutex
	post   func(func())
)

// SetPost says how to run a function on the main thread: the desktop
// host's bridge sets it as it starts the core.
func SetPost(p func(func())) {
	postMu.Lock()
	post = p
	postMu.Unlock()
}

// Post runs f on the main thread, soon: where a desktop implementation
// touches the platform's UI and event loop (GTK, COM's apartment, AppKit)
// and calls its callbacks, which the core runs on that thread. Any
// goroutine. Without a host (a test), f runs now.
func Post(f func()) {
	postMu.Lock()
	p := post
	postMu.Unlock()
	if p == nil {
		f()
		return
	}
	p(f)
}

// OnMain runs f on the main thread and waits for it: for a started
// call's goroutine that needs what only the main thread may touch.
func OnMain(f func()) {
	done := make(chan struct{})
	Post(func() {
		defer close(done)
		f()
	})
	<-done
}

// Under the C core, a native backend's answers (Done) and callbacks'
// calls (Invoke) are the core's: the host's bridge routes them there.
type routes struct {
	done   func(call uint64, answer []byte)
	invoke func(id uint64, args []byte) []byte
}

var core *routes

// SetCoreRoutes makes Done and Invoke the core's (the bridge's).
func SetCoreRoutes(done func(call uint64, answer []byte), invoke func(id uint64, args []byte) []byte) {
	postMu.Lock()
	core = &routes{done, invoke}
	postMu.Unlock()
}

func coreRoutes() *routes {
	postMu.Lock()
	defer postMu.Unlock()
	return core
}
