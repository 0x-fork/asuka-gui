package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/internal/pkgs"
	"github.com/egoist/asuka/lang/doc"
	"github.com/egoist/asuka/std"
)

// cmdDoc prints a package's documentation as Markdown: its comment and
// its pub declarations' signatures and /// comments (lang/doc), or one
// declaration's. With --all, every package of the standard library and
// the platform SDK into a directory, as the website's reference pages.
func cmdDoc(args []string) error {
	var out string
	var all bool
	fs := newFlagSet("doc", "[package] [name]")
	fs.about = "Prints a package's documentation as Markdown: the package's comment, then each of its pub\n" +
		"declarations, its signature and its /// comment. The package is an import path (std/time,\n" +
		"platform/camera, a dependency of the project here), a directory (./util), or the package\n" +
		"in this directory when none is named. A name (Time, Time.add, now) prints only that one.\n" +
		"--all writes every package of std/ and platform/ into the directory -o names."
	fs.String(&out, "out", 'o', "", "write to this file (with --all, this directory) instead of printing")
	fs.Bool(&all, "all", 0, "every package of the standard library and the platform SDK")
	if err := fs.parse(args); err != nil {
		return err
	}
	if all {
		if out == "" {
			return errors.New("doc --all: -o names the directory to write the pages to")
		}
		return writeAllDocs(out)
	}
	if len(fs.Args()) > 2 {
		return errors.New("doc: a package and a name at most: asuka doc std/time Time.add")
	}
	p, err := loadDoc(fs.Arg(0))
	if err != nil {
		return err
	}
	text := p.Markdown()
	if name := fs.Arg(1); name != "" {
		d := p.Lookup(name)
		if d == nil {
			return fmt.Errorf("doc: %s has no pub %s", p.Path, name)
		}
		text = d.Markdown(2)
	}
	if out == "" {
		_, err := io.WriteString(os.Stdout, text)
		return err
	}
	return os.WriteFile(out, []byte(text), 0o644)
}

// loadDoc reads the documentation of the package arg names: an import
// path (std/…, platform/…, a dependency of the project in this
// directory), a directory, or "" for this directory's.
func loadDoc(arg string) (*doc.Package, error) {
	if p, ok := doc.Builtin(arg); ok {
		return p, nil
	}
	if std.Reserved(arg) {
		dir, ok := std.Package(arg)
		if !ok {
			return nil, fmt.Errorf("doc: there's no %s (asuka doc --all lists them in the docs: /docs/std, /docs/platform)", arg)
		}
		return doc.Load(dir, arg)
	}
	if arg == "" || strings.HasPrefix(arg, ".") || filepath.IsAbs(arg) || isDir(arg) {
		dir := arg
		if dir == "" {
			dir = "."
		}
		// Its import path: its asuka.toml's name, else its directory's.
		path := arg
		if m, err := pkgs.ReadManifest(dir); err == nil && m.Name != "" {
			path = m.Name
		} else if abs, err := filepath.Abs(dir); err == nil {
			path = filepath.Base(abs)
		}
		return doc.Load(dir, path)
	}
	proj, err := appbuild.Load(".")
	if err != nil {
		return nil, fmt.Errorf("doc: %s: %v", arg, err)
	}
	dir, ok := proj.Deps[arg]
	if !ok {
		return nil, fmt.Errorf("doc: %s isn't a package of the standard library, the platform SDK or this project's dependencies, nor a directory", arg)
	}
	return doc.Load(dir, arg)
}

func isDir(p string) bool {
	st, err := os.Stat(p)
	return err == nil && st.IsDir()
}

// docIndexEntry is a package in --all's index.json.
type docIndexEntry struct {
	Path        string `json:"path"`        // std/time
	Slug        string `json:"slug"`        // std/time: its page, <slug>.md
	Description string `json:"description"` // asuka.toml's
}

// writeAllDocs writes every package of the standard library (std/<name>.md)
// and the platform SDK (platform/<name>.md) into dir, and index.json:
// each one's path and description, sorted.
func writeAllDocs(dir string) error {
	ps, err := std.Packages()
	if err != nil {
		return err
	}
	type entry struct {
		path string
		p    *doc.Package
	}
	var es []entry
	for path, d := range ps {
		p, err := doc.Load(d, path)
		if err != nil {
			return fmt.Errorf("doc: %s: %v", path, err)
		}
		es = append(es, entry{path, p})
	}
	for _, path := range doc.BuiltinPaths() {
		p, _ := doc.Builtin(path)
		es = append(es, entry{path, p})
	}
	sort.Slice(es, func(i, j int) bool { return es[i].path < es[j].path })
	var index []docIndexEntry
	for _, e := range es {
		file := filepath.Join(dir, filepath.FromSlash(e.path)+".md")
		if err := os.MkdirAll(filepath.Dir(file), 0o755); err != nil {
			return err
		}
		if err := os.WriteFile(file, []byte(e.p.Markdown()), 0o644); err != nil {
			return err
		}
		desc := e.p.Description
		if desc == "" {
			desc = firstSentence(e.p.Doc, e.path)
		}
		index = append(index, docIndexEntry{Path: e.path, Slug: e.path, Description: desc})
	}
	b, _ := json.MarshalIndent(index, "", "  ")
	return os.WriteFile(filepath.Join(dir, "index.json"), append(b, '\n'), 0o644)
}

// firstSentence is a package comment's first sentence, without its
// "Package <path> ": a description for a package whose asuka.toml has
// none (the compiler's own).
func firstSentence(text, path string) string {
	text = strings.TrimPrefix(text, "Package "+path+" ")
	text = strings.Join(strings.Fields(text), " ")
	if i := strings.Index(text, ". "); i >= 0 {
		text = text[:i]
	}
	text = strings.TrimSuffix(text, ".")
	if text != "" {
		text = strings.ToUpper(text[:1]) + text[1:]
	}
	return text
}
