//go:build !darwin && !linux && !freebsd && !netbsd && !openbsd && !dragonfly && !windows

package main

import "os"

// rawKeys: no keys on this system.
func rawKeys(f *os.File) (restore func(), ok bool) { return nil, false }

// isTerminal: none on this system.
func isTerminal(f *os.File) bool { return false }
