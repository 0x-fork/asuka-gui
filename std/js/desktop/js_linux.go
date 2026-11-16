//go:build linux && !android

package js

import (
	"errors"

	. "github.com/egoist/asuka/internal/gtk"
)

// Linux: a WebKitGTK web view that's never shown, the script run with
// webkit_web_view_call_async_javascript_function, which awaits a promise.

func supported() error { return LoadWebKit() }

// evalBody runs s by indirect eval (the global scope, the last
// statement's value) and returns its value as JSON.
const evalBody = `const v = await (0, eval)(s); return JSON.stringify(v === undefined ? null : v)`

// start loads a blank page and runs script in it; done gets its value's
// JSON, once. stop lets the view go. Main thread.
func start(script string, done func([]byte, error)) (stop func()) {
	v := ObjectRefSink(WebKitWebViewNew())
	var handler uint64
	ended := false
	end := func() {
		if ended {
			return
		}
		ended = true
		Disconnect(v, handler)
		WebKitWebViewStopLoading(v)
		ObjectUnref(v)
	}
	handler = Connect(v, "load-changed", func(ev uintptr) {
		if ev != WebKitLoadFinished || ended {
			return
		}
		d := WebGLib.VariantDictNew(0)
		WebGLib.VariantDictInsert(d, "s", VariantNewString(script))
		args := WebGLib.VariantRefSink(WebGLib.VariantDictEnd(d))
		WebGLib.VariantDictUnref(d)
		Async(func(a AsyncCall) {
			WebKitWebViewCallAsyncJavascriptFunction(v, evalBody, -1, args, 0, 0, 0, a.Callback, a.Data)
		}, func(src, res Ptr) {
			var gerr Ptr
			val := WebKitWebViewCallAsyncJavascriptFinish(src, res, &gerr)
			if ended {
				return
			}
			if gerr != 0 {
				_, _, msg := WebKitError(gerr)
				ErrorFree(gerr)
				end()
				done(nil, errors.New(msg))
				return
			}
			out := []byte("null")
			if val != 0 {
				if s := GoStringFree(JSCValueToString(val)); s != "" {
					out = []byte(s)
				}
				ObjectUnref(val)
			}
			end()
			done(out, nil)
		})
		WebGLib.VariantUnref(args)
	})
	WebKitWebViewLoadHTML(v, "<!doctype html><html></html>", 0)
	return end
}
