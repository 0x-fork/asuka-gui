//go:build windows

package windows

import (
	"sync"
	"syscall"
)

// Message windows for native packages' Go: a hidden window (HWND_MESSAGE)
// whose messages go to a Go function, for what Win32 tells a window (MCI's
// MM_MCINOTIFY, a COM object's window messages).

var (
	msgOnce     sync.Once
	msgHandlers sync.Map // hwnd → func(msg uint32, wparam, lparam uintptr)
	msgProc     uintptr
)

const msgClass = "AsukaPackageMessages"

// NewMessageWindow makes a message-only window on the calling thread
// (the main thread: its messages come there, through the host's message
// loop) whose messages handle hears. Destroy it with DestroyWindow.
func NewMessageWindow(handle func(msg uint32, wparam, lparam uintptr)) uintptr {
	registerMsgClass()
	hwnd := CreateWindow(0, msgClass, "", 0, 0, 0, 0, 0, hwndMessage, 0)
	if hwnd != 0 {
		msgHandlers.Store(hwnd, handle)
	}
	return hwnd
}

func registerMsgClass() {
	msgOnce.Do(func() {
		msgProc = syscall.NewCallback(func(hwnd uintptr, m uint32, w, l uintptr) uintptr {
			if f, ok := msgHandlers.Load(hwnd); ok && m >= WM_USER {
				f.(func(uint32, uintptr, uintptr))(m, w, l)
				return 0
			}
			if f, ok := msgHandlers.Load(hwnd); ok && m == 0x03B9 { // MM_MCINOTIFY
				f.(func(uint32, uintptr, uintptr))(m, w, l)
				return 0
			}
			return DefWindowProc(hwnd, m, w, l)
		})
		RegisterClass(msgClass, 0, msgProc, 0)
	})
}

// NewHiddenWindow makes a top-level window that's never shown, of the
// same class (its messages go to handle): what the platform ties to a
// window (media controls) when the app has none of its own.
func NewHiddenWindow(title string, handle func(msg uint32, wparam, lparam uintptr)) uintptr {
	registerMsgClass()
	hwnd := CreateWindow(0, msgClass, title, WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, 0, 0)
	if hwnd != 0 {
		msgHandlers.Store(hwnd, handle)
	}
	return hwnd
}

var (
	procOnce    sync.Once
	procWindows sync.Map // hwnd → func(hwnd uintptr, m uint32, wp, lp uintptr) (uintptr, bool)
)

const procClass = "AsukaPackageWindow"

// NewProcWindow makes a window whose messages go to handle first (ok:
// handled, its result returned), then DefWindowProc: a native package's
// child window (WS_CHILD in a view's parent) or top-level one. Main
// thread.
func NewProcWindow(ex uint32, title string, style uint32, x, y, w, h int32, parent uintptr, handle func(hwnd uintptr, m uint32, wp, lp uintptr) (uintptr, bool)) uintptr {
	procOnce.Do(func() {
		proc := syscall.NewCallback(func(hwnd, m, wp, lp uintptr) uintptr {
			if f, ok := procWindows.Load(hwnd); ok {
				if r, done := f.(func(uintptr, uint32, uintptr, uintptr) (uintptr, bool))(hwnd, uint32(m), wp, lp); done {
					return r
				}
			}
			if m == WM_NCDESTROY {
				procWindows.Delete(hwnd)
			}
			return DefWindowProc(hwnd, uint32(m), wp, lp)
		})
		RegisterClass(procClass, CS_HREDRAW|CS_VREDRAW, proc, AppIcon())
	})
	hwnd := CreateWindow(ex, procClass, title, style, x, y, w, h, parent, 0)
	if hwnd != 0 && handle != nil {
		procWindows.Store(hwnd, handle)
	}
	return hwnd
}
