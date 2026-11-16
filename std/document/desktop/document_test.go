package document

import (
	"context"
	"path/filepath"
	"testing"
)

// A write replaces the file whole; modified is 0 for a file that isn't.
func TestFiles(t *testing.T) {
	ctx := context.Background()
	p := filepath.Join(t.TempDir(), "a.txt")
	f := files{}
	if m, err := f.Modified(ctx, p); m != 0 || err != nil {
		t.Fatalf("modified before: %d %v", m, err)
	}
	if err := f.Write(ctx, p, []byte("one")); err != nil {
		t.Fatal(err)
	}
	if err := f.Write(ctx, p, []byte("two")); err != nil {
		t.Fatal(err)
	}
	b, err := f.Read(ctx, p)
	if string(b) != "two" || err != nil {
		t.Fatalf("read: %q %v", b, err)
	}
	if m, _ := f.Modified(ctx, p); m == 0 {
		t.Error("modified after: 0")
	}
}
