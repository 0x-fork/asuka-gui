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
	srcs, err := c.CSources(dir, web)
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
		{"[c.prebuilt]\nplan9-x64 = \"v/libx.a\"\n", "isn't a target"},
		{"[c.prebuilt]\nlinux-x64 = \"v/libx.so\"\n", "isn't a static library"},
		{"[c.prebuilt]\nandroid-arm64 = \"../libx.a\"\n", "outside the project"},
		{"[c.prebuilt]\nweb = []\n", "no library"},
		{"[c.prebuilt]\nweb = 3\n", "a path, or a list of paths"},
	} {
		write(ManifestFile, bad.toml)
		if _, err := ReadC(dir); err == nil || !strings.Contains(err.Error(), bad.want) {
			t.Errorf("%q: %v, want %q", bad.toml, err, bad.want)
		}
	}
	write(ManifestFile, "[c]\nsources = [\"nothere/*.c\"]\n")
	c, _ = ReadC(dir)
	if _, err := c.CSources(dir, c.For("ios")); err == nil || !strings.Contains(err.Error(), "no file") {
		t.Errorf("a glob that names nothing: %v", err)
	}

	// A vendor's libraries, by target; a universal one for its platform's.
	write("v/ios/libx.a", "")
	write("v/mac/libx.a", "")
	write("v/mac/liby.a", "")
	write(ManifestFile, "[c.prebuilt]\nios-arm64 = \"v/ios/libx.a\"\nmacos = [\"v/mac/libx.a\", \"v/mac/liby.a\"]\nlinux-x64 = \"v/missing.a\"\n")
	c, err = ReadC(dir)
	if err != nil {
		t.Fatal(err)
	}
	if c.Empty() {
		t.Error("[c.prebuilt] alone is empty")
	}
	if got, err := c.PrebuiltFor(dir, "ios-arm64"); err != nil || !slices.Equal(got, []string{filepath.Join(dir, "v", "ios", "libx.a")}) {
		t.Errorf("ios-arm64: %q %v", got, err)
	}
	if got, err := c.PrebuiltFor(dir, "macos-x64"); err != nil || len(got) != 2 {
		t.Errorf("macos-x64, from macos: %q %v", got, err)
	}
	if _, err := c.PrebuiltFor(dir, "android-arm"); err == nil || !strings.Contains(err.Error(), "no library for android-arm") {
		t.Errorf("android-arm: %v", err)
	}
	if _, err := c.PrebuiltFor(dir, "linux-x64"); err == nil {
		t.Error("linux-x64: a file that isn't there")
	}
	if got, err := (&CConfig{}).PrebuiltFor(dir, "web"); got != nil || err != nil {
		t.Errorf("no [c.prebuilt]: %q %v", got, err)
	}
}
