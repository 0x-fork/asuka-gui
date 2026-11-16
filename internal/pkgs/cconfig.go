package pkgs

import (
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"github.com/egoist/asuka/internal/toml"
)

// CConfig is asuka.toml's [c] (docs/asuka-toml.md): the C an app's or a
// package's extern declarations call (RFC 0008, docs/c.md), compiled
// into the core's build for every target, and what it links. Each
// platform's table adds to the top's.
type CConfig struct {
	CSettings
	IOS     CSettings `json:"ios"`
	Android CSettings `json:"android"`
	MacOS   CSettings `json:"macos"`
	Windows CSettings `json:"windows"`
	Linux   CSettings `json:"linux"`
	Web     CSettings `json:"web"`
}

// CSettings are a [c] table's keys.
type CSettings struct {
	// Sources are the C files, globs relative to the project (c/*.c;
	// ** matches any directories).
	Sources []string `json:"sources"`
	// Include are directories the sources (and only they) include from.
	Include []string `json:"include"`
	// Defines are macros, NAME or NAME=value.
	Defines []string `json:"defines"`
	// Flags are more of the compiler's flags (-Wno-…, -std=c99).
	Flags []string `json:"flags"`
	// Libraries are linked where the platform has them: sqlite3 is
	// -lsqlite3; a path (lib/libfoo.a, relative to the project) is that
	// file.
	Libraries []string `json:"libraries"`
	// Frameworks are Apple's (iOS, macOS): Security, CoreFoundation.
	Frameworks []string `json:"frameworks"`
}

// CPlatforms are the [c.<platform>] tables' names.
var CPlatforms = []string{"ios", "android", "macos", "windows", "linux", "web"}

// For is the settings for platform (CPlatforms), the top table's then
// the platform's.
func (c *CConfig) For(platform string) CSettings {
	var p CSettings
	switch platform {
	case "ios":
		p = c.IOS
	case "android":
		p = c.Android
	case "macos":
		p = c.MacOS
	case "windows":
		p = c.Windows
	case "linux":
		p = c.Linux
	case "web":
		p = c.Web
	}
	cat := func(a, b []string) []string { return append(append([]string{}, a...), b...) }
	return CSettings{
		Sources:    cat(c.Sources, p.Sources),
		Include:    cat(c.Include, p.Include),
		Defines:    cat(c.Defines, p.Defines),
		Flags:      cat(c.Flags, p.Flags),
		Libraries:  cat(c.Libraries, p.Libraries),
		Frameworks: cat(c.Frameworks, p.Frameworks),
	}
}

// Empty reports whether c says nothing.
func (c *CConfig) Empty() bool {
	for _, p := range append([]string{""}, CPlatforms...) {
		s := c.For(p)
		if len(s.Sources)+len(s.Include)+len(s.Defines)+len(s.Flags)+len(s.Libraries)+len(s.Frameworks) > 0 {
			return false
		}
	}
	return true
}

var (
	cDefine = regexp.MustCompile(`^[A-Za-z_][A-Za-z0-9_]*(=.*)?$`)
	cLib    = regexp.MustCompile(`^[A-Za-z0-9_.+-]+$`)
)

