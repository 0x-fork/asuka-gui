// Command eval measures whether coding agents can write Asuka from what
// a project tells them (rfcs/0015): each task in eval/tasks is a prompt
// and an asuka test file that checks the app's behavior on the fake
// host. For each, it makes a fresh project with asuka new (its AGENTS.md
// is all the agent is told of the language), runs the agent there with
// the prompt on its standard input, and the CLI on its PATH, then runs
// the task's tests against what the agent wrote. It records whether they
// pass, how many compiler round trips the agent made (its asuka check,
// test, run and build), and which diagnostic codes it hit, and which of
// them its code still has.
//
// It needs a model and costs money: run it by hand, not in CI.
//
//	go run ./eval -agent 'claude -p --model claude-haiku-5-5 --allowedTools "Bash(asuka:*)" Read Write Edit Glob Grep'
//	go run ./eval -agent '…' -tasks counter,todos -timeout 10m
package main

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"time"

	"github.com/egoist/asuka/lang/diag"
)

// Result is how a task went.
type Result struct {
	Task string `json:"task"`
	// Passed: the task's tests all passed against what the agent wrote.
	Passed bool `json:"passed"`
	// Tests and Failed count the task's tests ("eval: …").
	Tests  int `json:"tests"`
	Failed int `json:"failed"`
	// RoundTrips counts the agent's compiles: asuka check, test, run, build.
	RoundTrips int `json:"roundTrips"`
	// Codes are the diagnostic codes the agent's compiles reported, with
	// how many times; Left, those asuka check reports of its code at the
	// end.
	Codes map[string]int `json:"codes"`
	Left  []string       `json:"left,omitempty"`
	// Seconds is how long the agent took; TimedOut, whether it ran out.
	Seconds  float64 `json:"seconds"`
	TimedOut bool    `json:"timedOut,omitempty"`
	Error    string  `json:"error,omitempty"`
}

func main() {
	agent := flag.String("agent", "", "the agent's `command`, run by the shell in the project, the prompt on its standard input")
	only := flag.String("tasks", "", "the `tasks` to run, comma-separated (default: every one)")
	timeout := flag.Duration("timeout", 15*time.Minute, "how long the agent may take on a task")
	out := flag.String("out", "", "the `directory` the projects and the results go in (default: a new temporary one)")
	report := flag.String("report", "", "don't run the agent: read the results in `directory` again (a run's -out), recount from its logs, and print the table")
	flag.Parse()
	if *report != "" {
		if err := reread(*report); err != nil {
			fail(err)
		}
		return
	}
	if *agent == "" {
		fmt.Fprintln(os.Stderr, "eval: -agent: the agent's command")
		os.Exit(2)
	}
	root, err := repoRoot()
	if err != nil {
		fail(err)
	}
	work := *out
	if work == "" {
		if work, err = os.MkdirTemp("", "asuka-eval-"); err != nil {
			fail(err)
		}
	}
	work, _ = filepath.Abs(work)
	bin := filepath.Join(work, "bin")
	real, err := build(root, bin)
	if err != nil {
		fail(err)
	}
	tasks, err := tasksIn(filepath.Join(root, "eval", "tasks"), *only)
	if err != nil {
		fail(err)
	}
	var results []Result
	for _, t := range tasks {
		fmt.Fprintf(os.Stderr, "eval: %s…\n", t)
		r := runTask(root, work, bin, real, t, *agent, *timeout)
		results = append(results, r)
		fmt.Fprintf(os.Stderr, "eval: %s: passed %v, %d round trips, codes %v\n", t, r.Passed, r.RoundTrips, r.Codes)
	}
	b, _ := json.MarshalIndent(results, "", "  ")
	if err := os.WriteFile(filepath.Join(work, "results.json"), b, 0o644); err != nil {
		fail(err)
	}
	fmt.Print(table(results))
	fmt.Fprintf(os.Stderr, "eval: the projects and results.json are in %s\n", work)
}

