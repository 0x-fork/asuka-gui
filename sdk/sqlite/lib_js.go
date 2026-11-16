//go:build js && wasm

package sqlite

import "errors"

// Go doesn't run in the page anymore: on the web, asuka/sqlite is the
// core's, through the page's SQLite worker (core/src/sqlite/sqlite.c,
// platform/web/src/sqlite-worker.js).
func loadLibrary() (library, error) {
	return nil, errors.New("sqlite: Go's package has no library on the web (asuka/sqlite runs in the core there)")
}
