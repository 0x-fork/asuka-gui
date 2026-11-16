//go:build !(linux && !android) && !windows

package preview

import "errors"

// Elsewhere (macOS runs the package's Swift) there's no Go viewer.

func supported() error { return errors.ErrUnsupported }

func open(_ string, done func(error)) { done(errors.ErrUnsupported) }
