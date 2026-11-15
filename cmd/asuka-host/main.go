// Command asuka-host is a desktop app's executable: the platform's host
// (AppKit on macOS, Win32 on Windows, GTK on Linux), which runs the app
// on the C core beside it (libasuka.dylib, asuka.dll, libasuka.so: the
// core with the app compiled into it), or, under asuka dev (ASUKA_DEV),
// the bytecode the CLI sends. asuka build copies it into the app as the
// app's own executable; it's the same for every app, which reads its
// settings (its name, its ID, its accent) from asuka-app.json beside it
// (internal/boot's ConfigFile).
package main

import (
	"fmt"
	"os"

	"github.com/egoist/asuka/internal/boot"
)

func main() {
	if err := boot.LoadConfig(); err != nil {
		fmt.Fprintln(os.Stderr, "asuka:", boot.ConfigFile+":", err)
	}
	run()
}
