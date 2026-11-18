package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/prebuilt"
)

func TestToUpload(t *testing.T) {
	local := map[string]string{"asuka-linux-x64": "aa", "asuka-prebuilt-wasm32-1.0.0.tar.gz": "bb", "install.sh": "cc", "SHA256SUMS": "dd"}
	// Nothing there yet: everything, SHA256SUMS last.
	if got := toUpload(local, nil); !slices.Equal(got, []string{"asuka-linux-x64", "asuka-prebuilt-wasm32-1.0.0.tar.gz", "install.sh", "SHA256SUMS"}) {
		t.Fatalf("a new release: %v", got)
	}
	// A run again: what's missing, what differs, what didn't finish.
	remote := []remoteAsset{
		{Name: "asuka-linux-x64", Digest: "sha256:aa", State: "uploaded"},
		{Name: "asuka-prebuilt-wasm32-1.0.0.tar.gz", Digest: "sha256:00", State: "uploaded"},
		{Name: "install.sh", Digest: "sha256:cc", State: "starter"},
		{Name: "SHA256SUMS", Digest: "sha256:dd", State: "uploaded"},
		{Name: "asuka-win32-x64.exe", Digest: "sha256:ee", State: "uploaded"},
	}
	if got := toUpload(local, remote); !slices.Equal(got, []string{"asuka-prebuilt-wasm32-1.0.0.tar.gz", "install.sh"}) {
		t.Fatalf("a run again: %v", got)
	}
	for i := range remote {
		remote[i].Digest, remote[i].State = "sha256:"+local[remote[i].Name], "uploaded"
	}
	if got := toUpload(local, remote); len(got) != 0 {
		t.Fatalf("a finished release: %v", got)
	}
}

func TestPackPrebuilt(t *testing.T) {
	pre := t.TempDir()
	write := func(p, data string) {
		p = filepath.Join(pre, filepath.FromSlash(p))
		os.MkdirAll(filepath.Dir(p), 0o755)
		if err := os.WriteFile(p, []byte(data), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	write("linux-x64/lib/release/linux-amd64/libcore.a", "core")
	write("linux-x64/prebuilt.json", `{"name": "linux-x64", "compiler": "zig", "files": {"linux-amd64/release/core": {"path": "lib/release/linux-amd64/libcore.a", "sha256": "x"}}}`)
	write("android-aar/maven/dev/asuka/x.aar", "aar")
	write("android-aar/prebuilt.json", `{"name": "android-aar", "files": {}, "maven": "maven"}`)

	pack := func() (*prebuilt.Manifest, string) {
		out := t.TempDir()
		m, err := packPrebuilt(pre, out, "1.0.0")
		if err != nil {
			t.Fatal(err)
		}
		if err := writeSums(out); err != nil {
			t.Fatal(err)
		}
		return m, out
	}
	m, out := pack()
	p := m.Packages["linux-x64"]
	if p.Asset != "asuka-prebuilt-linux-x64-1.0.0.tar.gz" || p.Compiler != "zig" || p.Version != "1.0.0" || p.Size == 0 {
		t.Fatalf("linux-x64: %+v", p)
	}
	if sum, _ := fileSHA256(filepath.Join(out, p.Asset)); sum != p.SHA256 {
		t.Fatalf("the manifest pins %s, the archive is %s", p.SHA256, sum)
	}
	if f := m.Archives["linux-amd64/release/core"]; f.Package != "linux-x64" || f.Path != "lib/release/linux-amd64/libcore.a" {
		t.Fatalf("the library: %+v", f)
	}
	if m.Maven == nil || m.Maven.Package != "android-aar" || m.Maven.Path != "maven" {
		t.Fatalf("maven: %+v", m.Maven)
	}
	var written prebuilt.Manifest
	b, _ := os.ReadFile(filepath.Join(out, "prebuilt-manifest.json"))
	if err := json.Unmarshal(b, &written); err != nil || written.Packages["linux-x64"].SHA256 != p.SHA256 {
		t.Fatalf("prebuilt-manifest.json: %v, %s", err, b)
	}
	sums, _ := os.ReadFile(filepath.Join(out, "SHA256SUMS"))
	if !strings.Contains(string(sums), p.SHA256+"  "+p.Asset+"\n") || strings.Contains(string(sums), "SHA256SUMS") {
		t.Fatalf("SHA256SUMS:\n%s", sums)
	}
	// Packed again (the job run again): the same archives.
	m2, _ := pack()
	if m2.Packages["linux-x64"].SHA256 != p.SHA256 || m2.Packages["android-aar"].SHA256 != m.Packages["android-aar"].SHA256 {
		t.Fatal("packing the same files again made other archives")
	}
}

// Each CLI is released packed beside itself, and SHA256SUMS has both.
func TestPackCLI(t *testing.T) {
	out := t.TempDir()
	exe := strings.Repeat("asuka ", 10000)
	for _, c := range [][3]string{{"asuka-linux-x64", "linux", "amd64"}, {"asuka-windows-arm64.exe", "windows", "arm64"}} {
		if err := os.WriteFile(filepath.Join(out, c[0]), []byte(exe), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := packCLI(out, c[0], c[1], c[2]); err != nil {
			t.Fatal(err)
		}
	}
	if err := writeSums(out); err != nil {
		t.Fatal(err)
	}
	sums, _ := os.ReadFile(filepath.Join(out, "SHA256SUMS"))
	for _, name := range []string{"asuka-linux-x64", "asuka-linux-x64.tar.gz", "asuka-windows-arm64.exe", "asuka-windows-arm64.zip"} {
		st, err := os.Stat(filepath.Join(out, name))
		if err != nil || st.Size() == 0 {
			t.Fatalf("%s: %v", name, err)
		}
		if strings.HasSuffix(name, ".gz") || strings.HasSuffix(name, ".zip") {
			if st.Size() >= int64(len(exe))/10 {
				t.Errorf("%s: %d bytes from %d", name, st.Size(), len(exe))
			}
		}
		if !strings.Contains(string(sums), "  "+name+"\n") {
			t.Errorf("SHA256SUMS has no %s:\n%s", name, sums)
		}
	}
}
