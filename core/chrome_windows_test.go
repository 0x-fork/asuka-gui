package core_test

import (
	"os/exec"
	"strconv"
)

func inGroup(cmd *exec.Cmd) {}

// killGroup ends cmd's process and what it started (taskkill /t).
func killGroup(cmd *exec.Cmd) {
	exec.Command("taskkill", "/f", "/t", "/pid", strconv.Itoa(cmd.Process.Pid)).Run()
}
