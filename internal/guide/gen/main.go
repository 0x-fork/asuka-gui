// Command gen writes asuka guide's pages into the CLI, from the docs and
// lang/diag's table (go generate ./cmd/asuka): cmd/asuka/guide/<lang>.md.
package main

import (
	"log"
	"os"
	"path/filepath"

	"github.com/egoist/asuka/internal/guide"
	"github.com/egoist/asuka/lang/diag"
)

func main() {
	root := filepath.Join("..", "..") // go generate runs in cmd/asuka
	if err := os.MkdirAll("guide", 0o755); err != nil {
		log.Fatal(err)
	}
	for _, lang := range diag.Languages {
		text, err := guide.Build(root, lang)
		if err != nil {
			log.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join("guide", lang+".md"), []byte(text), 0o644); err != nil {
			log.Fatal(err)
		}
	}
}
