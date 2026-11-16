package main

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"unicode"
	"unicode/utf8"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/internal/boot"
	"github.com/egoist/asuka/internal/filetype"
	"github.com/egoist/asuka/lang/bytecode"
)

// asukaApp is an app: its directory of .asuka files, its asuka.toml, its
// dependencies (appbuild.Project) and what its sources import and name,
// which decide what each platform's build needs.
type asukaApp struct {
	dir      string // the app's directory
	name     string // its base name
	bundleID string
	cfg      *appConfig // asuka.toml
	proj     *appbuild.Project
	uses     *appbuild.Uses
}

// displayName is the name under the app's icon.
func (a *asukaApp) displayName() string {
	if a.cfg != nil && a.cfg.Name != "" {
		return a.cfg.Name
	}
	return a.name
}

// sdkPrefix is the Go import path the feature tables (featurePermissions,
// iosFeatures, …) name the standard library's packages under, as they
// were written for Go apps: asuka/location is sdkPrefix + "location".
const sdkPrefix = "github.com/egoist/asuka/sdk/"

// goPath is the feature tables' name of the package an Asuka import
// names: asuka/location's is sdkPrefix + "location"; a dependency's
// (@acme/charts) is itself.
func goPath(imp string) string {
	if rest, ok := strings.CutPrefix(imp, "asuka/"); ok {
		return sdkPrefix + rest
	}
	return imp
}

// deps are the packages the app imports, as the feature tables name
// them, and the prelude's (app, ui), which every app has.
func (a *asukaApp) deps() []string {
	out := []string{"github.com/egoist/asuka/app", "github.com/egoist/asuka/ui"}
	for _, imp := range a.used().ImportList() {
		out = append(out, goPath(imp))
	}
	return out
}

// imports reports whether the app imports the package the feature
// tables name pkg (sdkPrefix + "location").
func (a *asukaApp) imports(pkg string) bool {
	if pkg == "github.com/egoist/asuka/app" || pkg == "github.com/egoist/asuka/ui" {
		return true
	}
	return slices.Contains(a.deps(), pkg)
}

// hasSymbol reports whether the app uses what a feature table names by
// its Go symbol ("github.com/egoist/asuka/sdk/location.RequestAlways",
// ".../sdk/push.(*Message).From"): it imports the package, and its
// sources name the function (location.requestAlways, or RequestAlways).
// The prelude's (ui.WebView, app.ShareSheet) are the names alone.
func (a *asukaApp) hasSymbol(sym string) bool {
	pkg, fn := splitSymbol(sym)
	if !a.imports(pkg) {
		return false
	}
	return a.names(fn)
}

// names reports whether the sources name fn, or fn with its first letter
// lower-case (Asuka's functions).
func (a *asukaApp) names(fn string) bool {
	u := a.used()
	if u.Names[fn] {
		return true
	}
	r, n := utf8.DecodeRuneInString(fn)
	return u.Names[string(unicode.ToLower(r))+fn[n:]]
}

// used is what the app's sources import and name (nothing, for an app
// whose sources weren't read).
func (a *asukaApp) used() *appbuild.Uses {
	if a.uses == nil {
		return &appbuild.Uses{}
	}
	return a.uses
}

// splitSymbol splits a Go symbol into its package's path and its
// function's (or method's) name.
func splitSymbol(sym string) (pkg, fn string) {
	slash := strings.LastIndexByte(sym, '/')
	dot := strings.IndexByte(sym[slash+1:], '.')
	if dot < 0 {
		return sym, ""
	}
	pkg, fn = sym[:slash+1+dot], sym[slash+1+dot+1:]
	if i := strings.LastIndexByte(fn, '.'); i >= 0 {
		fn = fn[i+1:] // (*T).M
	}
	return pkg, fn
}

// loadApp reads the app in dir: its asuka.toml, its dependencies
// (installed from asuka.lock when it has any), and what its sources
// import and name, from which its permissions and its platforms'
// features follow.
func loadApp(dir string) (*asukaApp, error) {
	proj, err := appbuild.Load(dir)
	if err != nil {
		return nil, err
	}
	if proj.File != "" {
		return nil, fmt.Errorf("%s is a file: asuka dev and asuka build take an app's directory (asuka run runs one file)", dir)
	}
	cfg, err := loadConfig(proj.Dir)
	if err != nil {
		return nil, err
	}
	// The catalogs: the compiler checks them (and tr's calls of them),
	// as each compile does.
	app := &asukaApp{dir: proj.Dir, name: filepath.Base(proj.Dir), bundleID: cfg.ID, cfg: cfg, proj: proj}
	app.refresh()
	warnDevLang(cfg)
	return app, nil
}

