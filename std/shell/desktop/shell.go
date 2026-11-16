// asuka/shell on Windows and Linux (macOS runs the package's Swift).
package shell

import (
	"fmt"
	"os"
)

type desktop struct{}

func newDesktop() (Desktop, error) {
	if err := supported(); err != nil {
		return nil, err
	}
	return desktop{}, nil
}

// there is an error unless something is at path (a link counts, for
// reveal and trash).
func there(path string, link bool) error {
	stat := os.Stat
	if link {
		stat = os.Lstat
	}
	if _, err := stat(path); err != nil {
		return fmt.Errorf("no file at %s", path)
	}
	return nil
}
