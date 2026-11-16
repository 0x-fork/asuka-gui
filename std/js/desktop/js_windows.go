//go:build windows

package js

import (
	. "github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: a WebView2 page that's never shown (internal/windows'
// WebPage, in the app's WebView2 environment), the script run through
// DevTools' Runtime.evaluate, which awaits a promise.

func supported() error { return nil }

// start loads a blank page and runs script in it; done gets its value's
// JSON, once. stop lets the page go. Main thread.
func start(script string, done func([]byte, error)) (stop func()) {
	var page *WebPage
	stopped := false
	end := func() {
		stopped = true
		if page != nil {
			page.Close()
			page = nil
		}
	}
	LoadWebPage(appName(), "<!doctype html><html></html>", func(p *WebPage, err error) {
		if stopped {
			if p != nil {
				p.Close()
			}
			return
		}
		if err != nil {
			done(nil, err)
			return
		}
		page = p
		p.Evaluate(native.Post, script, func(b []byte, err error) {
			end()
			done(b, err)
		})
	})
	return end
}
