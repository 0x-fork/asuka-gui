//go:build !windows

package purchase

// Linux has no store (macOS runs the package's Swift): available is
// false, and the rest throw notAvailable.

func newStore() (Store, error) { return nil, ErrNotAvailable }
