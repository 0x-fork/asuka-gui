//go:build !(linux && !android) && !windows

package secure

import (
	"context"
	"errors"
)

// Elsewhere (macOS runs the package's Swift) there's no Go keychain.

func supported() error { return errors.ErrUnsupported }

func (keychain) Get(context.Context, string) (*string, error) { return nil, errors.ErrUnsupported }
func (keychain) Set(context.Context, string, string) error    { return errors.ErrUnsupported }
func (keychain) Remove(context.Context, string) error         { return errors.ErrUnsupported }
