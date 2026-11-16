// asuka/webauth on Windows and Linux (macOS runs the package's Swift): a
// window over the app's with a web view of the platform's, which loads
// the URL; it ends when a page navigates to the callback scheme (the
// navigation is stopped and its URL is the answer), as the user closes
// it (canceled), or as the call is cancelled. Its window and web view
// are the main thread's; the call's goroutine waits for the answer.
package webauth

import (
	"context"
	"errors"
	"net/url"
	"strings"

	"github.com/egoist/asuka/native"
)

type sheet struct{}

func newSheet() (Sheet, error) {
	if err := supported(); err != nil {
		return nil, err
	}
	return sheet{}, nil
}

type answer struct {
	url string
	err error
}

func (sheet) Open(ctx context.Context, link, scheme string, ephemeral bool) (string, error) {
	u, err := url.Parse(link)
	if err != nil || (u.Scheme != "https" && u.Scheme != "http") || u.Host == "" {
		return "", errors.New("webauth: not a web URL: " + link)
	}
	ch := make(chan answer, 1)
	var stop func()
	native.OnMain(func() {
		stop = show(link, u.Host, strings.ToLower(strings.TrimSuffix(scheme, ":")), ephemeral, func(to string, err error) { ch <- answer{to, err} })
	})
	select {
	case a := <-ch:
		return a.url, a.err
	case <-ctx.Done():
		native.Post(stop)
		return "", ctx.Err()
	}
}

// isCallback reports whether a URL is of the scheme ("": none is).
func isCallback(u, scheme string) bool {
	s, _, ok := strings.Cut(u, ":")
	return ok && scheme != "" && strings.ToLower(s) == scheme
}
