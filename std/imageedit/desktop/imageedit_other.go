//go:build !(linux && !android) && !windows

package imageedit

import (
	"errors"
	"time"
)

// Elsewhere (macOS runs the package's Swift) there are no Go codecs.
func supported() error { return errors.ErrUnsupported }

func run(method, source string, steps []Step, format Format, quality float64, at time.Duration) ([]byte, error) {
	return nil, errors.ErrUnsupported
}
