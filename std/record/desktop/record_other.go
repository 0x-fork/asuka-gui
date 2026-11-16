//go:build !(linux && !android) && !windows

package record

import "errors"

// Elsewhere (macOS runs the package's Swift) there's no Go recorder.
func newRecorder() (Recorder, error) { return nil, errors.ErrUnsupported }
