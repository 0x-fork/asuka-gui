//go:build !(linux && !android) && !windows

package network

import "errors"

// Elsewhere (macOS runs the package's Swift) there's no Go monitor.

func supported() error { return errors.ErrUnsupported }

func state() (Status, error) { return Status{}, errors.ErrUnsupported }

func follow(func()) (func(), error) { return nil, errors.ErrUnsupported }