// refresh reads again what the sources import and name, and what that
// needs of asuka.toml's (permissions, entitlements), as the sources
// change (asuka dev).
func (a *asukaApp) refresh() {
	if a.proj == nil {
		a.proj = &appbuild.Project{Dir: a.dir}
	}
	a.uses = a.proj.Uses()
	deps := a.deps()
	a.cfg.permissionsFor(deps)
	if err := a.cfg.depManifests(a.packageDirs(), a.uses); err != nil {
		fmt.Fprintln(os.Stderr, "asuka:", err)
	}
	a.cfg.displayName = a.displayName()
	a.cfg.useICloud(a)
}

// compile compiles the app to its module, printing its diagnostics; an
// error says it didn't compile. Its test blocks are left out (and the
// test builtins with them, which only asuka test's fake host has): this
// is the module a player loads and a release compiles to C.
func (a *asukaApp) compile() (*bytecode.Module, error) {
	m, diags, err := a.proj.Compile()
	for _, d := range diags {
		fmt.Fprintln(os.Stderr, relDiag(a.dir, d))
	}
	if err != nil {
		return nil, err
	}
	if m == nil {
		return nil, fmt.Errorf("%d %s", len(diags), plural(len(diags), "error", "errors"))
	}
	if m.App < 0 {
		return nil, fmt.Errorf("%s has no app block (app { Root() }) to run", a.dir)
	}
	return appbuild.StripTests(m), nil
}

// relDiag makes a diagnostic's file relative to dir, when it's in it.
func relDiag(dir, d string) string {
	file, rest, ok := strings.Cut(d, ":")
	if !ok || !filepath.IsAbs(file) {
		return d
	}
	if r, err := filepath.Rel(dir, file); err == nil && !strings.HasPrefix(r, "..") {
		return r + ":" + rest
	}
	return d
}

// hostConfig is what the desktop host reads of the app as it starts
// (internal/boot's ConfigFile): asuka.toml's, and asuka dev's --lang and
// --dir for its runs. target is a release's (darwin-universal,
// windows-amd64...; "": asuka dev's player), and updates says whether
// it updates itself (asuka.toml's updates; not from the Mac App Store).
func (a *asukaApp) hostConfig(platform, target string, updates bool) boot.Config {
	c := boot.Config{
		Name:              a.displayName(),
		ID:                a.cfg.ID,
		Version:           a.cfg.Version,
		Build:             fmt.Sprint(a.cfg.Build),
		Accent:            a.cfg.Accent,
		Scheme:            a.cfg.Scheme,
		Lang:              devLang,
		Direction:         devDirection,
		Background:        a.cfg.Background,
		MultipleInstances: a.cfg.SingleInstance != nil && !*a.cfg.SingleInstance,
	}
	if u, err := a.cfg.updates(); err == nil && u != nil && target != "" && updates {
		c.UpdateFeed, c.UpdateKey, c.UpdateTarget = u.feed(target), strings.TrimSpace(u.PublicKey), target
	}
	if platform == platformWindows {
		if types := a.cfg.fileTypes(); len(types) > 0 {
			// A release's icons are its executable's resources; the
			// player's has none.
			ids := map[int]int{}
			for i, t := range types {
				if t.icon != "" && target != "" {
					ids[i] = fileTypeIconID(i)
				}
			}
			c.FileTypes = filetype.Encode(windowsFileTypes(types, ids))
		}
	}
	return c
}

// workDir is the app's directory for platform's builds in the user's
// cache.
func workDir(app *asukaApp, platform string) (string, error) {
	base, err := os.UserCacheDir()
	if err != nil {
		return "", err
	}
	sum := sha256.Sum256([]byte(app.dir))
	dir := filepath.Join(base, "asuka", app.name+"-"+hex.EncodeToString(sum[:])[:8], platform)
	return dir, os.MkdirAll(dir, 0o755)
}

// packageDirs are the app's dependencies' directories and the standard
// library's packages it imports (asuka/network), by package ID: what
// brings native code and asks things of the app (asuka.toml's [ios] and
// [android]).
func (a *asukaApp) packageDirs() map[string]string {
	out := map[string]string{}
	if a.proj == nil {
		return out
	}
	for id, d := range a.proj.Deps {
		out[id] = d
	}
	for id, d := range a.proj.Std() {
		out[id] = d
	}
	return out
}
