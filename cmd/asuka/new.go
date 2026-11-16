package main

import (
	"bytes"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"text/template"

	"github.com/egoist/asuka/internal/toml"
	tpl "github.com/egoist/asuka/template"
)

// asukaCheckout returns the Asuka source tree this CLI was built from, when
// it runs from a checkout (go run ./cmd/asuka, or go install from one).
func asukaCheckout() string {
	_, file, _, ok := runtime.Caller(0)
	if !ok {
		return ""
	}
	root := filepath.Dir(filepath.Dir(filepath.Dir(file)))
	if data, err := os.ReadFile(filepath.Join(root, "go.mod")); err == nil && bytes.Contains(data, []byte("module github.com/egoist/asuka\n")) {
		return root
	}
	return ""
}

func cmdNew(args []string) error {
	flags := newFlagSet("new", "<dir>")
	flags.about = `Makes a project in dir: an app (the template's: asuka.toml, app.asuka with
a component and a test block), or with --lib a library, an Asuka package
other apps import (asuka.toml, <name>.asuka with a function and a test
block). A library with --native also declares a native module, which each
platform's own code implements: a Swift package for iOS and macOS
(Package.swift, swift/), a Gradle module for Android (android/), JavaScript
for the web (web/), and C for Windows and Linux (desktop/); asuka native
add adds one later, and asuka native generate (which dev and build run
too) connects them.`
	var lib, native bool
	only := strings.Join(nativeProjects, ",")
	flags.Bool(&lib, "lib", 0, "make a library, not an app")
	flags.Bool(&native, "native", 0, "a library with a native module and each platform's code (implies --lib)")
	flags.String(&only, "projects", 0, only, "with --native: the platforms' projects to make")
	if err := flags.parse(args); err != nil {
		return err
	}
	if len(flags.Args()) != 1 {
		return errors.New("usage: asuka new [--lib [--native [--projects swift,android,web,desktop]]] <dir>")
	}
	dir := flags.Arg(0)
	if entries, err := os.ReadDir(dir); err == nil && len(entries) > 0 {
		return fmt.Errorf("%s is not empty", dir)
	}
	if lib || native {
		return newLib(dir, native, strings.Split(only, ","))
	}
	abs, err := filepath.Abs(dir)
	if err != nil {
		return err
	}
	name := filepath.Base(abs)
	data := struct{ Name, ID string }{
		Name: name,
		ID:   "com.example." + strings.Trim(strings.ToLower(idPart.ReplaceAllString(name, "")), "-"),
	}
	err = fs.WalkDir(tpl.Files, "files", func(path string, d fs.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return err
		}
		src, _ := tpl.Files.ReadFile(path)
		rel := strings.TrimSuffix(strings.TrimPrefix(path, "files/"), ".tmpl")
		if rel == "gitignore" {
			rel = ".gitignore"
		}
		t, err := template.New(rel).Funcs(template.FuncMap{"quote": toml.Quote}).Parse(string(src))
		if err != nil {
			return err
		}
		var out bytes.Buffer
		if err := t.Execute(&out, data); err != nil {
			return err
		}
		dst := filepath.Join(abs, rel)
		os.MkdirAll(filepath.Dir(dst), 0o755)
		return os.WriteFile(dst, out.Bytes(), 0o644)
	})
	if err != nil {
		return err
	}
	fmt.Printf("asuka: made %s\n\n  cd %s\n  asuka dev --platform ios    # or android, web; on macOS, Windows or Linux, asuka dev runs it on this computer\n  asuka test                  # its test blocks\n", dir, dir)
	return nil
}
