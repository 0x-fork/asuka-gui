package main

import (
	"archive/tar"
	"compress/gzip"
	"debug/elf"
	"encoding/xml"
	"image"
	"image/color"
	"io"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
	"time"

	"github.com/egoist/asuka/internal/cbuild"
)

func linuxTestApp(t *testing.T, config string) *asukaApp {
	t.Helper()
	dir := filepath.Join(t.TempDir(), "My Notes")
	os.MkdirAll(filepath.Join(dir, "assets", "img"), 0o755)
	os.WriteFile(filepath.Join(dir, "assets", "img", "a.txt"), []byte("asset"), 0o644)
	if config != "" {
		os.WriteFile(filepath.Join(dir, "asuka.toml"), []byte(config), 0o644)
	}
	cfg, err := loadConfig(dir)
	if err != nil {
		t.Fatal(err)
	}
	return &asukaApp{dir: dir, name: filepath.Base(dir), bundleID: cfg.ID, cfg: cfg}
}

// The host's settings: the app's name, ID, version and build (and the
// accent, as every platform's).
func TestLinuxHostConfig(t *testing.T) {
	app := linuxTestApp(t, "name = \"Notes & Co\"\nid = \"com.example.notes\"\nversion = \"2.1\"\nbuild = 7\naccent = \"#ff8800\"")
	c := app.hostConfig(platformLinux, "linux-amd64", true)
	if c.Name != "Notes & Co" || c.ID != "com.example.notes" || c.Version != "2.1" || c.Build != "7" || c.Accent != "#ff8800" || c.MultipleInstances {
		t.Errorf("%+v", c)
	}
	multi := linuxTestApp(t, "name = \"Notes\"\nid = \"com.example.notes\"\nsingleInstance = false")
	if c := multi.hostConfig(platformLinux, "", false); !c.MultipleInstances {
		t.Error(`"singleInstance": false isn't in the settings`)
	}
}

func TestLinuxExe(t *testing.T) {
	for name, want := range map[string]string{
		"Counter": "counter", "My Notes!": "my-notes", "Notes 2 Go": "notes-2-go", "笔记": "notes", "a/b": "a-b",
	} {
		app := &asukaApp{bundleID: "com.example.Notes", cfg: &appConfig{Name: name}}
		if got := linuxExe(app); got != want {
			t.Errorf("%q: %q, want %q", name, got, want)
		}
	}
}

func TestGAppIDValid(t *testing.T) {
	for id, want := range map[string]bool{
		"com.example.notes": true, "dev.asuka.my-app": true, "dev.asuka.my_app": true,
		"notes": false, "com.example.2notes": false, "com..notes": false, "com.example.notes!": false,
	} {
		if gAppIDValid(id) != want {
			t.Errorf("%q: %v", id, !want)
		}
	}
}

func TestLinuxArch(t *testing.T) {
	if a, err := linuxArch("arm64"); a != "arm64" || err != nil {
		t.Error(a, err)
	}
	if a, err := linuxArch(""); a != "arm64" && a != "amd64" || err != nil {
		t.Error(a, err)
	}
	if _, err := linuxArch("386"); err == nil {
		t.Error("386: no error")
	}
}

