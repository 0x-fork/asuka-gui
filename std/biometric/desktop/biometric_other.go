package biometric

import (
	"context"
	"errors"
)

// Elsewhere there's no Go authenticator: Linux has none, and macOS runs
// the package's Swift.

func supported() error { return errors.ErrUnsupported }

func (authenticator) Check(context.Context) (Status, error) { return Status{}, errors.ErrUnsupported }

func (authenticator) Authenticate(context.Context, string, string, bool) error {
	return errors.ErrUnsupported
}
