package main

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/plugin"
)

// Programs (docs/programs.md): a package whose fn main is its entry, a
// command-line tool with no UI. asuka build makes one an executable for
// this computer, or with --target for another desktop (Zig).

// programTargets are --target's values.
var programTargets = []string{"darwin-amd64", "darwin-arm64", "linux-amd64", "linux-arm64", "windows-amd64", "windows-arm64"}

// parseTarget reads --target (os-arch); "": this computer.
func parseTarget(s string) (cbuild.Target, error) {
	if s == "" {
		return cbuild.Host(), nil
	}
	for _, t := range programTargets {
		if s == t {
			os, arch, _ := strings.Cut(s, "-")
			return cbuild.Target{OS: os, Arch: arch}, nil
		}
	}
	return cbuild.Target{}, fmt.Errorf("--target %s: one of %s", s, strings.Join(programTargets, ", "))
}

// projectRoot is the directory of the project p is (or is a [bin] of).
func projectRoot(p *appbuild.Project) string {
	if p.Root != "" {
		return p.Root
	}
	return p.Dir
}

// programApp is the program in p (the project's own package, or one of
// its [bin]s) as the build sees an app: its directory, the project's
// asuka.toml (its version), what it uses.
func programApp(p *appbuild.Project) (*asukaApp, error) {
	cfg, err := loadConfig(projectRoot(p))
	if err != nil {
		return nil, err
	}
	app := &asukaApp{dir: p.Dir, name: p.ProgramName(), bundleID: cfg.ID, cfg: cfg, proj: p}
	app.refresh()
	return app, nil
}

// programExeName is the executable's file name for t.
func programExeName(name string, t cbuild.Target) string {
	if t.OS == "windows" {
		return name + ".exe"
	}
	return name
}

// buildProgram builds the program in p for t into dir: its executable,
// and its plugins' libraries beside it. It returns the executable's path.
func buildProgram(p *appbuild.Project, t cbuild.Target, dir, version string, verbose bool) (string, error) {
	app, err := programApp(p)
	if err != nil {
		return "", err
	}
	if version != "" {
		app.cfg.Version = strings.TrimPrefix(version, "v")
	}
	m, err := compileProject(p)
	if err != nil {
		return "", err
	}
	if m.Main < 0 {
		return "", fmt.Errorf("%s isn't a program: it has no fn main (asuka build --platform builds an app)", cwdOr(p.Dir))
	}
	ptarget := plugin.Target(t.OS, t.Arch)
	libs, err := appLibs(app, ptarget)
	if err != nil {
		return "", err
	}
	exe := filepath.Join(dir, programExeName(app.name, t))
	if verbose {
		fmt.Printf("asuka: building %s for %s\n", app.name, t)
	}
	if t.OS == "darwin" {
		natives, err := nativeFor(app, "macos")
		if err != nil {
			return "", err
		}
		if len(natives) > 0 {
			if runtime.GOOS != "darwin" {
				return "", errors.New("a program whose packages have Swift (asuka/http's URLSession) is built for macOS on a Mac")
			}
			if err := macProgram(app, m, natives, t.Arch, exe, verbose); err != nil {
				return "", err
			}
			return exe, writeLibs(libs, dir)
		}
	}
	err = appbuild.ProgramExe(t, m, appbuild.ProgramBuild{
		Name: app.name, Version: app.cfg.Version, Natives: desktopNativeDirs(app), Plugins: len(libs) > 0, Root: projectRoot(p),
	}, exe)
	if err != nil {
		return "", err
	}
	return exe, writeLibs(libs, dir)
}

// cmdBuildProgram is asuka build for a program: dist/<name> for this
// computer, dist/<os>-<arch>/<name> for --target's.
func cmdBuildProgram(p *appbuild.Project, o *buildOptions) error {
	t, err := parseTarget(o.target)
	if err != nil {
		return err
	}
	if o.platform != "" && o.platform != thisComputer() {
		return fmt.Errorf("--platform %s builds an app: a program takes --target (%s)", o.platform, strings.Join(programTargets, ", "))
	}
	root := projectRoot(p)
	dir := o.out
	if dir == "" {
		dir = filepath.Join(root, "dist")
	}
	if o.target != "" {
		dir = filepath.Join(dir, t.String())
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	exe, err := buildProgram(p, t, dir, o.version, o.verbose)
	if err != nil {
		return err
	}
	fmt.Printf("asuka: %s\n", relToCwd(exe))
	return nil
}

// isProgramDir reports whether dir is a program's directory (its package
// declares fn main).
func isProgramDir(dir string) bool {
	st, err := os.Stat(dir)
	if err != nil || !st.IsDir() {
		return false
	}
	abs, _ := filepath.Abs(dir)
	return isProgram(&appbuild.Project{Dir: abs})
}

// relToCwd is path relative to the working directory, when it's in it.
func relToCwd(path string) string {
	wd, err := os.Getwd()
	if err != nil {
		return path
	}
	if r, err := filepath.Rel(wd, path); err == nil && filepath.IsLocal(r) {
		return r
	}
	return path
}

// macProgram builds a program whose packages have Swift for macOS: the
// Mac host's Swift with theirs (the packages' runtime: NativeModules), no
// window, its entry a headless one (AsukaProgram), linked with the core
// and the program compiled to C.
func macProgram(app *asukaApp, m *bytecode.Module, natives []nativePackage, arch, exe string, verbose bool) error {
	_ = natives // nativeFor's again, in executable
	work, err := os.MkdirTemp("", "asuka-program-")
	if err != nil {
		return err
	}
	defer os.RemoveAll(work)
	b := &macBuild{app: app, work: work, release: true, program: true, run: macRunner(verbose)}
	tmp := filepath.Join(work, "program")
	if err := b.executable(tmp, []string{arch}, relativeFiles(appbuild.StripTests(m), projectRoot(app.proj))); err != nil {
		return err
	}
	if b.packages != nil && b.packages.bundled() {
		return errors.New("the program's packages bring frameworks or resources, which a program's one executable can't carry: build it as an app")
	}
	return copyFile(tmp, exe)
}

// relativeFiles is m with its functions' files relative to root (what an
// error's trace names).
func relativeFiles(m *bytecode.Module, root string) *bytecode.Module {
	c := *m
	c.Funcs = append([]bytecode.FuncDef{}, m.Funcs...)
	for i := range c.Funcs {
		if r, err := filepath.Rel(root, c.Funcs[i].File); err == nil && filepath.IsLocal(r) {
			c.Funcs[i].File = filepath.ToSlash(r)
		}
	}
	return &c
}
