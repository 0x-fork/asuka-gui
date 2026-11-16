//go:build !(linux && !android)

package print

import (
	"context"
	"errors"
)

// Elsewhere (macOS runs the package's Swift) there's no Go printing.

func supported() error { return errors.ErrUnsupported }

func (printer) File(context.Context, string, Job) (Outcome, error) {
	return OutcomeCancelled, errors.ErrUnsupported
}
func (printer) Html(context.Context, string, Job) (Outcome, error) {
	return OutcomeCancelled, errors.ErrUnsupported
}
func (printer) Pdf(context.Context, Layout) (string, error) { return "", errors.ErrUnsupported }
