package apphost

import (
	"runtime"

	"github.com/egoist/asuka/host/linux"
)

func init() { runtime.LockOSThread() }

func run() { linux.RunCore() }
