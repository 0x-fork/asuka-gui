package compile

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// An editor's unsaved files: read instead of the disk's, and a new file
// that isn't on disk yet joins its directory's package.
func TestOverlay(t *testing.T) {
	root := t.TempDir()
	write := func(p, s string) {
		p = filepath.Join(root, filepath.FromSlash(p))
		if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(p, []byte(s), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	write("app/main.asuka", "import ./util\n\nfn four() -> int = util.twice(2)\n")
	write("app/util/util.asuka", "pub fn twice(n: int) -> int = n * 2\n")
	dir := filepath.Join(root, "app")

	res, err := Dir(dir, &Options{CheckOnly: true})
	if err != nil {
		t.Fatal(err)
	}
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	if res.Module != nil {
		t.Fatal("CheckOnly made a module")
	}

	// The imported package's file, changed but unsaved.
	ov := map[string][]byte{
		filepath.Join(dir, "util", "util.asuka"): []byte("pub fn thrice(n: int) -> int = n * 3\n"),
	}
	res, err = Dir(dir, &Options{Overlay: ov, CheckOnly: true})
	if err != nil {
		t.Fatal(err)
	}
	if err := res.Err(); err == nil || !strings.Contains(err.Error(), "package ./util has no twice") {
		t.Fatalf("got %v, want util.twice missing", err)
	}

	// A file not on disk yet, in the root package.
	ov = map[string][]byte{
		filepath.Join(dir, "more.asuka"): []byte("fn other() -> int = four() + 1\n"),
	}
	res, err = Dir(dir, &Options{Overlay: ov})
	if err != nil {
		t.Fatal(err)
	}
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	if len(res.Program.Root.Files) != 2 || res.Module == nil {
		t.Fatalf("got %d files, module %v", len(res.Program.Root.Files), res.Module != nil)
	}

	// A directory that only the overlay has.
	ov = map[string][]byte{
		filepath.Join(root, "fresh", "a.asuka"): []byte("fn a() -> int = 1\n"),
	}
	res, err = Dir(filepath.Join(root, "fresh"), &Options{Overlay: ov, CheckOnly: true})
	if err != nil {
		t.Fatal(err)
	}
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
}
