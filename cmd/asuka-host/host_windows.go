package main

import (
	"runtime"

	"github.com/egoist/asuka/host/windows"
)

func init() { runtime.LockOSThread() }

func run() { windows.RunCore() }
