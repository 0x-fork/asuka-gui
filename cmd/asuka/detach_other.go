//go:build !darwin && !linux && !freebsd && !netbsd && !openbsd && !dragonfly && !windows

package main

import "os/exec"

func detach(cmd *exec.Cmd) {}
