//go:build !windows

package core_test

import (
	"os/exec"
	"syscall"
)

// inGroup starts cmd in a process group of its own, so killGroup ends
// what it starts too.
func inGroup(cmd *exec.Cmd) { cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true} }

func killGroup(cmd *exec.Cmd) { syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL) }
