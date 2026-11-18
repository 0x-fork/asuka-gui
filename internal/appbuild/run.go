package appbuild

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/diag"
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
// externs', which the module's ffi.* builtins are in; the std packages'
// it imports: std/sqlite's SQLite), one built with that C, linked in dir
// (the caller's, which it removes).
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
	Name    string `json:"test"`
	OK      bool   `json:"ok"`
	Message string `json:"message,omitempty"` // what failed
	Pos     string `json:"pos"`               // file:line of the test block
	// Property is a property test's failing input (testing.go).
	Property *PropertyFailure `json:"property,omitempty"`
}

// WriteJSON writes v to out as a line of JSON (asuka test --json's).
func WriteJSON(out io.Writer, v any) { writeJSON(out, v) }

// writeJSON writes v to out as a line of JSON.
func writeJSON(out io.Writer, v any) {
	b, err := json.Marshal(v)
	if err != nil {
		panic(err)
	}
	fmt.Fprintf(out, "%s\n", b)
}

// TestSummary is a run of a module's tests.
type TestSummary struct {
	Results []TestResult
	// Diags are the run-time diagnostics the core reported as the tests
	// ran (rfcs/0015: load-waterfall, long-hold...), in order.
	Diags []RunDiag
	// Benches are the benches' results (with Bench), in order.
	Benches []BenchResult
	// Snapshots are what the snapshots saw, in order.
	Snapshots []Snapshot
	// Fuzz is the fuzzing's progress (with Fuzz).
	Fuzz []FuzzProgress
	// properties: the failing inputs asuka-run said, until their tests'
	// results come.
	properties map[string]*PropertyFailure
}

// A RunDiag is a diagnostic the core reported as the app ran: its code,
// its message, and the places in the source it concerns ("Profile's load
// of user at app.asuka:8"), in the test it ran in.
type RunDiag struct {
	Code    string   `json:"diag"`
	Message string   `json:"message"`
	Where   []string `json:"where"`
	Test    string   `json:"test,omitempty"`
}

// relative is d with dir's files said relative to it.
func (d RunDiag) relative(dir string) RunDiag {
	if dir == "" {
		return d
	}
	prefix := dir + string(filepath.Separator)
	d.Message = strings.ReplaceAll(d.Message, prefix, "")
	where := make([]string, len(d.Where))
	for i, w := range d.Where {
		where[i] = strings.ReplaceAll(w, prefix, "")
	}
	d.Where = where
	return d
}

// String is d as a terminal shows it (base.h's ak_diag_format).
func (d RunDiag) String() string {
	var b strings.Builder
	fmt.Fprintf(&b, "warning[%s]: %s", d.Code, d.Message)
	for _, w := range d.Where {
		fmt.Fprintf(&b, "\n  --> %s", w)
	}
	fmt.Fprintf(&b, "\n  = see %s", diag.URL(d.Code))
	return b.String()
}

// whereAt is the file and line a site's label ends with: "… at app.asuka:8".
var whereAt = regexp.MustCompile(` at (.+):(\d+)$`)

