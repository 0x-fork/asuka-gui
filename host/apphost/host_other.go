//go:build !darwin && !linux

package apphost

import (
	"fmt"
	"os"
	"runtime"
)

func run() {
	fmt.Fprintf(os.Stderr, "asuka-host: no desktop host for %s\n", runtime.GOOS)
	os.Exit(1)
}
