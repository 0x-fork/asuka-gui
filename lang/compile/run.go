package compile

import (
	"bytes"
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"

	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/lang/bytecode"
)

// FindCore finds the C core's directory (core/, with core/runner): from
// ASUKA_CORE, beside this source file (go run, go test), or up from the
// working directory.
func FindCore() (string, error) {
	ok := func(d string) bool {
		_, err := os.Stat(filepath.Join(d, "runner", "main.c"))
		return err == nil
	}
	if d := os.Getenv("ASUKA_CORE"); d != "" {
		if ok(d) {
			return d, nil
		}
		return "", errors.New("ASUKA_CORE (" + d + ") isn't the core's directory: it has no runner/main.c")
	}
	if _, file, _, found := runtime.Caller(0); found {
		d := filepath.Join(filepath.Dir(file), "..", "..", "core")
		if ok(d) {
			return filepath.Clean(d), nil
		}
	}
	wd, _ := os.Getwd()
	for d := wd; ; {
		if ok(filepath.Join(d, "core")) {
			return filepath.Join(d, "core"), nil
		}
		parent := filepath.Dir(d)
		if parent == d {
			break
		}
		d = parent
	}
	return "", errors.New("can't find the C core (core/runner/main.c): set ASUKA_CORE to its directory")
}

// Runner builds (once, cached) the VM runner, asuka-run.
func Runner(sanitize bool) (string, error) {
	core, err := FindCore()
	if err != nil {
		return "", err
	}
	return cbuild.Runner(core, sanitize)
}

// Run runs a module on the VM with runner: its tests matching pattern
// with test set (pattern "" for all), else its app. It returns what the
// runner printed, and an error when it failed (a test failed, or the
// module didn't load).
func Run(runner string, m *bytecode.Module, test bool, pattern string) (string, error) {
	dir, err := os.MkdirTemp("", "asuka-run")
	if err != nil {
		return "", err
	}
	defer os.RemoveAll(dir)
	p := filepath.Join(dir, "module.akb")
	if err := os.WriteFile(p, m.Encode(), 0o644); err != nil {
		return "", err
	}
	var args []string
	if test {
		args = append(args, "-test")
		if pattern != "" {
			args = append(args, pattern)
		}
	}
	args = append(args, p)
	cmd := exec.Command(runner, args...)
	var out bytes.Buffer
	cmd.Stdout, cmd.Stderr = &out, &out
	err = cmd.Run()
	return out.String(), err
}
