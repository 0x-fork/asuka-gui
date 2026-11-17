package ccache

import (
	"os"
	"path/filepath"
	"testing"
	"time"
)

func put(t *testing.T, p string, size int, used time.Time) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(p, make([]byte, size), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.Chtimes(p, used, used); err != nil {
		t.Fatal(err)
	}
}

func TestTrimEvictsLeastRecentlyUsed(t *testing.T) {
	root := t.TempDir()
	t.Setenv("ASUKA_CACHE", root)
	now := time.Now()
	old := Sharded("c", "o", Key("old"), ".o")
	older := Sharded("c", "o", Key("older"), ".o")
	recent := Sharded("c", "o", Key("recent"), ".o")
	put(t, older, 4000, now.Add(-72*time.Hour))
	put(t, old, 4000, now.Add(-48*time.Hour))
	put(t, recent, 4000, now.Add(-time.Minute))
	// An app's work directory: one entry, its newest file's age.
	work := filepath.Join(root, "counter-12345678")
	put(t, filepath.Join(work, "ios", "x"), 4000, now.Add(-24*time.Hour))
	os.Chtimes(work, now.Add(-24*time.Hour), now.Add(-24*time.Hour))

	if size, n := Size(); size != 16000 || n != 4 {
		t.Fatalf("Size = %d bytes, %d entries; want 16000, 4", size, n)
	}
	// Room for two: the oldest two go, to 90% of the cap.
	freed, removed := Trim(9000)
	if removed != 2 || freed != 8000 {
		t.Fatalf("Trim freed %d in %d entries; want 8000 in 2", freed, removed)
	}
	for _, p := range []string{older, old} {
		if _, err := os.Stat(p); err == nil {
			t.Errorf("%s wasn't evicted", p)
		}
	}
	for _, p := range []string{recent, work} {
		if _, err := os.Stat(p); err != nil {
			t.Errorf("%s was evicted", p)
		}
	}
	// What was used lately stays, whatever the cap.
	if _, removed := Trim(1); removed != 1 {
		t.Fatalf("Trim(1) removed %d; want only the work directory", removed)
	}
	if _, err := os.Stat(recent); err != nil {
		t.Error("an entry used a minute ago was evicted")
	}
}

func TestTouchKeepsAnEntry(t *testing.T) {
	root := t.TempDir()
	t.Setenv("ASUKA_CACHE", root)
	a := Sharded("c", "a", Key("a"), ".a")
	b := Sharded("c", "a", Key("b"), ".a")
	put(t, a, 100, time.Now().Add(-3*time.Hour))
	put(t, b, 100, time.Now().Add(-2*time.Hour))
	Touch(a)
	old := Recent
	Recent = 0
	defer func() { Recent = old }()
	Trim(150)
	if _, err := os.Stat(a); err != nil {
		t.Error("the entry used last was evicted")
	}
	if _, err := os.Stat(b); err == nil {
		t.Error("the entry used least lately was kept")
	}
}

func TestCounts(t *testing.T) {
	t.Setenv("ASUKA_CACHE", t.TempDir())
	Hit("c")
	Hit("c")
	Miss("c")
	Miss("swift")
	Flush()
	Hit("c")
	Flush()
	c := ReadCounts()
	if c.Hits["c"] != 3 || c.Misses["c"] != 1 || c.Misses["swift"] != 1 {
		t.Fatalf("counts %+v", c)
	}
	ResetCounts()
	if c := ReadCounts(); c.Hits["c"] != 0 {
		t.Fatalf("counts after a reset %+v", c)
	}
}

func TestClean(t *testing.T) {
	root := t.TempDir()
	t.Setenv("ASUKA_CACHE", root)
	put(t, Sharded("c", "o", Key("x"), ".o"), 10, time.Now())
	put(t, filepath.Join(root, "app-1", "f"), 20, time.Now())
	freed, err := Clean()
	if err != nil || freed != 30 {
		t.Fatalf("Clean = %d, %v", freed, err)
	}
	if size, n := Size(); size != 0 || n != 0 {
		t.Fatalf("after Clean: %d bytes, %d entries", size, n)
	}
}

func TestParseSize(t *testing.T) {
	for in, want := range map[string]int64{"10G": 10 << 30, "500M": 500 << 20, "1.5GB": 3 << 29, "2KiB": 2048, "123": 123, "0": 0} {
		if got, err := ParseSize(in); err != nil || got != want {
			t.Errorf("ParseSize(%q) = %d, %v; want %d", in, got, err, want)
		}
	}
	if _, err := ParseSize("lots"); err == nil {
		t.Error("ParseSize(lots) took it")
	}
}
