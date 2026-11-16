//go:build windows

package windows

import "syscall"

// The system window: a top-level window that's never shown, which hears
// what Windows broadcasts (WM_DISPLAYCHANGE, WM_SETTINGCHANGE,
// WM_SYSCOLORCHANGE, WM_POWERBROADCAST: top-level windows hear them, a
// message-only window none), and the power and session notifications it
// registers for (WatchPower). Native packages' Go (asuka/appearance,
// asuka/power, asuka/screen) listen through it.

const classSystem = "AsukaSystemWindow"

var sysWin struct {
	hwnd     uintptr
	next     int
	handlers map[int]func(m uint32, wp, lp uintptr)
}

// OnSystemMessage calls f with each message the system window gets,
// making it the first time, until stop. Main thread.
func OnSystemMessage(f func(m uint32, wp, lp uintptr)) (stop func()) {
	s := &sysWin
	if s.hwnd == 0 {
		proc := syscall.NewCallback(func(hwnd, m, wp, lp uintptr) uintptr {
			for _, h := range s.handlers {
				h(uint32(m), wp, lp)
			}
			return DefWindowProc(hwnd, uint32(m), wp, lp)
		})
		RegisterClass(classSystem, 0, proc, 0)
		s.hwnd = CreateWindow(WS_EX_TOOLWINDOW, classSystem, "", WS_POPUP, 0, 0, 0, 0, 0, 0)
		s.handlers = map[int]func(uint32, uintptr, uintptr){}
		WatchPower(s.hwnd)
	}
	s.next++
	id := s.next
	s.handlers[id] = f
	return func() { delete(s.handlers, id) }
}

var (
	enumThreadProc uintptr
	enumThreadTo   *[]uintptr
)

// ThreadWindows are the visible top-level windows of the calling thread
// (on the main thread: the app's). Main thread.
func ThreadWindows() []uintptr {
	if enumThreadProc == 0 {
		enumThreadProc = syscall.NewCallback(func(hwnd, _ uintptr) uintptr {
			if IsWindowVisible(hwnd) {
				*enumThreadTo = append(*enumThreadTo, hwnd)
			}
			return 1
		})
	}
	var out []uintptr
	enumThreadTo = &out
	tid, _, _ := procGetCurrentThreadId.Call()
	procEnumThreadWindows.Call(tid, enumThreadProc, 0)
	enumThreadTo = nil
	return out
}

const classOwn = "AsukaPackageWindow"

var own struct {
	proc     uintptr
	handlers map[uintptr]func(hwnd uintptr, m uint32, wp, lp uintptr) (uintptr, bool)
}

// NewWindow makes a window of native packages' own class, whose messages
// go to handle first (true: handled, its answer); the rest to
// DefWindowProc. Main thread.
func NewWindow(ex uint32, title string, style uint32, x, y, w, h int32, owner uintptr, handle func(hwnd uintptr, m uint32, wp, lp uintptr) (uintptr, bool)) uintptr {
	if own.proc == 0 {
		own.handlers = map[uintptr]func(uintptr, uint32, uintptr, uintptr) (uintptr, bool){}
		own.proc = syscall.NewCallback(func(hwnd, m, wp, lp uintptr) uintptr {
			if f := own.handlers[hwnd]; f != nil {
				if r, ok := f(hwnd, uint32(m), wp, lp); ok {
					return r
				}
			}
			if m == WM_NCDESTROY {
				delete(own.handlers, hwnd)
			}
			return DefWindowProc(hwnd, uint32(m), wp, lp)
		})
		RegisterClass(classOwn, CS_HREDRAW|CS_VREDRAW, own.proc, AppIcon())
	}
	hwnd := CreateWindow(ex, classOwn, title, style, x, y, w, h, owner, 0)
	if hwnd != 0 && handle != nil {
		own.handlers[hwnd] = handle
	}
	return hwnd
}
