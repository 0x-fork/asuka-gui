//go:build !(linux && !android) && !windows

package shell

import (
	"context"
	"errors"
)

// Elsewhere (macOS runs the package's Swift) there's no Go shell.

func supported() error { return errors.ErrUnsupported }

func (desktop) Reveal(context.Context, string) error { return errors.ErrUnsupported }
func (desktop) Open(context.Context, string) error   { return errors.ErrUnsupported }
func (desktop) Trash(context.Context, string) error  { return errors.ErrUnsupported }
func (desktop) Beep() error                          { return nil }
