package main

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/lang/bytecode"
)

// cmdTest is asuka test: an app's or a package's test blocks, compiled
// and run on the fake host (asuka-run: the VM, the core and the fake
// host built for this computer).
func cmdTest(args []string) error {
	var pattern, platform string
	fs := newFlagSet("test", "[dir]")
	fs.about = "Runs the test blocks of the app or package in dir (default: the working directory) on the\n" +
		"fake host: the app's views as a phone would show them, measured and laid out, driven by\n" +
		"tap, type and expect."
	fs.String(&pattern, "run", 0, "", "run only the tests whose names contain `text`")
	platformFlag(fs, &platform, []string{platformIOS, platformAndroid, platformMac, platformWindows, platformLinux, platformWeb},
		"the `platform` the fake host says it is (what the app's platform checks see): ios (the default), android, mac, windows, linux or web")
	if err := fs.parse(args); err != nil {
		return err
	}
	dir := fs.Arg(0)
	if dir == "" {
		dir = "."
	}
	p, err := appbuild.Load(dir)
	if err != nil {
		return err
	}
	m, err := compileProject(p)
	if err != nil {
		return err
	}
	return runTests(m, p, pattern, platform)
}

// testPlatform is the fake host's name for a --platform.
func testPlatform(p string) string {
	if p == platformMac {
		return "macos"
	}
	return p
}

func runTests(m *bytecode.Module, p *appbuild.Project, pattern, platform string) error {
	if len(m.Tests) == 0 {
		fmt.Println("asuka: no test blocks")
		return nil
	}
	start := time.Now()
	s, err := appbuild.RunTests(m, appbuild.TestOptions{Pattern: pattern, Platform: testPlatform(platform), Dir: cwdOr(p.Dir)}, os.Stdout)
	if err != nil {
		return err
	}
	n, failed := len(s.Results), s.Failed()
	if n == 0 {
		return fmt.Errorf("no test's name contains %q", pattern)
	}
	if failed > 0 {
		return fmt.Errorf("%d of %d %s failed", failed, n, plural(n, "test", "tests"))
	}
	fmt.Printf("ok   %d %s passed in %.2fs\n", n, plural(n, "test", "tests"), time.Since(start).Seconds())
	return nil
}

// cwdOr is the working directory, what reports are relative to, when
// dir is in it; else dir.
func cwdOr(dir string) string {
	wd, err := os.Getwd()
	if err != nil {
		return dir
	}
	if r, err := filepath.Rel(wd, dir); err == nil && (r == "." || filepath.IsLocal(r)) {
		return wd
	}
	return dir
}

// compileProject compiles p, printing its diagnostics relative to the
// working directory.
func compileProject(p *appbuild.Project) (*bytecode.Module, error) {
	m, diags, err := p.Compile()
	base := cwdOr(p.Dir)
	for _, d := range diags {
		fmt.Fprintln(os.Stderr, relDiag(base, d))
	}
	if err != nil {
		return nil, err
	}
	if m == nil {
		return nil, fmt.Errorf("%d %s", len(diags), plural(len(diags), "error", "errors"))
	}
	return m, nil
}

// cmdRun is asuka run: one file (or a directory), compiled and run on
// the fake host: its test blocks, and what its app shows.
func cmdRun(args []string) error {
	var pattern string
	fs := newFlagSet("run", "file.asuka")
	fs.about = "Compiles one .asuka file (or a package's directory) and runs it on the fake host: its test\n" +
		"blocks, if it has any, and its app, whose screen it prints (each view: its kind, frame and text)."
	fs.String(&pattern, "run", 0, "", "run only the tests whose names contain `text`")
	if err := fs.parse(args); err != nil {
		return err
	}
	if fs.Arg(0) == "" {
		return errors.New("usage: asuka run file.asuka")
	}
	p, err := appbuild.Load(fs.Arg(0))
	if err != nil {
		return err
	}
	m, err := compileProject(p)
	if err != nil {
		return err
	}
	if len(m.Tests) == 0 && m.App < 0 {
		return errors.New("nothing to run: no test blocks, and no app block")
	}
	var testErr error
	if len(m.Tests) > 0 {
		testErr = runTests(m, p, pattern, "")
	}
	if m.App >= 0 {
		if err := appbuild.ShowApp(m, os.Stdout); err != nil {
			return err
		}
	}
	return testErr
}
