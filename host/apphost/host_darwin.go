package apphost

import (
	"runtime"

	"github.com/egoist/asuka/host/darwin"
)

func init() { runtime.LockOSThread() }

func run() { darwin.RunCore() }
