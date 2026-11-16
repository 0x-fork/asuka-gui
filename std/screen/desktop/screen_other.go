//go:build !(linux && !android) && !windows

package screen

import "errors"

// Elsewhere (macOS runs the package's Swift) there's no Go screen.

func supported() error { return errors.ErrUnsupported }

func read() []Display { return []Display{} }

func follow(func()) func() { return func() {} }

func preventCapture(bool) {}
