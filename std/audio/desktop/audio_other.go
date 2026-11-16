//go:build !(linux && !android)

package audio

import "errors"

// Elsewhere (macOS runs the package's Swift) there's no Go player.
func newSound() (Sound, error) { return nil, errors.ErrUnsupported }
