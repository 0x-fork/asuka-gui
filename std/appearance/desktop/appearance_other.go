//go:build !(linux && !android)

package appearance

import "errors"

// Elsewhere (macOS runs the package's Swift) there's no Go appearance.

func supported() error { return errors.ErrUnsupported }

func settings() Settings { return Settings{} }

func follow(func()) func() { return func() {} }