// reread recounts a run's results from its logs, and prints the table.
func reread(dir string) error {
	b, err := os.ReadFile(filepath.Join(dir, "results.json"))
	if err != nil {
		return err
	}
	var rs []Result
	if err := json.Unmarshal(b, &rs); err != nil {
		return err
	}
	for i := range rs {
		rs[i].RoundTrips, rs[i].Codes = 0, map[string]int{}
		calls(filepath.Join(dir, rs[i].Task+".calls.jsonl"), &rs[i])
	}
	b, _ = json.MarshalIndent(rs, "", "  ")
	if err := os.WriteFile(filepath.Join(dir, "results.json"), b, 0o644); err != nil {
		return err
	}
	fmt.Print(table(rs))
	return nil
}

func fail(err error) {
	fmt.Fprintln(os.Stderr, "eval:", err)
	os.Exit(1)
}

func repoRoot() (string, error) {
	_, file, _, _ := runtime.Caller(0)
	root := filepath.Dir(filepath.Dir(file))
	if _, err := os.Stat(filepath.Join(root, "go.mod")); err != nil {
		return "", errors.New("run it from the repository: go run ./eval")
	}
	return root, nil
}

// build builds the CLI as asuka-real, and the shim that logs each call
// of it as asuka, into bin; it returns asuka-real.
func build(root, bin string) (string, error) {
	exe := ""
	if runtime.GOOS == "windows" {
		exe = ".exe"
	}
	real := filepath.Join(bin, "asuka-real"+exe)
	for _, b := range [][2]string{{real, "./cmd/asuka"}, {filepath.Join(bin, "asuka"+exe), "./eval/shim"}} {
		cmd := exec.Command("go", "build", "-o", b[0], b[1])
		cmd.Dir = root
		cmd.Env = append(os.Environ(), "CGO_ENABLED=0")
		if out, err := cmd.CombinedOutput(); err != nil {
			return "", fmt.Errorf("go build %s: %v\n%s", b[1], err, out)
		}
	}
	return real, nil
}

func tasksIn(dir, only string) ([]string, error) {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}
	var all []string
	for _, e := range entries {
		if e.IsDir() {
			all = append(all, e.Name())
		}
	}
	if only == "" {
		return all, nil
	}
	var out []string
	for _, t := range strings.Split(only, ",") {
		if !contains(all, t) {
			return nil, fmt.Errorf("no task %s (%s)", t, strings.Join(all, ", "))
		}
		out = append(out, t)
	}
	return out, nil
}

func contains(xs []string, x string) bool {
	for _, y := range xs {
		if y == x {
			return true
		}
	}
	return false
}

// framing goes before a task's prompt: where the agent is.
const framing = "This directory is a new Asuka project, made by `asuka new`; its AGENTS.md says how to work in it. " +
	"The `asuka` command is on your PATH. Do the task below in this directory, and check your work with asuka's own commands.\n\n"

func runTask(root, work, bin, real, task, agent string, timeout time.Duration) Result {
	r := Result{Task: task, Codes: map[string]int{}}
	taskDir := filepath.Join(root, "eval", "tasks", task)
	prompt, err := os.ReadFile(filepath.Join(taskDir, "prompt.md"))
	if err != nil {
		r.Error = err.Error()
		return r
	}
	dir := filepath.Join(work, task)
	os.RemoveAll(dir)
	if out, err := exec.Command(real, "new", dir).CombinedOutput(); err != nil {
		r.Error = fmt.Sprintf("asuka new: %v: %s", err, out)
		return r
	}
	log := filepath.Join(work, task+".calls.jsonl")
	os.Remove(log)
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()
	cmd := exec.CommandContext(ctx, "sh", "-c", agent)
	if runtime.GOOS == "windows" {
		cmd = exec.CommandContext(ctx, "cmd", "/c", agent)
	}
	cmd.Dir = dir
	cmd.Stdin = strings.NewReader(framing + string(prompt))
	cmd.Env = append(os.Environ(),
		"PATH="+bin+string(os.PathListSeparator)+os.Getenv("PATH"),
		"ASUKA_EVAL_REAL="+real,
		"ASUKA_EVAL_LOG="+log)
	transcript, _ := os.Create(filepath.Join(work, task+".agent.txt"))
	cmd.Stdout, cmd.Stderr = transcript, transcript
	start := time.Now()
	err = cmd.Run()
	transcript.Close()
	r.Seconds = time.Since(start).Seconds()
	if ctx.Err() != nil {
		r.TimedOut = true
	} else if err != nil {
		r.Error = "the agent: " + err.Error()
	}
	calls(log, &r)
	r.Left = left(real, dir)
	// The task's tests, against what the agent wrote.
	test, err := os.ReadFile(filepath.Join(taskDir, "eval_test.asuka"))
	if err != nil {
		r.Error = err.Error()
		return r
	}
	if err := os.WriteFile(filepath.Join(dir, "eval_test.asuka"), test, 0o644); err != nil {
		r.Error = err.Error()
		return r
	}
	r.Tests, r.Failed = runTests(real, dir, "eval: ")
	r.Passed = r.Tests > 0 && r.Failed == 0
	return r
}

