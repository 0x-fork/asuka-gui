// An app in Go that rewrites its pushes (push.Rewrite): what the player's
// gaps look for (player_gaps_test.go).
package main

import (
	"github.com/egoist/asuka/app"
	"github.com/egoist/asuka/sdk/push"
	"github.com/egoist/asuka/ui"
)

func main() {
	push.Rewrite(func(m *push.Message) error { return nil })
	app.Run(func() ui.Node { return ui.StaticText("Pushes") })
}
