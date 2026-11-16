package watch

import (
	"context"
	"os"
	"path/filepath"
	"testing"
	"time"
)

// A watch of a directory: a file made in it comes as a create, one
// renamed as a rename with its old path; follow ends as the watch closes.
func TestFollow(t *testing.T) {
	dir := t.TempDir()
	w, err := watching{}.Open(context.Background(), dir, []string{".*"}, false, false)
	if err != nil {
		t.Fatal(err)
	}
	got := make(chan []Event, 16)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() { done <- w.Follow(ctx, func(es []Event) { got <- es }) }()
	next := func(want Op) Event {
		t.Helper()
		for {
			select {
			case es := <-got:
				for _, e := range es {
					if e.Op == want {
						return e
					}
				}
			case <-time.After(10 * time.Second):
				t.Fatalf("no %v", want)
			}
		}
	}
	os.WriteFile(filepath.Join(dir, "a.txt"), []byte("a"), 0o644)
	os.WriteFile(filepath.Join(dir, ".hidden"), []byte("h"), 0o644)
	if e := next(OpCreate); e.Path != "a.txt" {
		t.Errorf("create: %+v", e)
	}
	os.Rename(filepath.Join(dir, "a.txt"), filepath.Join(dir, "b.txt"))
	if e := next(OpRename); e.Path != "b.txt" || e.OldPath != "a.txt" {
		t.Errorf("rename: %+v", e)
	}
	w.Close()
	select {
	case err := <-done:
		if err != nil {
			t.Errorf("follow after close: %v", err)
		}
	case <-time.After(10 * time.Second):
		t.Fatal("follow didn't end as the watch closed")
	}
}
