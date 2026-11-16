//go:build !(linux && !android) && !windows

package camera

import "errors"

// Elsewhere (macOS runs the package's Swift) there's no Go camera.
func newCamera() (Camera, error)           { return nil, errors.ErrUnsupported }
func newView(events ViewEvents) ViewNative { return nil }
