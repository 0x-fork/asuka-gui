//go:build !windows && !linux

package apphost

import (
	"fmt"
	"os"
	"runtime"
)

func run() {
	// The Mac's host is Swift (platform/macos), an app's own executable.
	fmt.Fprintf(os.Stderr, "asuka-host: no desktop host for %s\n", runtime.GOOS)
	os.Exit(1)
}