// Check reports what's wrong in c: a define that isn't one, a library
// or framework name that isn't.
func (c *CConfig) Check() error {
	tables := map[string]CSettings{"c": c.CSettings, "c.ios": c.IOS, "c.android": c.Android, "c.macos": c.MacOS, "c.windows": c.Windows, "c.linux": c.Linux, "c.web": c.Web}
	names := make([]string, 0, len(tables))
	for n := range tables {
		names = append(names, n)
	}
	sort.Strings(names)
	for _, n := range names {
		s := tables[n]
		for _, d := range s.Defines {
			if !cDefine.MatchString(d) {
				return fmt.Errorf("[%s] defines: %q isn't a macro (NAME or NAME=value)", n, d)
			}
		}
		for _, l := range s.Libraries {
			if !cLib.MatchString(l) && !strings.ContainsAny(l, `/\`) {
				return fmt.Errorf("[%s] libraries: %q is neither a library's name (sqlite3) nor a file's path", n, l)
			}
		}
		for _, f := range s.Frameworks {
			if !cLib.MatchString(f) {
				return fmt.Errorf("[%s] frameworks: %q isn't a framework's name", n, f)
			}
		}
		for _, p := range append(append([]string{}, s.Sources...), s.Include...) {
			if filepath.IsAbs(p) || strings.HasPrefix(filepath.ToSlash(filepath.Clean(p)), "../") {
				return fmt.Errorf("[%s]: %q is outside the project (paths are relative to asuka.toml's directory, inside it)", n, p)
			}
		}
	}
	return nil
}

// ReadC reads [c] from dir's asuka.toml (nothing when there's none).
func ReadC(dir string) (*CConfig, error) {
	var m struct {
		C CConfig `json:"c"`
	}
	b, err := os.ReadFile(filepath.Join(dir, ManifestFile))
	if errors.Is(err, os.ErrNotExist) {
		return &m.C, nil
	}
	if err != nil {
		return nil, err
	}
	if err := toml.Unmarshal(b, &m); err != nil {
		return nil, fmt.Errorf("%s:%v", filepath.Join(dir, ManifestFile), err)
	}
	if err := m.C.Check(); err != nil {
		return nil, fmt.Errorf("%s: %v", filepath.Join(dir, ManifestFile), err)
	}
	return &m.C, nil
}

// CSources are the files s's source globs name in dir, sorted, each
// once. A glob that names none is an error: a typo, or a file missing.
func (s CSettings) CSources(dir string) ([]string, error) {
	seen := map[string]bool{}
	var out []string
	for _, g := range s.Sources {
		ms, err := globAll(dir, filepath.FromSlash(g))
		if err != nil {
			return nil, fmt.Errorf("[c] sources %q: %v", g, err)
		}
		if len(ms) == 0 {
			return nil, fmt.Errorf("[c] sources %q: no file in %s matches it", g, dir)
		}
		for _, m := range ms {
			if !seen[m] {
				seen[m] = true
				out = append(out, m)
			}
		}
	}
	sort.Strings(out)
	return out, nil
}

// globAll is filepath.Glob in dir, where ** matches any directories
// (none included).
func globAll(dir, pattern string) ([]string, error) {
	if !strings.Contains(pattern, "**") {
		return filepath.Glob(filepath.Join(dir, pattern))
	}
	re, err := globRegexp(filepath.ToSlash(pattern))
	if err != nil {
		return nil, err
	}
	var out []string
	err = filepath.WalkDir(dir, func(p string, e fs.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		if e.IsDir() {
			if p != dir && (strings.HasPrefix(e.Name(), ".") || e.Name() == "node_modules") {
				return filepath.SkipDir
			}
			return nil
		}
		rel, _ := filepath.Rel(dir, p)
		if re.MatchString(filepath.ToSlash(rel)) {
			out = append(out, p)
		}
		return nil
	})
	return out, err
}

// globRegexp turns a glob with ** into a regexp over slash paths.
func globRegexp(g string) (*regexp.Regexp, error) {
	var b strings.Builder
	b.WriteString("^")
	for i := 0; i < len(g); i++ {
		switch c := g[i]; {
		case strings.HasPrefix(g[i:], "**/"):
			b.WriteString("(?:.*/)?")
			i += 2
		case strings.HasPrefix(g[i:], "**"):
			b.WriteString(".*")
			i++
		case c == '*':
			b.WriteString("[^/]*")
		case c == '?':
			b.WriteString("[^/]")
		default:
			b.WriteString(regexp.QuoteMeta(string(c)))
		}
	}
	b.WriteString("$")
	return regexp.Compile(b.String())
}
