package main

import (
	"os"
	"path/filepath"
	"slices"
	"testing"
	"time"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/lang/check"
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

// A file the program embeds (std/embed) is one of its sources, whatever
// its name: changing it, or adding one its glob names, compiles the app
// again and reloads it in place, where a .json it doesn't embed builds
// the player again; an embedded file under assets/ is both, as the
// platform reads it too.
func TestWatcherEmbeddedFiles(t *testing.T) {
	dir := t.TempDir()
	t0 := time.Now().Add(-time.Hour)
	for _, f := range []string{"app.asuka", "notes.txt", "data/a.json", "other.json", "assets/logo.svg"} {
		p := filepath.Join(dir, filepath.FromSlash(f))
		os.MkdirAll(filepath.Dir(p), 0o755)
		os.WriteFile(p, []byte("1"), 0o644)
		os.Chtimes(p, t0, t0)
	}
	w := newWatcher(dir)
	watchEmbeds(w, &appbuild.Uses{Embeds: []check.EmbedPath{{Dir: dir, Pattern: "notes.txt"}, {Dir: dir, Pattern: "data/*.json"}, {Dir: dir, Pattern: "assets/logo.svg"}}})
	if s, p := w.changed(); s || p {
		t.Fatal("changed with nothing changed")
	}
	for _, c := range []struct {
		file     string
		src, prj bool
	}{
		{"notes.txt", true, false},
		{"data/a.json", true, false},
		{"data/b.json", true, false},    // a new file the glob names
		{"assets/logo.svg", true, true}, // the platform's file too
		{"other.json", false, true},
		{"unwatched.txt", false, false},
	} {
		p := filepath.Join(dir, filepath.FromSlash(c.file))
		os.WriteFile(p, []byte("2"), 0o644)
		if s, pr := w.changed(); s != c.src || pr != c.prj {
			t.Errorf("%s: source %v, project %v; want %v, %v", c.file, s, pr, c.src, c.prj)
		}
	}
}
