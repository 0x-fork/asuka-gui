//go:build !(linux && !android)

package webauth

import "errors"

// Elsewhere (macOS runs the package's Swift) there's no Go sign-in.

func supported() error { return errors.ErrUnsupported }

func show(_, _, _ string, _ bool, reply func(string, error)) func() {
	reply("", errors.ErrUnsupported)
	return func() {}
}
