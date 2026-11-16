// asuka/sqlite on Windows and Linux: where a database's relative name
// is, the app's data directory (internal/appdir). SQLite itself is the
// plugin's library, which the core calls.
package sqlite

import (
	"os"

	"github.com/egoist/asuka/internal/appdir"
)

type place struct{}

func newPlace() (Place, error) { return place{}, nil }

func (place) DataDir() (string, error) {
	d := appdir.Data()
	return d, os.MkdirAll(d, 0o755)
}
