package compile

import (
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing/fstest"

	"github.com/egoist/asuka/i18n"
	"github.com/egoist/asuka/internal/i18ngen"
	"github.com/egoist/asuka/internal/l10n"
	"github.com/egoist/asuka/internal/toml"
	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/syntax"
)

// Catalogs are an app's catalogs of messages (docs/i18n.md): a catalog
// per language, <tag>.json, in ARB of ICU messages.
type Catalogs struct {
	// FS holds the catalogs at its root.
	FS fs.FS
	// Dir is their directory as diagnostics name it, relative to the
	// app ("locales").
	Dir string
	// Root is the app's directory, which diagnostics' files are under
	// (empty: they're named by Dir alone).
	Root string
	// Source is the language the app is written in, whose catalog has
	// every message (default en, or the only catalog there is).
	Source string
}

// I18nConfig is asuka.toml's [i18n]: where an app's catalogs are, when
// they aren't the locales directory, and the language it's written in.
type I18nConfig struct {
	Dir    string `json:"dir"`
	Source string `json:"source"`
}

// CatalogsOf finds the catalogs of the app in dir: asuka.toml's [i18n],
// else its locales directory when that has catalogs. nil: it has none.
func CatalogsOf(dir string) (*Catalogs, error) {
	var cfg struct {
		I18n *I18nConfig `json:"i18n"`
	}
	if b, err := os.ReadFile(filepath.Join(dir, "asuka.toml")); err == nil {
		if err := toml.Unmarshal(b, &cfg); err != nil {
			return nil, fmt.Errorf("asuka.toml: %v", err)
		}
	}
	c := cfg.I18n
	if c == nil {
		if m, _ := filepath.Glob(filepath.Join(dir, "locales", "*.json")); len(m) == 0 {
			return nil, nil
		}
		c = &I18nConfig{}
	}
	if c.Dir == "" {
		c.Dir = "locales"
	}
	abs := filepath.Join(dir, filepath.FromSlash(c.Dir))
	if info, err := os.Stat(abs); err != nil || !info.IsDir() {
		return nil, fmt.Errorf("asuka.toml: [i18n] dir %s: no such directory", c.Dir)
	}
	if c.Source == "" {
		c.Source = "en"
		if m, _ := filepath.Glob(filepath.Join(abs, "*.json")); len(m) == 1 {
			c.Source = strings.TrimSuffix(filepath.Base(m[0]), ".json")
		}
	}
	return &Catalogs{FS: os.DirFS(abs), Dir: filepath.ToSlash(c.Dir), Root: dir, Source: c.Source}, nil
}

// framework is Asuka's own catalog, whose messages an app's keys
// "asuka.<key>" change.
var framework = sync.OnceValues(func() (*i18ngen.Catalog, error) {
	cat, diags, err := i18ngen.Load(l10n.FS(), i18ngen.Options{Dir: "asuka", Source: "en", Namespace: l10n.Namespace})
	if err == nil && diags.Errors() > 0 {
		err = diags.Err()
	}
	return cat, err
})

// Load reads and checks the catalogs (internal/i18ngen): what's wrong
// with them is in the diagnostics, warnings apart.
func (cs *Catalogs) Load() (*i18ngen.Catalog, i18ngen.Diagnostics, error) {
	lib, err := framework()
	if err != nil {
		return nil, nil, err
	}
	return i18ngen.Load(cs.FS, i18ngen.Options{
		Dir: cs.Dir, Source: cs.Source,
		Libraries: map[string]*i18ngen.Catalog{l10n.Namespace: lib},
	})
}

// position is where a catalog's diagnostic is.
func (cs *Catalogs) position(d i18ngen.Diagnostic) syntax.Position {
	name := d.File
	if cs.Root != "" {
		name = filepath.Join(cs.Root, filepath.FromSlash(d.File))
	}
	line := d.Line
	if line == 0 {
		line = 1
	}
	return syntax.Position{Filename: name, Line: line, Column: 1}
}

// catalog loads, checks and compiles the app's catalogs for the checker
// (tr) and the module (its blob). dates says the program formats dates
// itself (asuka/i18n), so every language's are compiled in. Errors and
// warnings are diagnostics; the catalog is nil when there are errors.
func (cs *Catalogs) catalog(dates bool) (*check.Catalog, []check.Diagnostic, []check.Diagnostic) {
	var errs, warns []check.Diagnostic
	fail := func(msg string) (*check.Catalog, []check.Diagnostic, []check.Diagnostic) {
		pos := syntax.Position{Filename: cs.Dir, Line: 1, Column: 1}
		if cs.Root != "" {
			pos.Filename = filepath.Join(cs.Root, filepath.FromSlash(cs.Dir))
		}
		return nil, append(errs, check.Diagnostic{Pos: pos, End: pos, Msg: msg}), warns
	}
	gen, diags, err := cs.Load()
	if err != nil {
		return fail(err.Error())
	}
	for _, d := range diags {
		pos := cs.position(d)
		cd := check.Diagnostic{Pos: pos, End: pos, Msg: d.Msg}
		if d.Warning {
			warns = append(warns, cd)
		} else {
			errs = append(errs, cd)
		}
	}
	if len(errs) > 0 {
		return nil, errs, warns
	}
	cat, err := i18n.NewCatalog(cs.FS, gen.Source)
	if err != nil {
		return fail(err.Error())
	}
	keys := map[string]*i18ngen.Key{}
	opts := i18n.CompileOptions{Dates: dates, DateArgs: map[string][]string{}}
	for _, k := range gen.Keys {
		keys[k.Name] = k
		for _, p := range k.Params {
			if p.Type == "DateTime" {
				opts.DateArgs[k.Name] = append(opts.DateArgs[k.Name], p.Name)
			}
		}
	}
	comp, err := cat.CompileWith(opts)
	if err != nil {
		return fail(err.Error())
	}
	out := &check.Catalog{Blob: comp.Blob, Dir: cs.Dir, Source: gen.Source, Keys: map[string]*check.CatalogKey{}}
	for i, name := range comp.Keys {
		k := keys[name]
		if k == nil {
			continue // a library's ("asuka.ok"), or a translation's alone (reported)
		}
		ck := &check.CatalogKey{Name: name, Message: k.Message, Description: k.Description, Index: i}
		for _, a := range comp.Args[i] {
			p := &check.CatalogParam{Name: a.Name, ARB: argType(a)}
			for _, kp := range k.Params {
				if kp.Name == a.Name && kp.Type != "" {
					p.ARB = kp.Type
				}
			}
			ck.Params = append(ck.Params, p)
		}
		out.Keys[name] = ck
	}
	return out, nil, warns
}

// argType is the type of an argument without a placeholder's: a number
// for plural, selectordinal and number, a date for date and time, a
// string otherwise (as i18ngen infers it).
func argType(a i18n.Arg) string {
	switch a.Kind {
	case "plural", "selectordinal", "number":
		return "int"
	case "date", "time":
		return "DateTime"
	}
	return "String"
}

// implicitCatalog is the catalog of an app that has none but formats
// numbers and dates (asuka/i18n): its source language's (English's)
// formats, with no messages.
func implicitCatalog() (*check.Catalog, error) {
	cat, err := i18n.NewCatalog(fstest.MapFS{"en.json": {Data: []byte(`{}`)}}, "en")
	if err != nil {
		return nil, err
	}
	comp, err := cat.CompileWith(i18n.CompileOptions{Dates: true})
	if err != nil {
		return nil, err
	}
	return &check.Catalog{Blob: comp.Blob, Source: "en", Implicit: true}, nil
}
