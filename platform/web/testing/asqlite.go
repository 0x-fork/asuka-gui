//go:build ignore

// asqlite.go prints the path of std/sqlite's module for the web (its
// SQLite worker's, appbuild.SQLiteWasm: SQLite and asq built to
// WebAssembly with Zig, into the build cache), for the JavaScript tests
// (asqlite.js):
//
//	go run platform/web/testing/asqlite.go
package main

import (
	"fmt"
	"log"

	"github.com/egoist/asuka/internal/appbuild"
)

func main() {
	log.SetFlags(0)
	p, err := appbuild.SQLiteWasm(false)
	if err != nil {
		log.Fatal(err)
	}
	fmt.Println(p)
}
