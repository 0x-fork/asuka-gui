//go:build windows

package windows

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"unsafe"
)

// The process's WebView2 environment, which the Windows host's WebViews
// and native packages' hidden pages (asuka/js, asuka/print) share: one
// browser process, one profile in the app's local data. Main thread.

var (
	webEnv      uintptr
	webEnvErr   error
	webEnvWait  []func(uintptr, error)
	webEnvStart bool
)

// WhenWebEnvironment calls f on the main thread with the process's
// WebView2 environment once it's made (at once if it is), or why there's
// none. app names its data folder (%LOCALAPPDATA%\<app>\WebView2). Main
// thread.
func WhenWebEnvironment(app string, f func(env uintptr, err error)) {
	if webEnv != 0 || webEnvErr != nil {
		f(webEnv, webEnvErr)
		return
	}
	webEnvWait = append(webEnvWait, f)
	if webEnvStart {
		return
	}
	webEnvStart = true
	// Videos play without a click, as a WebView's autoPlay asks.
	os.Setenv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", "--autoplay-policy=no-user-gesture-required")
	dir := filepath.Join(os.Getenv("LOCALAPPDATA"), app, "WebView2")
	os.MkdirAll(dir, 0o755)
	made := func(env uintptr, err error) {
		webEnv, webEnvErr = env, err
		if err != nil {
			fmt.Fprintln(os.Stderr, "asuka:", err)
		}
		fs := webEnvWait
		webEnvWait = nil
		for _, f := range fs {
			f(env, err)
		}
	}
	if err := CreateWebView2Environment(dir, made); err != nil {
		made(0, err)
	}
}

// WebView2's vtable slots a hidden page uses.
const (
	pageEnvCreateController       = 3
	pageCtlPutBounds              = 6
	pageCtlClose                  = 24
	pageCtlGetCoreWebView2        = 25
	pageWvNavigateToString        = 6
	pageWvAddNavigationCompleted  = 15
	pageWvExecuteScript           = 29
	pageWvCallDevToolsProtocol    = 36
	pageWv16ShowPrintUI           = 114
	pageNavCompletedGetIsSuccess  = 3
	PrintDialogBrowser            = 0 // ShowPrintUI's kinds
	PrintDialogSystem             = 1
	webPageWidth, webPageHeight   = 816, 1056 // a Letter page at 96 dpi
	webPageClass                  = "STATIC"
	webPageStyle           uint32 = WS_POPUP
)

var iidPageWebView2_16 = NewGUID("0eb34dc9-9f91-41e1-8639-95cd5943906b")

// A WebPage is markup in a WebView2 that's never shown: in a hidden
// window of its own, a Letter page wide.
type WebPage struct {
	Window, Controller, View uintptr
}

// LoadWebPage loads html in a hidden WebView2, then calls done on the
// main thread with the page, which its Close lets go. app is the app's
// name (WhenWebEnvironment's). Main thread.
func LoadWebPage(app, html string, done func(*WebPage, error)) {
	p := &WebPage{Window: CreateWindow(0, webPageClass, "", webPageStyle, 0, 0, webPageWidth, webPageHeight, 0, 0)}
	if p.Window == 0 {
		done(nil, errors.New("WebView2: no window for the page"))
		return
	}
	WhenWebEnvironment(app, func(env uintptr, err error) {
		if env == 0 {
			p.Close()
			if err == nil {
				err = errors.New("WebView2 isn't available")
			}
			done(nil, err)
			return
		}
		hr := WithHandler(func(hr, controller uintptr) {
			if Failed(hr) || controller == 0 {
				p.Close()
				done(nil, errors.New("WebView2 couldn't make a view"))
				return
			}
			AddRef(controller)
			p.Controller = controller
			ComCall(controller, pageCtlGetCoreWebView2, uintptr(unsafe.Pointer(&p.View)))
			ComCallRect(controller, pageCtlPutBounds, Rect{Right: webPageWidth, Bottom: webPageHeight})
			var tok int64
			finished := false
			WithHandler(func(_, args uintptr) {
				if finished {
					return
				}
				finished = true
				var ok int32
				ComCall(args, pageNavCompletedGetIsSuccess, uintptr(unsafe.Pointer(&ok)))
				if ok == 0 {
					p.Close()
					done(nil, errors.New("WebView2: the page couldn't load"))
					return
				}
				done(p, nil)
			}, func(handler uintptr) uintptr {
				return ComCall(p.View, pageWvAddNavigationCompleted, handler, uintptr(unsafe.Pointer(&tok)))
			})
			ComCall(p.View, pageWvNavigateToString, Str(html))
		}, func(handler uintptr) uintptr {
			return ComCall(env, pageEnvCreateController, p.Window, handler)
		})
		if Failed(hr) {
			p.Close()
			done(nil, fmt.Errorf("WebView2 couldn't make a view (0x%x)", uint32(hr)))
		}
	})
}

