//go:build !darwin && !linux && !windows

package fswatch

// newPlatform: no file watching here.
func newPlatform(notify func(notice), opts Options) (native, error) {
	return nil, ErrUnsupported
}
