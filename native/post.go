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
