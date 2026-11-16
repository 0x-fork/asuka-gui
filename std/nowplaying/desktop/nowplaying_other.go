//go:build !(linux && !android)

package nowplaying

import "errors"

// Elsewhere (macOS runs the package's Swift) there are no Go controls.
func newCenter() (Center, error) { return nil, errors.ErrUnsupported }
