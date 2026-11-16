package pkgs

import (
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

func TestReadC(t *testing.T) {
	dir := t.TempDir()
	write := func(p, s string) {
		os.MkdirAll(filepath.Dir(filepath.Join(dir, p)), 0o755)
		if err := os.WriteFile(filepath.Join(dir, p), []byte(s), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	write("c/a.c", "")
	write("c/deep/b.c", "")
	write("web/w.c", "")
	write(ManifestFile, `
[c]
sources = ["c/**/*.c"]
defines = ["A=1"]
libraries = ["sqlite3"]

[c.web]
sources = ["web/*.c"]
libraries = []

[c.macos]
frameworks = ["Security"]
`)
	c, err := ReadC(dir)
	if err != nil {
		t.Fatal(err)
	}
	web := c.For("web")
	srcs, err := web.CSources(dir)
	if err != nil {
		t.Fatal(err)
	}
	var rel []string
	for _, s := range srcs {
		r, _ := filepath.Rel(dir, s)
		rel = append(rel, filepath.ToSlash(r))
	}
	if !slices.Equal(rel, []string{"c/a.c", "c/deep/b.c", "web/w.c"}) {
		t.Errorf("web's sources: %q", rel)
	}
	if mac := c.For("macos"); !slices.Equal(mac.Frameworks, []string{"Security"}) || !slices.Equal(mac.Libraries, []string{"sqlite3"}) {
		t.Errorf("macos: %+v", mac)
	}
	if c.Empty() {
		t.Error("empty")
	}

	for _, bad := range []struct{ toml, want string }{
		{"[c]\ndefines = [\"1X\"]\n", "isn't a macro"},
		{"[c]\nsources = [\"../x.c\"]\n", "outside the project"},
		{"[c.ios]\nframeworks = [\"a b\"]\n", "isn't a framework"},
	} {
		write(ManifestFile, bad.toml)
		if _, err := ReadC(dir); err == nil || !strings.Contains(err.Error(), bad.want) {
			t.Errorf("%q: %v, want %q", bad.toml, err, bad.want)
		}
	}
	write(ManifestFile, "[c]\nsources = [\"nothere/*.c\"]\n")
	c, _ = ReadC(dir)
	if _, err := c.For("ios").CSources(dir); err == nil || !strings.Contains(err.Error(), "no file") {
		t.Errorf("a glob that names nothing: %v", err)
	}
}
