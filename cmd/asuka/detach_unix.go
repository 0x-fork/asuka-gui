//go:build darwin || linux || freebsd || netbsd || openbsd || dragonfly

package main

import (
	"os/exec"
	"syscall"
)

// detach makes cmd a session of its own: the terminal's Ctrl-C and
// hang-up aren't its.
func detach(cmd *exec.Cmd) { cmd.SysProcAttr = &syscall.SysProcAttr{Setsid: true} }
