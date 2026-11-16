//go:build !(linux && !android) && !windows

package scan

import "errors"

// Elsewhere (macOS runs the package's Swift) there's no Go reader.
func newReader() (Reader, error) { return nil, errors.ErrUnsupported }