// Record is d as asuka test --json writes it: a diagnostic's record, its
// file and line its first place's, with where and the test.
func (d RunDiag) Record() map[string]any {
	r := map[string]any{"code": d.Code, "severity": "warning", "message": d.Message, "url": diag.URL(d.Code), "where": d.Where}
	if len(d.Where) > 0 {
		if m := whereAt.FindStringSubmatch(d.Where[0]); m != nil {
			line, _ := strconv.Atoi(m[2])
			at := map[string]int{"line": line, "column": 1}
			r["file"] = m[1]
			r["range"] = map[string]any{"start": at, "end": at}
		}
	}
	if d.Test != "" {
		r["test"] = d.Test
	}
	return r
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
	// JSON writes each result to out as a JSON object on a line of its
	// own ({"test", "ok", "message", "pos"}), and what the tests print as
	// {"output"}.
	JSON bool

	// Bench runs the benches instead of the tests: compiled to C, as a
	// release build is, unless VM. Each's samples take BenchTime
	// together, or each sample is BenchN iterations. Baseline: the
	// results of a run to compare with (by name).
	Bench     bool
	VM        bool
	BenchTime time.Duration
	BenchN    uint64
	Baseline  map[string]BenchResult
	// Seed and Cases are the property tests' (Cases 0: 100). Fuzz fuzzes
	// them instead, for FuzzTime or FuzzCases each. Corpus is a file of
	// "test\tpath" lines: the inputs each test runs first.
	Seed      uint64
	Cases     int
	Fuzz      bool
	FuzzTime  time.Duration
	FuzzCases uint64
	Corpus    string
	// Snapshots is a file of the stored snapshots ("key\0value\0"
	// records); Update takes what differs; CI fails on a missing one.
	Snapshots  string
	Update, CI bool
	// Cover is the file asuka-run writes the coverage counters' counts
	// to, for a module compiled with them.
	Cover string
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
	aot := o.Bench && !o.VM
	var exe, p string
	if aot {
		if exe, err = cRunner(m, work); err != nil {
			return nil, err
		}
		p = "-aot"
	} else {
		if exe, err = RunnerFor(m, work); err != nil {
			return nil, err
		}
		var cleanup func()
		if p, cleanup, err = writeModule(m); err != nil {
			return nil, err
		}
		defer cleanup()
	}
	var args []string
	if o.Platform != "" {
		args = append(args, "-platform", o.Platform)
	}
	args = append(args, testFlags(o)...)
	args = append(args, "-diag-json", "-test")
	if pattern != "" {
		args = append(args, pattern)
	}
	cmd := exec.Command(exe, append(args, p)...)
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
	width := benchWidth(m)
	sc := bufio.NewScanner(stdout)
	sc.Buffer(make([]byte, 64<<10), 64<<20)
	for sc.Scan() {
		line := sc.Text()
		if strings.HasPrefix(line, "{") && s.testLine(line, o, m, out, width) {
			continue
		}
		if strings.HasPrefix(line, `{"diag":`) {
			var d RunDiag
			if err := json.Unmarshal([]byte(line), &d); err == nil {
				d = d.relative(dir)
				s.Diags = append(s.Diags, d)
				if o.JSON {
					writeJSON(out, d.Record())
				} else {
					fmt.Fprintln(out, d.String())
				}
				continue
			}
		}
		r, ok := parseResult(line, names)
		if !ok {
			if o.JSON {
				writeJSON(out, map[string]string{"output": line})
			} else {
				fmt.Fprintln(out, line)
			}
			continue
		}
		r.Pos = testPos(m, r.Name, dir)
		if !r.OK {
			s.failed(&r)
		}
		s.Results = append(s.Results, r)
		if o.JSON {
			writeJSON(out, r)
			continue
		}
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
		if o.JSON {
			for _, l := range strings.Split(e, "\n") {
				writeJSON(out, map[string]string{"output": l})
			}
		} else {
			fmt.Fprintln(out, e)
		}
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

// testFlags are asuka-run's flags for what o asks of the tests.
func testFlags(o TestOptions) []string {
	var args []string
	if o.Bench {
		args = append(args, "-bench", "-no-diag")
		if o.BenchN > 0 {
			args = append(args, "-benchn", strconv.FormatUint(o.BenchN, 10))
		} else if o.BenchTime > 0 {
			args = append(args, "-benchtime", strconv.FormatInt(int64(o.BenchTime), 10))
		}
	}
	if o.Seed != 0 {
		args = append(args, "-seed", strconv.FormatUint(o.Seed, 10))
	}
	if o.Cases > 0 {
		args = append(args, "-cases", strconv.Itoa(o.Cases))
	}
	if o.Fuzz {
		args = append(args, "-fuzz")
		if o.FuzzCases > 0 {
			args = append(args, "-fuzzcases", strconv.FormatUint(o.FuzzCases, 10))
		} else if o.FuzzTime > 0 {
			args = append(args, "-fuzztime", strconv.FormatInt(int64(o.FuzzTime), 10))
		}
	}
	if o.Corpus != "" {
		args = append(args, "-corpus", o.Corpus)
	}
	if o.Snapshots != "" {
		args = append(args, "-snapshots", o.Snapshots)
	}
	if o.Update {
		args = append(args, "-update")
	}
	if o.CI {
		args = append(args, "-ci")
	}
	if o.Cover != "" {
		args = append(args, "-cover", o.Cover)
	}
	return args
}
