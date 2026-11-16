//go:build windows

package webauth

import (
	"errors"
	"os"
	"path/filepath"
	"time"
	"unsafe"

	"github.com/egoist/asuka/internal/localfile"
	. "github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: a window owned by the one the user works in (which waits,
// disabled, while it's open), with WebView2 in it: the app's web profile,
// or with ephemeral a throwaway one in a folder of its own.

func supported() error { return nil }

// WebView2's vtable slots the sign-in uses.
const (
	envCreateController      = 3
	ctlPutIsVisible          = 4
	ctlPutBounds             = 6
	ctlClose                 = 24
	ctlGetCoreWebView2       = 25
	wvNavigate               = 5
	wvAddNavigationStarting  = 7
	wvAddNewWindowRequested  = 44
	wvAddDocumentTitleChange = 46
	wvGetDocumentTitle       = 48
	navStartingGetURI        = 3
	navStartingPutCancel     = 5
	newWinGetURI             = 3
	newWinPutHandled         = 6
)

type session struct {
	hwnd, owner      uintptr
	controller, view uintptr
	env              uintptr // an ephemeral profile's
	dir              string  // its folder
	done             bool
	reply            func(string, error)
}

func show(link, host, scheme string, ephemeral bool, reply func(string, error)) (stop func()) {
	s := &session{reply: reply, owner: native.Window()}
	k := float64(DPI(s.owner)) / 96
	w, h := int32(480*k), int32(640*k)
	x, y := int32(-0x80000000), int32(-0x80000000) // CW_USEDEFAULT
	if s.owner != 0 {
		r := WindowRect(s.owner)
		x, y = r.Left+(r.W()-w)/2, r.Top+(r.H()-h)/2
	}
	s.hwnd = NewWindow(0, host, WS_OVERLAPPEDWINDOW, x, y, w, h, s.owner, func(hwnd uintptr, m uint32, _, _ uintptr) (uintptr, bool) {
		switch m {
		case WM_SIZE:
			if s.controller != 0 {
				ComCallRect(s.controller, ctlPutBounds, ClientRect(hwnd))
			}
		case WM_CLOSE:
			s.finish("", ErrCanceled)
			return 0, true
		}
		return 0, false
	})
	if s.hwnd == 0 {
		reply("", errors.New("webauth: no window for the sign-in"))
		return func() {}
	}
	if s.owner != 0 {
		EnableWindow(s.owner, false)
	}
	ShowWindow(s.hwnd, SW_SHOW)
	withEnv := func(env uintptr, err error) {
		if s.done {
			return
		}
		if env == 0 {
			s.finish("", errors.New("webauth: couldn't start the sign-in: WebView2 isn't there"))
			return
		}
		hr := WithHandler(func(hr, controller uintptr) {
			if Failed(hr) || controller == 0 || s.done {
				if !s.done {
					s.finish("", errors.New("webauth: couldn't start the sign-in"))
				}
				return
			}
			AddRef(controller)
			s.controller = controller
			ComCall(controller, ctlGetCoreWebView2, uintptr(unsafe.Pointer(&s.view)))
			ComCallRect(controller, ctlPutBounds, ClientRect(s.hwnd))
			var tok int64
			add := func(index int, fn func(a, b uintptr)) {
				WithHandler(fn, func(handler uintptr) uintptr {
					return ComCall(s.view, index, handler, uintptr(unsafe.Pointer(&tok)))
				})
			}
			add(wvAddNavigationStarting, func(_, args uintptr) {
				var p uintptr
				ComCall(args, navStartingGetURI, uintptr(unsafe.Pointer(&p)))
				to := TakeString(p)
				if s.done || !isCallback(to, scheme) {
					return
				}
				ComCall(args, navStartingPutCancel, 1)
				native.Post(func() { s.finish(to, nil) })
			})
			add(wvAddNewWindowRequested, func(_, args uintptr) {
				// A popup (a provider's "sign in with...") opens here.
				var p uintptr
				ComCall(args, newWinGetURI, uintptr(unsafe.Pointer(&p)))
				to := TakeString(p)
				ComCall(args, newWinPutHandled, 1)
				ComCall(s.view, wvNavigate, Str(to))
			})
			add(wvAddDocumentTitleChange, func(_, _ uintptr) {
				var p uintptr
				ComCall(s.view, wvGetDocumentTitle, uintptr(unsafe.Pointer(&p)))
				if t := TakeString(p); t != "" && !s.done {
					SetWindowText(s.hwnd, t)
				}
			})
			ComCall(controller, ctlPutIsVisible, 1)
			ComCall(s.view, wvNavigate, Str(link))
		}, func(handler uintptr) uintptr {
			return ComCall(env, envCreateController, s.hwnd, handler)
		})
		if Failed(hr) {
			s.finish("", errors.New("webauth: couldn't start the sign-in"))
		}
	}
	if !ephemeral {
		WhenWebEnvironment(localfile.AppName(), withEnv)
		return func() { s.finish("", errors.New("canceled")) }
	}
	// A profile of its own, in a new folder, thrown away after.
	base := filepath.Join(os.Getenv("LOCALAPPDATA"), localfile.AppName(), "WebView2-signin")
	os.MkdirAll(base, 0o755)
	dir, err := os.MkdirTemp(base, "ephemeral-")
	if err != nil {
		s.finish("", err)
		return func() {}
	}
	s.dir = dir
	if err := CreateWebView2Environment(dir, func(env uintptr, err error) {
		s.env = env
		withEnv(env, err)
	}); err != nil {
		withEnv(0, err)
	}
	return func() { s.finish("", errors.New("canceled")) }
}

// finish answers the session, once, and closes its window (on a later
// turn: it's often called from WebView2's callbacks).
func (s *session) finish(to string, err error) {
	if s.done {
		return
	}
	s.done = true
	s.reply(to, err)
	controller, view, env, dir, hwnd, owner := s.controller, s.view, s.env, s.dir, s.hwnd, s.owner
	s.controller, s.view, s.env = 0, 0, 0
	native.Post(func() {
		if controller != 0 {
			ComCall(controller, ctlClose)
			Release(view)
			Release(controller)
		}
		if owner != 0 {
			EnableWindow(owner, true)
			SetForegroundWindow(owner)
		}
		if hwnd != 0 {
			DestroyWindow(hwnd)
		}
		if env != 0 {
			Release(env)
		}
		if dir != "" {
			// The browser's processes let the folder go once they've exited.
			go func() {
				for i := 0; i < 20; i++ {
					if os.RemoveAll(dir) == nil {
						return
					}
					time.Sleep(500 * time.Millisecond)
				}
			}()
		}
	})
}
