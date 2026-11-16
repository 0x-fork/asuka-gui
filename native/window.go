package native

import "sync"

var (
	windowMu sync.Mutex
	window   func() uintptr
)

// SetWindow says how to find the window the user works in: the desktop
// host's bridge sets it as it starts the core, for hosts that have one.
func SetWindow(w func() uintptr) {
	windowMu.Lock()
	window = w
	windowMu.Unlock()
}

// Window is the app's window the user works in (the one activated last,
// or a sheet over it): its HWND on Windows, its GtkWindow on Linux; 0
// without a host or a window. A desktop implementation owns its dialogs
// by it. Main thread.
func Window() uintptr {
	windowMu.Lock()
	w := window
	windowMu.Unlock()
	if w == nil {
		return 0
	}
	return w()
}
