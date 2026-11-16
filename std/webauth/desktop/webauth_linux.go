//go:build linux && !android

package webauth

import (
	"errors"
	"os"
	"strings"

	. "github.com/egoist/asuka/internal/gtk"
	"github.com/egoist/asuka/internal/l10n"
	"github.com/egoist/asuka/native"
)

// Linux, as ASWebAuthenticationSession on a Mac: a dialog over the window
// the user works in with a WebKitGTK web view; closing it (its Cancel
// button, Escape) cancels. Its cookies are WebKit's default session's
// (the app's web views'), or, with ephemeral, a session of its own, gone
// with it.

func supported() error { return LoadWebKit() }

// locale is the user's language for the Cancel button, as a BCP 47 tag.
func locale() string {
	for _, k := range []string{"LC_ALL", "LC_MESSAGES", "LANG"} {
		if v := os.Getenv(k); v != "" && v != "C" && v != "POSIX" {
			v, _, _ = strings.Cut(v, ".")
			v, _, _ = strings.Cut(v, "@")
			return strings.ReplaceAll(v, "_", "-")
		}
	}
	return "en"
}

func show(link, host, scheme string, ephemeral bool, reply func(string, error)) (stop func()) {
	var view Ptr
	if ephemeral {
		view = WebKitWebViewNewEphemeral()
	} else {
		view = WebKitWebViewNew()
	}
	ObjectRefSink(view)
	WidgetSetVExpand(view, true)
	dialog := ObjectRefSink(AdwDialogNew())
	AdwDialogSetContentWidth(dialog, 560)
	AdwDialogSetContentHeight(dialog, 680)
	tv := AdwToolbarViewNew()
	bar := AdwHeaderBarNew()
	AdwHeaderBarSetShowStartTitleButtons(bar, false)
	AdwHeaderBarSetShowEndTitleButtons(bar, false)
	cancel := ButtonNew()
	ButtonSetLabel(cancel, l10n.Cancel(locale()))
	AdwHeaderBarPackStart(bar, cancel)
	title := AdwWindowTitleNew("", host)
	AdwHeaderBarSetTitleWidget(bar, title)
	AdwToolbarViewAddTopBar(tv, bar)
	AdwToolbarViewSetContent(tv, view)
	AdwDialogSetChild(dialog, tv)
	done := false
	finish := func(to string, err error) {
		if done {
			return
		}
		done = true
		WebKitWebViewStopLoading(view)
		AdwDialogForceClose(dialog)
		ObjectUnref(dialog)
		ObjectUnref(view)
		reply(to, err)
	}
	Connect(cancel, "clicked", func() { finish("", ErrCanceled) })
	Connect(dialog, "closed", func() { finish("", ErrCanceled) })
	Connect(view, "decide-policy", func(d, kind uintptr) bool {
		if kind != WebKitPolicyNavigation && kind != WebKitPolicyNewWindow {
			return false
		}
		u := WebKitNavigationURI(d)
		if !isCallback(u, scheme) {
			return false
		}
		WebKitPolicyDecisionIgnore(d)
		native.Post(func() { finish(u, nil) })
		return true
	})
	Connect(view, "notify::title", func(uintptr) {
		AdwWindowTitleSetTitle(title, GoString(WebKitWebViewGetTitle(view)))
	})
	Connect(view, "notify::uri", func(uintptr) {
		Dev.AdwWindowTitleSetSubtitle(title, hostOf(GoString(WebKitWebViewGetURI(view))))
	})
	AdwDialogPresent(dialog, native.Window())
	WebKitWebViewLoadURI(view, link)
	return func() { finish("", errors.New("canceled")) }
}

// hostOf is a URL's host, as the dialog's subtitle shows it.
func hostOf(u string) string {
	_, rest, ok := strings.Cut(u, "://")
	if !ok {
		return ""
	}
	host, _, _ := strings.Cut(rest, "/")
	if i := strings.LastIndexByte(host, '@'); i >= 0 {
		host = host[i+1:]
	}
	return host
}
