//go:build ignore

// asqlite.go prints the path of std/sqlite's module for the web (its
// SQLite worker's, appbuild.SQLiteWasm: SQLite and asq built to
// WebAssembly with Zig, into the build cache), for the JavaScript tests
// (asqlite.js); with -header, the path of the sqlite3.h it was built
// with (std/sqlite's [c.download.sqlite], fetched into the cache):
//
//	go run platform/web/testing/asqlite.go [-header]
package main

import (
	"fmt"
	"log"
	"os"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/internal/pkgs"
	"github.com/egoist/asuka/std"
)

func main() {
	log.SetFlags(0)
	if len(os.Args) > 1 && os.Args[1] == "-header" {
		dir, ok := std.Package("std/sqlite")
		if !ok {
			log.Fatal("no std/sqlite")
		}
		cfg, err := pkgs.ReadC(dir)
		if err != nil {
			log.Fatal(err)
		}
		p, d, err := cfg.Path(dir, "$sqlite/sqlite3.h")
		if err != nil {
			log.Fatal(err)
		}
		if err := d.Fetch(); err != nil {
			log.Fatal(err)
		}
		fmt.Println(p)
		return
	}
	p, err := appbuild.SQLiteWasm(false)
	if err != nil {
		log.Fatal(err)
	}
	fmt.Println(p)
}
