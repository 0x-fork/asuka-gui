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
	if len(args) != 1 {
		return errors.New("usage: asuka new <dir>")
	}
	dir := args[0]
	if entries, err := os.ReadDir(dir); err == nil && len(entries) > 0 {
		return fmt.Errorf("%s is not empty", dir)
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
