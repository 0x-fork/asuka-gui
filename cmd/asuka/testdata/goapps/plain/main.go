// An app in Go that imports neither push nor iCloud.
package main

import (
	"github.com/egoist/asuka/app"
	"github.com/egoist/asuka/ui"
)

func main() { app.Run(func() ui.Node { return ui.StaticText("Plain") }) }
