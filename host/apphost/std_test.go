package apphost

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// Every standard library package with desktop Go is in every host:
// std.go imports each std/<name>/desktop.
func TestStdDesktop(t *testing.T) {
	src, err := os.ReadFile("std.go")
	if err != nil {
		t.Fatal(err)
	}
	dirs, _ := filepath.Glob("../../std/*/desktop")
	if len(dirs) == 0 {
		t.Fatal("no std/*/desktop")
	}
	for _, d := range dirs {
		if files, _ := filepath.Glob(filepath.Join(d, "*.go")); len(files) == 0 {
			continue
		}
		name := filepath.Base(filepath.Dir(d))
		if !strings.Contains(string(src), `"github.com/egoist/asuka/std/`+name+`/desktop"`) {
			t.Errorf("std.go doesn't import std/%s/desktop", name)
		}
	}
}
