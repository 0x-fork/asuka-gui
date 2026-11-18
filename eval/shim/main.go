// Command shim is the asuka an agent runs in eval: it runs the real CLI
// (ASUKA_EVAL_REAL) with its arguments, its input and output as they
// are, and logs the call (ASUKA_EVAL_LOG, a JSON line: its arguments,
// its exit code, the diagnostic codes its output said).
package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"io"
	"os"
	"os/exec"
	"regexp"
)

var codeRe = regexp.MustCompile(`(?:error|warning)\[([a-z]+(?:-[a-z]+)*)\]|"code":"([a-z]+(?:-[a-z]+)*)"`)

func main() {
	real := os.Getenv("ASUKA_EVAL_REAL")
	if real == "" {
		os.Stderr.WriteString("asuka (eval's shim): ASUKA_EVAL_REAL isn't set\n")
		os.Exit(2)
	}
	var seen bytes.Buffer
	cmd := exec.Command(real, os.Args[1:]...)
	cmd.Stdin = os.Stdin
	cmd.Stdout = io.MultiWriter(os.Stdout, &seen)
	cmd.Stderr = io.MultiWriter(os.Stderr, &seen)
	code := 0
	if err := cmd.Run(); err != nil {
		var exit *exec.ExitError
		if !errors.As(err, &exit) {
			os.Stderr.WriteString("asuka: " + err.Error() + "\n")
			os.Exit(1)
		}
		code = exit.ExitCode()
	}
	var codes []string
	for _, m := range codeRe.FindAllStringSubmatch(seen.String(), -1) {
		if m[1] != "" {
			codes = append(codes, m[1])
		} else {
			codes = append(codes, m[2])
		}
	}
	if log := os.Getenv("ASUKA_EVAL_LOG"); log != "" {
		b, _ := json.Marshal(map[string]any{"args": os.Args[1:], "exit": code, "codes": codes})
		if f, err := os.OpenFile(log, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o644); err == nil {
			f.Write(append(b, '\n'))
			f.Close()
		}
	}
	os.Exit(code)
}
