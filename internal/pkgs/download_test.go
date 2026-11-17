package pkgs

import (
	"archive/tar"
	"archive/zip"
	"bytes"
	"compress/gzip"
	"crypto/sha256"
	"encoding/hex"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
)

func zipOf(t *testing.T, files map[string]string) []byte {
	t.Helper()
	var b bytes.Buffer
	w := zip.NewWriter(&b)
	for name, data := range files {
		f, _ := w.Create(name)
		f.Write([]byte(data))
	}
	w.Close()
	return b.Bytes()
}

func tgzOf(t *testing.T, files map[string]string) []byte {
	t.Helper()
	var b bytes.Buffer
	gz := gzip.NewWriter(&b)
	tw := tar.NewWriter(gz)
	for name, data := range files {
		tw.WriteHeader(&tar.Header{Name: name, Mode: 0o644, Size: int64(len(data)), Typeflag: tar.TypeReg})
		tw.Write([]byte(data))
	}
	tw.Close()
	gz.Close()
	return b.Bytes()
}

func hexSHA(b []byte) string {
	s := sha256.Sum256(b)
	return hex.EncodeToString(s[:])
}

// serve serves files by path, counting requests.
func serve(t *testing.T, files map[string][]byte) (string, *atomic.Int32) {
	t.Helper()
	var n atomic.Int32
	s := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		n.Add(1)
		b, ok := files[r.URL.Path]
		if !ok {
			http.NotFound(w, r)
			return
		}
		w.Write(b)
	}))
	t.Cleanup(s.Close)
	return s.URL, &n
}

// A download is fetched once into the cache, its top directory
// stripped; a .tar.gz as a .zip.
func TestDownloadFetch(t *testing.T) {
	t.Setenv("ASUKA_CACHE", t.TempDir())
	z := zipOf(t, map[string]string{"lib-1.0/lib.c": "int lib;", "lib-1.0/include/lib.h": "int lib(void);", "README": "top"})
	g := tgzOf(t, map[string]string{"pkg/x.c": "int x;"})
	url, n := serve(t, map[string][]byte{"/lib.zip": z, "/x.tar.gz": g})

	d := CDownload{URL: url + "/lib.zip", SHA256: hexSHA(z), Strip: 1}
	if d.Fetched() {
		t.Fatal("fetched before it was")
	}
	if err := d.Fetch(); err != nil {
		t.Fatal(err)
	}
	if b, _ := os.ReadFile(filepath.Join(d.Dir(), "include", "lib.h")); string(b) != "int lib(void);" {
		t.Errorf("include/lib.h: %q", b)
	}
	if _, err := os.Stat(filepath.Join(d.Dir(), "README")); err == nil {
		t.Error("a file above the stripped directory was kept")
	}
	if err := d.Fetch(); err != nil || n.Load() != 1 {
		t.Errorf("again: %v, %d requests", err, n.Load())
	}

	x := CDownload{URL: url + "/x.tar.gz", SHA256: hexSHA(g), Strip: 1}
	if err := x.Fetch(); err != nil {
		t.Fatal(err)
	}
	if b, _ := os.ReadFile(filepath.Join(x.Dir(), "x.c")); string(b) != "int x;" {
		t.Errorf("x.c: %q", b)
	}
}

// An archive that isn't the one pinned is refused, and nothing of it
// kept; one with a file outside it, too.
func TestDownloadRefused(t *testing.T) {
	t.Setenv("ASUKA_CACHE", t.TempDir())
	z := zipOf(t, map[string]string{"a/lib.c": "int lib;"})
	evil := zipOf(t, map[string]string{"a/../../evil.c": "int evil;"})
	url, _ := serve(t, map[string][]byte{"/lib.zip": z, "/evil.zip": evil})

	d := CDownload{URL: url + "/lib.zip", SHA256: strings.Repeat("0", 64), Strip: 1}
	if err := d.Fetch(); err == nil || !strings.Contains(err.Error(), "SHA-256") {
		t.Errorf("a different archive: %v", err)
	}
	if d.Fetched() {
		t.Error("a refused archive is in the cache")
	}
	e := CDownload{URL: url + "/evil.zip", SHA256: hexSHA(evil), Strip: 1}
	if err := e.Fetch(); err == nil || !strings.Contains(err.Error(), "outside") {
		t.Errorf("a file outside the archive: %v", err)
	}
}

// [c]'s paths name a download as $<name>/…: a file is its path there
// before it's fetched (a build fetches it before compiling), a glob
// fetches it; what's wrong is said when asuka.toml is read.
func TestDownloadPaths(t *testing.T) {
	t.Setenv("ASUKA_CACHE", t.TempDir())
	z := zipOf(t, map[string]string{"lib-1.0/lib.c": "int lib;", "lib-1.0/more.c": "int more;"})
	url, n := serve(t, map[string][]byte{"/lib.zip": z})
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, "glue.c"), nil, 0o644)
	manifest := `
[c]
sources = ["$lib/lib.c", "glue.c"]
include = ["$lib"]

[c.download.lib]
url = "` + url + `/lib.zip"
sha256 = "` + hexSHA(z) + `"
strip = 1
`
	os.WriteFile(filepath.Join(dir, ManifestFile), []byte(strings.Replace(manifest, url, "https://example.com", 1)), 0o644)
	c, err := ReadC(dir)
	if err != nil {
		t.Fatal(err)
	}
	c.Download["lib"] = CDownload{URL: url + "/lib.zip", SHA256: hexSHA(z), Strip: 1}
	d := c.Download["lib"]
	srcs, err := c.CSources(dir, c.For("ios"))
	if err != nil {
		t.Fatal(err)
	}
	if len(srcs) != 2 || srcs[0] != filepath.Join(d.Dir(), "lib.c") && srcs[1] != filepath.Join(d.Dir(), "lib.c") {
		t.Errorf("sources: %q", srcs)
	}
	if n.Load() != 0 {
		t.Error("naming a file fetched the download")
	}
	if inc, _, err := c.Path(dir, "$lib"); err != nil || inc != d.Dir() {
		t.Errorf("include: %s, %v", inc, err)
	}
	if err := c.FetchFor(c.For("ios")); err != nil || !d.Fetched() {
		t.Fatalf("FetchFor: %v", err)
	}
	glob := *c
	glob.Sources = []string{"$lib/*.c"}
	if srcs, err := glob.CSources(dir, glob.For("ios")); err != nil || len(srcs) != 2 {
		t.Errorf("a glob in the download: %q, %v", srcs, err)
	}

	for _, bad := range []struct{ toml, want string }{
		{`[c]` + "\n" + `sources = ["$nope/a.c"]`, "no [c.download.nope]"},
		{`[c]` + "\n" + `sources = ["$lib/../../a.c"]` + "\n[c.download.lib]\nurl = \"https://e.com/a.zip\"\nsha256 = \"" + hexSHA(z) + "\"", "outside its download"},
		{"[c.download.lib]\nurl = \"http://e.com/a.zip\"\nsha256 = \"" + hexSHA(z) + "\"", "https"},
		{"[c.download.lib]\nurl = \"https://e.com/a.rar\"\nsha256 = \"" + hexSHA(z) + "\"", ".zip"},
		{"[c.download.lib]\nurl = \"https://e.com/a.zip\"\nsha256 = \"abc\"", "SHA-256"},
	} {
		os.WriteFile(filepath.Join(dir, ManifestFile), []byte(bad.toml), 0o644)
		if _, err := ReadC(dir); err == nil || !strings.Contains(err.Error(), bad.want) {
			t.Errorf("%s:\n%v", bad.toml, err)
		}
	}
}
