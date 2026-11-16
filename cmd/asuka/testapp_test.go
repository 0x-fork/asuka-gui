package main

import (
	"os/exec"
	"path/filepath"
	"testing"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/internal/appbuild/apptest"
	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/lang/bytecode"
)

// useCounter makes appbuild.Compile compile any app to the
// hand-assembled Counter whose Text says label and the count (the
// compiler isn't in this build), until the test ends.
func useCounter(t *testing.T, label string) {
	t.Helper()
	old := appbuild.Compile
	appbuild.Compile = func(dir string, deps map[string]string, main bool) (*bytecode.Module, []string, error) {
		return apptest.Counter(label), nil, nil
	}
	t.Cleanup(func() { appbuild.Compile = old })
}

// projectApp is the app in dir (its asuka.toml and .asuka files), with
// deps (package ID → directory), as loadApp reads one.
func projectApp(t *testing.T, dir string, deps map[string]string) *asukaApp {
	t.Helper()
	app, err := projectAppErr(dir, deps)
	if err != nil {
		t.Fatal(err)
	}
	return app
}

func projectAppErr(dir string, deps map[string]string) (*asukaApp, error) {
	dir, err := filepath.Abs(dir)
	if err != nil {
		return nil, err
	}
	cfg, err := loadConfig(dir)
	if err != nil {
		return nil, err
	}
	if deps == nil {
		deps = map[string]string{}
	}
	app := &asukaApp{dir: dir, name: filepath.Base(dir), bundleID: cfg.ID, cfg: cfg, proj: &appbuild.Project{Dir: dir, Deps: deps}}
	app.refresh()
	return app, nil
}

// sqliteDeps are testdata/sqliteapp's dependencies: the sqlite plugin of
// this checkout.
func sqliteDeps() map[string]string {
	d, _ := filepath.Abs(filepath.Join("..", "..", "sdk", "sqlite"))
	return map[string]string{"asuka/sqlite": d}
}

// sqliteTestApp is testdata/sqliteapp, an app that uses the sqlite
// plugin (asuka/sqlite).
func sqliteTestApp(t *testing.T) *asukaApp {
	t.Helper()
	return projectApp(t, filepath.Join("testdata", "sqliteapp"), sqliteDeps())
}

// needDesktopBuild skips a test that builds a desktop app for t when this
// computer can't: the core needs a C compiler for t (Zig to cross-compile),
// the host the go command (the CLI runs from its checkout).
func needDesktopBuild(t *testing.T, target cbuild.Target) {
	t.Helper()
	if testing.Short() {
		t.Skip("builds an app (not -short)")
	}
	if _, err := cbuild.ForTarget(target); err != nil {
		t.Skip(err)
	}
	if _, err := exec.LookPath("go"); err != nil || appbuild.Checkout() == "" {
		t.Skip("the desktop host is built from the checkout with go")
	}
}
