//go:build !(linux && !android) && !windows

package power

import (
	"context"
	"errors"
	"time"
)

// Elsewhere (macOS runs the package's Swift) there's no Go power.

func supported() error { return errors.ErrUnsupported }

func state() Status { return unknown }

func follow(func(), func(Event)) func() { return func() {} }

func (monitor) Idle(context.Context) (time.Duration, error) { return 0, errors.ErrUnsupported }
