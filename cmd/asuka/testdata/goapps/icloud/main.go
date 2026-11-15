// An app in Go that uses both parts of iCloud: its key-value store
// (icloud.Signal) and its documents (icloud.WriteFile), as
// examples/journal did (icloud_test.go).
package main

import (
	"github.com/egoist/asuka/app"
	"github.com/egoist/asuka/sdk/icloud"
	"github.com/egoist/asuka/ui"
)

func main() {
	size := icloud.Signal("textSize", 17.0)
	app.Run(func() ui.Node {
		go icloud.WriteFile("journal.txt", nil)
		_ = size
		return ui.StaticText("Journal")
	})
}
