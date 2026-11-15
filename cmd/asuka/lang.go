package main

import (
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/egoist/asuka/lang/compile"
)

// The commands of the Asuka language's compiler (lang/compile): check,
// test, run.

// errReported says the command printed what went wrong already.
var errReported = errors.New("failed")

// cmdCheck reports a package's diagnostics.
func cmdCheck(args []string) error {
	fs := newFlagSet("check", "[dir]")
	fs.about = "Checks the Asuka package in dir (default .): its files, and what they import. Prints\n" +
		"each problem as file:line:col: message."
	if err := fs.parse(args); err != nil {
		return err
	}
	dir := "."
	if len(fs.Args()) > 0 {
		dir = fs.Args()[0]
	}
	res, err := compile.Dir(dir, nil)
	if err != nil {
		return err
	}
	if printDiags(os.Stderr, res) {
		return errReported
	}
	return nil
}

// cmdTest runs a package's test blocks on the VM, on the fake host.
func cmdTest(args []string) error {
	var run string
	var verbose bool
	fs := newFlagSet("test", "[dir]")
	fs.about = "Runs the test blocks of the Asuka package in dir (default .) on the VM, with the fake\n" +
		"host and a clock that moves only when nothing else can run. Needs a C compiler (the\n" +
		"runner is built once, and cached); ASUKA_CORE names the core's directory when asuka\n" +
		"can't find it."
	fs.String(&run, "run", 0, "", "run only the tests whose names contain `pattern`")
	fs.Bool(&verbose, "verbose", 'v', "print every test's result, not only failures")
	if err := fs.parse(args); err != nil {
		return err
	}
	dir := "."
	if len(fs.Args()) > 0 {
		dir = fs.Args()[0]
	}
	res, err := compile.Dir(dir, nil)
	if err != nil {
		return err
	}
	if printDiags(os.Stderr, res) {
		return errReported
	}
	if len(res.Module.Tests) == 0 {
		fmt.Println("no tests")
		return nil
	}
	return runModule(res, true, run, verbose, "")
}

// cmdRun runs a file: its fn main(), or else its tests.
func cmdRun(args []string) error {
	fs := newFlagSet("run", "file.asuka|dir")
	fs.about = "Runs an Asuka file (or package) on the VM: its fn main() when it has one, else its\n" +
		"test blocks. For trying things out; apps run with asuka dev."
	if err := fs.parse(args); err != nil {
		return err
	}
	if len(fs.Args()) != 1 {
		return errors.New("run takes a file: asuka run hello.asuka")
	}
	path := fs.Args()[0]
	var res *compile.Result
	if info, err := os.Stat(path); err == nil && info.IsDir() {
		res, err = compile.Dir(path, &compile.Options{MainTest: true})
		if err != nil {
			return err
		}
	} else {
		src, err := os.ReadFile(path)
		if err != nil {
			return err
		}
		res = compile.Files(map[string][]byte{path: src}, &compile.Options{MainTest: true})
	}
	if printDiags(os.Stderr, res) {
		return errReported
	}
	for _, t := range res.Module.Tests {
		if t.Name == "main" {
			return runModule(res, true, "main", false, "main")
		}
	}
	return runModule(res, true, "", true, "")
}

// runModule runs a compiled module's tests and prints what they print.
// hide is a test whose "ok" line isn't shown (main's).
func runModule(res *compile.Result, test bool, pattern string, verbose bool, hide string) error {
	runner, err := compile.Runner(false)
	if err != nil {
		return err
	}
	out, err := compile.Run(runner, res.Module, test, pattern)
	failed := false
	for _, line := range strings.Split(strings.TrimRight(out, "\n"), "\n") {
		switch {
		case line == "ok "+hide && hide != "":
		case strings.HasPrefix(line, "ok "):
			if verbose {
				fmt.Println(line)
			}
		case strings.HasPrefix(line, "FAIL "):
			failed = true
			fmt.Println(line)
		case line != "":
			fmt.Println(line)
		}
	}
	if err != nil || failed {
		if !failed {
			return fmt.Errorf("the runner failed: %v", err)
		}
		return errReported
	}
	if !verbose && hide == "" {
		fmt.Printf("ok (%d tests)\n", countTests(res, pattern))
	}
	return nil
}

func countTests(res *compile.Result, pattern string) int {
	n := 0
	for _, t := range res.Module.Tests {
		if pattern == "" || strings.Contains(t.Name, pattern) {
			n++
		}
	}
	return n
}

// printDiags prints a compilation's diagnostics, paths relative to the
// working directory; it reports whether there were any.
func printDiags(w io.Writer, res *compile.Result) bool {
	wd, _ := os.Getwd()
	for _, d := range res.Diags {
		if rel, err := filepath.Rel(wd, d.Pos.Filename); err == nil && !strings.HasPrefix(rel, "..") {
			d.Pos.Filename = rel
		}
		fmt.Fprintln(w, d.String())
	}
	return len(res.Diags) > 0
}