// The prefix the build lays out: the assets where the host finds them
// from bin/<exe>, the icon at each size, the desktop file and the
// metainfo, named for the ID; and its .tar.gz.
func TestLinuxShare(t *testing.T) {
	app := linuxTestApp(t, `name = "My Notes"
id = "com.example.notes"
version = "1.2"
icon = "icon.png"

[linux]
summary = "Write things down"
description = "Notes, kept.\n\nOn <your> machine & synced."
categories = ["Office", "Utility"]
keywords = ["memo"]
license = "MIT"
developer = "Example Inc."
homepage = "https://example.com"`)
	img := image.NewNRGBA(image.Rect(0, 0, 64, 64))
	for i := range img.Pix {
		img.Pix[i] = 200
	}
	img.SetNRGBA(0, 0, color.NRGBA{255, 0, 0, 255})
	if err := writePNG(app.cfg.Icon, img); err != nil {
		t.Fatal(err)
	}
	out := filepath.Join(t.TempDir(), "my-notes")
	if err := writeLinuxShare(app, out, "my-notes"); err != nil {
		t.Fatal(err)
	}
	for _, p := range []string{
		"share/my-notes/assets/img/a.txt",
		"share/icons/hicolor/16x16/apps/com.example.notes.png",
		"share/icons/hicolor/512x512/apps/com.example.notes.png",
		"share/applications/com.example.notes.desktop",
		"share/metainfo/com.example.notes.metainfo.xml",
	} {
		if _, err := os.Stat(filepath.Join(out, p)); err != nil {
			t.Error(err)
		}
	}
	if icon, err := loadIcon(filepath.Join(out, "share/icons/hicolor/48x48/apps/com.example.notes.png")); err != nil || icon.Bounds().Dx() != 48 {
		t.Errorf("48x48: %v", err)
	}

	desktop, _ := os.ReadFile(filepath.Join(out, "share/applications/com.example.notes.desktop"))
	for _, want := range []string{
		"[Desktop Entry]\nType=Application\n", "\nName=My Notes\n", "\nComment=Write things down\n", "\nExec=my-notes\n",
		"\nIcon=com.example.notes\n", "\nCategories=Office;Utility;\n", "\nKeywords=memo;\n", "\nStartupWMClass=my-notes\n",
	} {
		if !strings.Contains(string(desktop), want) {
			t.Errorf("the desktop file has no %q:\n%s", want, desktop)
		}
	}

	meta, _ := os.ReadFile(filepath.Join(out, "share/metainfo/com.example.notes.metainfo.xml"))
	var c struct {
		Type       string   `xml:"type,attr"`
		ID         string   `xml:"id"`
		Name       string   `xml:"name"`
		Summary    string   `xml:"summary"`
		License    string   `xml:"project_license"`
		Developer  string   `xml:"developer>name"`
		Paragraphs []string `xml:"description>p"`
		Categories []string `xml:"categories>category"`
		Launchable string   `xml:"launchable"`
		Icon       string   `xml:"icon"`
		Release    struct {
			Version string `xml:"version,attr"`
		} `xml:"releases>release"`
	}
	if err := xml.Unmarshal(meta, &c); err != nil {
		t.Fatalf("%v:\n%s", err, meta)
	}
	if c.Type != "desktop-application" || c.ID != "com.example.notes" || c.Name != "My Notes" || c.Summary != "Write things down" ||
		c.License != "MIT" || c.Developer != "Example Inc." || c.Launchable != "com.example.notes.desktop" || c.Icon != "com.example.notes" ||
		c.Release.Version != "1.2" || len(c.Paragraphs) != 2 || c.Paragraphs[1] != "On <your> machine & synced." ||
		strings.Join(c.Categories, ",") != "Office,Utility" {
		t.Errorf("%+v\n%s", c, meta)
	}

	// The archive: under the folder's name, the executable executable.
	os.MkdirAll(filepath.Join(out, "bin"), 0o755)
	os.WriteFile(filepath.Join(out, "bin", "my-notes"), []byte("ELF"), 0o644)
	tgz := filepath.Join(t.TempDir(), "my-notes.tar.gz")
	if err := tarGz(out, tgz); err != nil {
		t.Fatal(err)
	}
	f, _ := os.Open(tgz)
	defer f.Close()
	gz, err := gzip.NewReader(f)
	if err != nil {
		t.Fatal(err)
	}
	tr := tar.NewReader(gz)
	modes := map[string]int64{}
	for {
		h, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			t.Fatal(err)
		}
		modes[h.Name] = h.Mode
	}
	if modes["my-notes/bin/my-notes"] != 0o755 || modes["my-notes/share/my-notes/assets/img/a.txt"] != 0o644 || modes["my-notes/bin/"] != 0o755 {
		t.Errorf("%v", modes)
	}
}

// Without an icon, nothing names one; without linux's fields, the
// defaults.
func TestLinuxShareDefaults(t *testing.T) {
	app := linuxTestApp(t, "")
	if d := desktopEntry(app, "my-notes", false); strings.Contains(d, "Icon=") || !strings.Contains(d, "\nCategories=Utility;\n") || strings.Contains(d, "Comment=") {
		t.Error(d)
	}
	m := metainfo(app, false, time.Date(2026, 1, 2, 0, 0, 0, 0, time.UTC))
	if strings.Contains(m, "<icon") || !strings.Contains(m, "<summary>My Notes</summary>") || !strings.Contains(m, `date="2026-01-02"`) ||
		!strings.Contains(m, "<description>\n    <p>My Notes</p>\n  </description>") || !strings.Contains(m, "<category>Utility</category>") {
		t.Error(m)
	}
}

func TestFlatpakManifest(t *testing.T) {
	app := linuxTestApp(t, `id = "com.example.notes"`)
	m := flatpakManifest(app, "my-notes")
	for _, want := range []string{
		"\napp-id: com.example.notes\n", "\nruntime: org.gnome.Platform\n", "\nruntime-version: '" + flatpakRuntime + "'\n",
		"\nsdk: org.gnome.Sdk\n", "\ncommand: my-notes\n", "--socket=wayland", "cp -a bin lib share /app/", "\n        path: my-notes\n",
	} {
		if !strings.Contains(m, want) {
			t.Errorf("no %q in\n%s", want, m)
		}
	}
	app.cfg.Linux.FlatpakRuntime = "47"
	if !strings.Contains(flatpakManifest(app, "my-notes"), "runtime-version: '47'") {
		t.Error("linux.flatpakRuntime")
	}
}

