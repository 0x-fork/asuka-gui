// Package apphost is a desktop app's executable (cmd/asuka-host): the
// platform's host (AppKit on macOS, Win32 on Windows, GTK on Linux), which
// runs the app on the C core beside it (libasuka.dylib, asuka.dll,
// libasuka.so: the core with the app compiled into it), or, under asuka
// dev (ASUKA_DEV), the bytecode the CLI sends. It's the same for every
// app, which reads its settings (its name, its ID, its accent) from
// asuka-app.json beside it (internal/boot's ConfigFile).
package apphost

import (
	"fmt"
	"os"

	"github.com/egoist/asuka/internal/boot"
)

// Main runs the app; it returns when the app quits.
func Main() {
	if err := boot.LoadConfig(); err != nil {
		fmt.Fprintln(os.Stderr, "asuka:", boot.ConfigFile+":", err)
	}
	run()
}