// calls reads the shim's log: each compile the agent made, and the codes
// it reported.
func calls(log string, r *Result) {
	f, err := os.Open(log)
	if err != nil {
		return
	}
	defer f.Close()
	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 64<<10), 16<<20)
	for sc.Scan() {
		var c struct {
			Args  []string `json:"args"`
			Codes []string `json:"codes"`
		}
		if json.Unmarshal(sc.Bytes(), &c) != nil || len(c.Args) == 0 {
			continue
		}
		switch c.Args[0] {
		case "check", "test", "run", "build", "dev":
			r.RoundTrips++
			// What a compile said; asuka guide's and explain's pages
			// mention codes too, which aren't the agent's.
			for _, code := range c.Codes {
				if diag.Lookup(code) != nil {
					r.Codes[code]++
				}
			}
		}
	}
}

// left is what asuka check reports of the project at the end: its codes.
func left(real, dir string) []string {
	out, _ := exec.Command(real, "check", "--json", dir).Output()
	seen := map[string]bool{}
	for _, line := range strings.Split(string(out), "\n") {
		var d struct{ Code string }
		if json.Unmarshal([]byte(line), &d) == nil && d.Code != "" {
			seen[d.Code] = true
		}
	}
	var codes []string
	for c := range seen {
		codes = append(codes, c)
	}
	sort.Strings(codes)
	return codes
}

// runTests runs dir's tests whose names start with prefix: how many, and
// how many failed. A project that doesn't compile fails them all.
func runTests(real, dir, prefix string) (tests, failed int) {
	out, _ := exec.Command(real, "test", "--json", dir).Output()
	for _, line := range strings.Split(string(out), "\n") {
		var t struct {
			Test string `json:"test"`
			OK   bool   `json:"ok"`
			Code string `json:"code"`
		}
		if json.Unmarshal([]byte(line), &t) != nil || !strings.HasPrefix(t.Test, prefix) || t.Code != "" {
			continue
		}
		tests++
		if !t.OK {
			failed++
		}
	}
	if tests == 0 {
		return 1, 1 // it didn't compile, or the tests didn't run
	}
	return tests, failed
}

// table is the results as Markdown, for the README.
func table(rs []Result) string {
	var b strings.Builder
	b.WriteString("| Task | Passed | Round trips | Codes hit | Left |\n|---|---|---|---|---|\n")
	passed := 0
	for _, r := range rs {
		mark := "no"
		if r.Passed {
			mark = "yes"
			passed++
		}
		if r.TimedOut {
			mark += " (timed out)"
		}
		var codes []string
		for c, n := range r.Codes {
			codes = append(codes, fmt.Sprintf("`%s` ×%d", c, n))
		}
		sort.Strings(codes)
		var leftCodes []string
		for _, c := range r.Left {
			leftCodes = append(leftCodes, "`"+c+"`")
		}
		fmt.Fprintf(&b, "| %s | %s | %d | %s | %s |\n", r.Task, mark, r.RoundTrips, strings.Join(codes, ", "), strings.Join(leftCodes, ", "))
	}
	fmt.Fprintf(&b, "\n%d of %d passed.\n", passed, len(rs))
	return b.String()
}
