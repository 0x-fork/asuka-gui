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
	for _, d := range res.Warnings {
		if rel, err := filepath.Rel(wd, d.Pos.Filename); err == nil && !strings.HasPrefix(rel, "..") {
			d.Pos.Filename = rel
		}
		fmt.Fprintf(w, "%s: warning: %s\n", d.Pos, d.Msg)
	}
	for _, d := range res.Diags {
		if rel, err := filepath.Rel(wd, d.Pos.Filename); err == nil && !strings.HasPrefix(rel, "..") {
			d.Pos.Filename = rel
		}
		fmt.Fprintln(w, d.String())
	}
	return len(res.Diags) > 0
}
