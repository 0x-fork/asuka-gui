//go:build windows

package main

import (
	"os"

	"golang.org/x/sys/windows"
)

// rawKeys turns f's console's line input and echo off, keeping Ctrl-C's
// processing, and says how to turn them back on; false when f isn't a
// console.
func rawKeys(f *os.File) (restore func(), ok bool) {
	h := windows.Handle(f.Fd())
	var old uint32
	if windows.GetConsoleMode(h, &old) != nil {
		return nil, false
	}
	if windows.SetConsoleMode(h, old&^(windows.ENABLE_LINE_INPUT|windows.ENABLE_ECHO_INPUT)) != nil {
		return nil, false
	}
	return func() { windows.SetConsoleMode(h, old) }, true
}

// isTerminal reports whether f is a console.
func isTerminal(f *os.File) bool {
	var mode uint32
	return windows.GetConsoleMode(windows.Handle(f.Fd()), &mode) == nil
}
