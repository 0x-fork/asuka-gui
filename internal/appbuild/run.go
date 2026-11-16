package appbuild

import (
	"bufio"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
	"sync"

	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/compile"
)

var (
	runnerOnce sync.Once
	runnerPath string
	runnerErr  error
)

// Runner is asuka-run (core/runner: the VM, the core and the fake host)
// built for this computer, once per version of the core.
func Runner() (string, error) {
	runnerOnce.Do(func() {
		core, err := cbuild.CoreDir()
		if err != nil {
			runnerErr = err
			return
		}
		runnerPath, runnerErr = cbuild.Runner(core, false)
	})
	return runnerPath, runnerErr
}

// RunnerFor is asuka-run for m: Runner, or for a module with C (its
// externs'), one built with that C, which the module's ffi.* builtins
// are in, linked in dir (the caller's, which it removes).
func RunnerFor(m *bytecode.Module, dir string) (string, error) {
	if m == nil || m.C == nil {
		return Runner()
	}
	core, err := cbuild.CoreDir()
	if err != nil {
		return "", err
	}
	f, err := Foreign(m.C, cbuild.Host())
	if err != nil {
		return "", err
	}
	return cbuild.RunnerFor(core, cbuild.RunnerOptions{Foreign: f, Dir: dir})
}

// TestResult is how a test block went.
type TestResult struct {
	Name    string
	OK      bool
	Message string // what failed
	Pos     string // file:line of the test block
}

// TestSummary is a run of a module's tests.
type TestSummary struct {
	Results []TestResult
}

// Failed is how many tests failed.
func (s *TestSummary) Failed() int {
	n := 0
	for _, r := range s.Results {
		if !r.OK {
			n++
		}
	}
	return n
}

// TestOptions say which tests run, and how.
type TestOptions struct {
	// Pattern picks the tests whose names contain it ("": all).
	Pattern string
	// Platform is what the fake host says it is: ios (the default),
	// android, macos, windows, web, linux.
	Platform string
	// Dir, when set, is what the files reported are relative to.
	Dir string
}

// RunTests runs m's test blocks on the fake host, with asuka-run, writing
// each result to out as it comes (what the tests print too): "ok   name",
// or "FAIL name (file:line)" and the message, indented.
func RunTests(m *bytecode.Module, o TestOptions, out io.Writer) (*TestSummary, error) {
	pattern, dir := o.Pattern, o.Dir
	work, err := os.MkdirTemp("", "asuka-test-")
	if err != nil {
		return nil, err
	}
	defer os.RemoveAll(work)
	exe, err := RunnerFor(m, work)
	if err != nil {
		return nil, err
	}
	p, cleanup, err := writeModule(m)
	if err != nil {
		return nil, err
	}
	defer cleanup()
	var args []string
	if o.Platform != "" {
		args = append(args, "-platform", o.Platform)
	}
	args = append(args, "-test")
	if pattern != "" {
		args = append(args, pattern)
	}
	cmd := exec.Command(exe, append(args, p)...)
	// The plugins' libraries the program calls (std/sqlite's), as the core
	// finds them: ASUKA_LIB_<NAME>.
	cmd.Env = append(os.Environ(), compile.PluginEnv(m)...)
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, err
	}
	var stderr strings.Builder
	cmd.Stderr = &stderr
	if err := cmd.Start(); err != nil {
		return nil, err
	}
	names := make([]string, len(m.Tests))
	for i, t := range m.Tests {
		names[i] = t.Name
	}
	// Longest first: a name that starts another's is matched last.
	sort.Slice(names, func(i, j int) bool { return len(names[i]) > len(names[j]) })
	s := &TestSummary{}
	sc := bufio.NewScanner(stdout)
	sc.Buffer(make([]byte, 64<<10), 16<<20)
	for sc.Scan() {
		line := sc.Text()
		r, ok := parseResult(line, names)
		if !ok {
			fmt.Fprintln(out, line)
			continue
		}
		r.Pos = testPos(m, r.Name, dir)
		s.Results = append(s.Results, r)
		if r.OK {
			fmt.Fprintf(out, "ok   %s\n", r.Name)
			continue
		}
		fmt.Fprintf(out, "FAIL %s (%s)\n", r.Name, r.Pos)
		for _, l := range strings.Split(r.Message, "\n") {
			fmt.Fprintf(out, "     %s\n", l)
		}
	}
	err = cmd.Wait()
	if e := strings.TrimSpace(stderr.String()); e != "" {
		fmt.Fprintln(out, e)
	}
	var exit *exec.ExitError
	if err != nil && !(errors.As(err, &exit) && exit.ExitCode() == 1 && s.Failed() > 0) {
		return s, fmt.Errorf("asuka-run: %v", err)
	}
	return s, nil
}

func parseResult(line string, names []string) (TestResult, bool) {
	if n, ok := strings.CutPrefix(line, "ok "); ok {
		for _, name := range names {
			if n == name {
				return TestResult{Name: name, OK: true}, true
			}
		}
	}
	if rest, ok := strings.CutPrefix(line, "FAIL "); ok {
		for _, name := range names {
			if msg, ok := strings.CutPrefix(rest, name+": "); ok {
				return TestResult{Name: name, Message: msg}, true
			}
		}
	}
	return TestResult{}, false
}

// testPos is where the test named name is declared: its function's file
// and first line.
func testPos(m *bytecode.Module, name, dir string) string {
	for _, t := range m.Tests {
		if t.Name != name || int(t.Func) >= len(m.Funcs) {
			continue
		}
		f := m.Funcs[t.Func]
		file := f.File
		if dir != "" {
			if rel, err := filepath.Rel(dir, file); err == nil && !strings.HasPrefix(rel, "..") {
				file = rel
			}
		}
		if len(f.Lines) > 0 {
			return fmt.Sprintf("%s:%d", file, f.Lines[0].Line)
		}
		return file
	}
	return "?"
}

// ShowApp mounts m's app on the fake host (an iPhone's screen), lets it
// settle and writes what the screen shows to out.
func ShowApp(m *bytecode.Module, out io.Writer) error {
	work, err := os.MkdirTemp("", "asuka-run-")
	if err != nil {
		return err
	}
	defer os.RemoveAll(work)
	exe, err := RunnerFor(m, work)
	if err != nil {
		return err
	}
	p, cleanup, err := writeModule(m)
	if err != nil {
		return err
	}
	defer cleanup()
	cmd := exec.Command(exe, "-app", p)
	cmd.Env = append(os.Environ(), compile.PluginEnv(m)...)
	cmd.Stdout, cmd.Stderr = out, out
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("asuka-run: %v", err)
	}
	return nil
}

func writeModule(m *bytecode.Module) (string, func(), error) {
	f, err := os.CreateTemp("", "asuka-*.akb")
	if err != nil {
		return "", nil, err
	}
	if _, err := f.Write(m.Encode()); err != nil {
		f.Close()
		os.Remove(f.Name())
		return "", nil, err
	}
	f.Close()
	return f.Name(), func() { os.Remove(f.Name()) }, nil
}
