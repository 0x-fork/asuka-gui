package main

import (
	"os"
	"path/filepath"
	"slices"
	"testing"
	"time"
)

// The dev loops wake as the system reports a change, and changed decides.
func TestWatcherWakesOnAChange(t *testing.T) {
	dir := t.TempDir()
	src := filepath.Join(dir, "app.asuka")
	cfg := filepath.Join(dir, "asuka.toml")
	t0 := time.Now().Add(-time.Hour)
	os.WriteFile(src, []byte("app { Text(\"a\") }\n"), 0o644)
	os.WriteFile(cfg, []byte("name = \"a\"\n"), 0o644)
	os.Chtimes(src, t0, t0)
	os.Chtimes(cfg, t0, t0)
	w := newWatcher(dir)
	if s, p := w.changed(); s || p {
		t.Fatal("changed with nothing changed")
	}
	os.WriteFile(src, []byte("app { Text(\"b\") }\n"), 0o644)
	select {
	case <-w.wake:
	case <-time.After(10 * time.Second): // a hang guard
		t.Fatal("no wake for a change")
	}
	if s, p := w.changed(); !s || p {
		t.Fatal("a source's change: noticed as one")
	}
	if s, p := w.changed(); s || p {
		t.Fatal("a change: noticed once")
	}
	os.WriteFile(cfg, []byte("name = \"b\"\n"), 0o644)
	if s, p := w.changed(); s || !p {
		t.Fatal("asuka.toml's change: the project's")
	}
}

func TestOutermostRoots(t *testing.T) {
	dir := t.TempDir()
	a := filepath.Join(dir, "a")
	got := outermost([]string{filepath.Join(a, "app"), a, filepath.Join(dir, "ab"), a})
	if want := []string{a, filepath.Join(dir, "ab")}; !slices.Equal(got, want) {
		t.Fatal(got, want)
	}
}