// Close lets the page go. Main thread.
func (p *WebPage) Close() {
	if p.Controller != 0 {
		ComCall(p.Controller, pageCtlClose)
		Release(p.View)
		Release(p.Controller)
		p.Controller, p.View = 0, 0
	}
	if p.Window != 0 {
		DestroyWindow(p.Window)
		p.Window = 0
	}
}

// DevTools calls a DevTools protocol method in the page
// (CallDevToolsProtocolMethod); done gets its answer's JSON on a later
// turn of the main thread (post: the host's Post), never inside
// WebView2's callback. Main thread.
func (p *WebPage) DevTools(post func(func()), method string, params any, done func(json.RawMessage, error)) {
	b, err := json.Marshal(params)
	if err != nil {
		done(nil, err)
		return
	}
	hr := WithHandler(func(hr, result uintptr) {
		var err error
		var r json.RawMessage
		if Failed(hr) {
			err = fmt.Errorf("WebView2: %s failed (0x%x)", method, uint32(hr))
		} else {
			r = json.RawMessage(GoStr(result))
		}
		post(func() { done(r, err) })
	}, func(handler uintptr) uintptr {
		return ComCall(p.View, pageWvCallDevToolsProtocol, Str(method), Str(string(b)), handler)
	})
	if Failed(hr) {
		done(nil, fmt.Errorf("WebView2: %s failed (0x%x)", method, uint32(hr)))
	}
}

// Evaluate runs script in the page as a script of its own (its last
// statement's value; a promise's, awaited) and answers with the value as
// JSON (null for undefined), or what it threw. Main thread.
func (p *WebPage) Evaluate(post func(func()), script string, done func([]byte, error)) {
	p.DevTools(post, "Runtime.evaluate", map[string]any{"expression": script, "awaitPromise": true, "returnByValue": true},
		func(r json.RawMessage, err error) {
			if err != nil {
				done(nil, err)
				return
			}
			done(Evaluated(r))
		})
}

// Evaluated is DevTools' Runtime.evaluate answer: the value's JSON (null
// for undefined), or what was thrown.
func Evaluated(r json.RawMessage) ([]byte, error) {
	var res struct {
		Result struct {
			Value json.RawMessage `json:"value"`
		} `json:"result"`
		Exception *struct {
			Text      string `json:"text"`
			Exception *struct {
				Description string `json:"description"`
			} `json:"exception"`
		} `json:"exceptionDetails"`
	}
	if err := json.Unmarshal(r, &res); err != nil {
		return nil, err
	}
	if e := res.Exception; e != nil {
		msg := e.Text
		if e.Exception != nil && e.Exception.Description != "" {
			msg = e.Exception.Description
			if i := strings.IndexByte(msg, '\n'); i > 0 {
				msg = msg[:i] // the message, not the stack
			}
		}
		return nil, errors.New(msg)
	}
	if len(res.Result.Value) == 0 {
		return []byte("null"), nil
	}
	return res.Result.Value, nil
}

// ShowPrintUI shows a print dialog for the page a WebView2 (view) shows:
// the browser's print preview (PrintDialogBrowser) or the system's
// (PrintDialogSystem); window.print() where WebView2 is too old for it.
func ShowPrintUI(view uintptr, kind uintptr) {
	if wv16 := QueryInterface(view, &iidPageWebView2_16); wv16 != 0 {
		ComCall(wv16, pageWv16ShowPrintUI, kind)
		Release(wv16)
		return
	}
	ComCall(view, pageWvExecuteScript, Str("window.print()"), 0)
}
