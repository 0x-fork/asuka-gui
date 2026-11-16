//go:build !(linux && !android)

package dialog

import "errors"

// Elsewhere (macOS runs the package's Swift) there are no Go dialogs.

func supported() error { return errors.ErrUnsupported }

func fileDialog(_ string, _ Request, done func(chosen)) { done(chosen{err: errors.ErrUnsupported}) }
