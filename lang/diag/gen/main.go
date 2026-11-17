// Command gen writes the error codes' pages from lang/diag's table:
// docs/errors/<code>.md and its index, and docs/zh/errors and
// docs/ja/errors likewise (go generate ./lang/diag). A page of a code the
// table no longer has is removed.
package main

import (
	"log"
	"os"
	"path/filepath"

	"github.com/egoist/asuka/lang/diag"
)

func main() {
	root := filepath.Join("..", "..")
	pages := diag.Pages()
	for _, dir := range []string{"docs/errors", "docs/zh/errors", "docs/ja/errors"} {
		if err := os.MkdirAll(filepath.Join(root, dir), 0o755); err != nil {
			log.Fatal(err)
		}
		old, _ := filepath.Glob(filepath.Join(root, dir, "*.md"))
		for _, f := range old {
			rel, _ := filepath.Rel(root, f)
			if _, ok := pages[filepath.ToSlash(rel)]; !ok {
				os.Remove(f)
			}
		}
	}
	for name, text := range pages {
		if err := os.WriteFile(filepath.Join(root, filepath.FromSlash(name)), []byte(text), 0o644); err != nil {
			log.Fatal(err)
		}
	}
}
