//go:build !(linux && !android)

package js

import "errors"

// Elsewhere (macOS runs the package's Swift) there's no Go engine.

func supported() error { return errors.ErrUnsupported }

func start(string, func([]byte, error)) func() { return func() {} }