// SQLite's library goes in the prefix's lib/<exe>, where the package
// finds it from bin/<exe> (sqlite/locate_linux.go); it's built for the
// build's architecture, and shows only its entry points, so a system
// SQLite in the same process (GTK's, say) never takes its symbols, nor
// it the system's.
func TestLinuxSQLiteLib(t *testing.T) {
	out := t.TempDir()
	lib := filepath.Join(linuxPluginLibs(out, "my-notes"), "libasqlite.so")
	if rel, _ := filepath.Rel(filepath.Join(out, "bin"), lib); rel != filepath.Join("..", "lib", "my-notes", "libasqlite.so") {
		t.Errorf("from the executable: %s", rel)
	}
	for arch, machine := range map[string]elf.Machine{"amd64": elf.EM_X86_64, "arm64": elf.EM_AARCH64} {
		if err := writeTestPluginLib("linux-"+arch, lib); err != nil {
			t.Fatal(err)
		}
		f, err := elf.Open(lib)
		if err != nil {
			t.Fatal(err)
		}
		if f.Machine != machine {
			t.Errorf("%s: %v", arch, f.Machine)
		}
		syms, err := f.DynamicSymbols()
		if err != nil {
			t.Fatal(err)
		}
		var exported []string
		for _, s := range syms {
			if s.Section != elf.SHN_UNDEF && elf.ST_BIND(s.Info) != elf.STB_LOCAL && elf.ST_TYPE(s.Info) == elf.STT_FUNC {
				exported = append(exported, s.Name)
			}
		}
		slices.Sort(exported)
		if !slices.Equal(exported, []string{"asq_call", "asq_version"}) {
			t.Errorf("%s exports %v", arch, exported)
		}
		f.Close()
	}
}

// A Linux build is an install prefix: one executable in bin (the host,
// the core and the app compiled to C), the plugins' libraries in
// lib/<exe>, where the core finds them, the app's settings in
// share/<exe>; the tarball and the Flatpak have them all.
func TestBuildLinux(t *testing.T) {
	needDesktopBuild(t, cbuild.Target{OS: "linux", Arch: "amd64"})
	useCounter(t, "Count: ")
	app := sqliteTestApp(t)
	machines := map[string]elf.Machine{"amd64": elf.EM_X86_64, "arm64": elf.EM_AARCH64}
	for _, arch := range []string{"amd64", "arm64"} {
		out := t.TempDir()
		if err := buildLinux(app, &buildOptions{out: out, arch: arch}); err != nil {
			t.Fatal(err)
		}
		prefix := filepath.Join(out, "linux", "sqliteapp")
		for _, f := range []string{"bin/sqliteapp", "lib/sqliteapp/libasqlite.so"} {
			e, err := elf.Open(filepath.Join(prefix, f))
			if err != nil {
				t.Fatal(err)
			}
			if e.Machine != machines[arch] {
				t.Errorf("%s: %s is for %v", arch, f, e.Machine)
			}
			if f == "bin/sqliteapp" {
				// The core is in the executable: no library of Asuka's
				// beside it, GTK and the rest opened as it runs.
				libs, _ := e.ImportedLibraries()
				for _, l := range libs {
					if strings.Contains(l, "asuka") {
						t.Errorf("%s: the executable needs %s", arch, l)
					}
				}
			}
			e.Close()
		}
		if _, err := os.Stat(filepath.Join(prefix, "lib", "sqliteapp", "libasuka.so")); err == nil {
			t.Errorf("%s: a libasuka.so beside the executable, which has the core", arch)
		}
		if b, err := os.ReadFile(filepath.Join(prefix, "share", "sqliteapp", "asuka-app.json")); err != nil || !strings.Contains(string(b), `"name": "sqliteapp"`) {
			t.Errorf("%s: the settings: %v\n%s", arch, err, b)
		}
		m, _ := os.ReadFile(filepath.Join(out, "linux", app.bundleID+".yml"))
		if !strings.Contains(string(m), "cp -a bin lib share /app/") {
			t.Errorf("%s: the Flatpak leaves lib out:\n%s", arch, m)
		}
		tgz, _ := filepath.Glob(filepath.Join(out, "linux", "*.tar.gz"))
		if len(tgz) != 1 {
			t.Fatalf("%s: %v", arch, tgz)
		}
		f, _ := os.Open(tgz[0])
		gz, err := gzip.NewReader(f)
		if err != nil {
			t.Fatal(err)
		}
		var names []string
		tr := tar.NewReader(gz)
		for h, err := tr.Next(); err == nil; h, err = tr.Next() {
			names = append(names, h.Name)
		}
		f.Close()
		for _, want := range []string{"sqliteapp/bin/sqliteapp", "sqliteapp/lib/sqliteapp/libasqlite.so", "sqliteapp/share/sqliteapp/asuka-app.json"} {
			if !slices.Contains(names, want) {
				t.Errorf("%s: the tarball has %v, not %s", arch, names, want)
			}
		}
	}
}
