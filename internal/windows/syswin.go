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
