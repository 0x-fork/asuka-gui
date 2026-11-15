//go:build ios

package fswatch

// newPlatform starts kqueue: iOS has no FSEvents for apps.
func newPlatform(notify func(notice), opts Options) (native, error) {
	return newKqueue(notify)
}
