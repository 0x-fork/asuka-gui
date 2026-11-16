package apphost

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// Every std package with desktop code is in the host.
func TestStdDesktop(t *testing.T) {
	b, err := os.ReadFile("std.go")
	if err != nil {
		t.Fatal(err)
	}
	dirs, _ := filepath.Glob("../../std/*/desktop")
	if len(dirs) == 0 {
		t.Fatal("no std desktop packages")
	}
	for _, d := range dirs {
		name := filepath.Base(filepath.Dir(d))
		if !strings.Contains(string(b), `"github.com/egoist/asuka/std/`+name+`/desktop"`) {
			t.Errorf("host/apphost/std.go doesn't import std/%s/desktop", name)
		}
	}
}
