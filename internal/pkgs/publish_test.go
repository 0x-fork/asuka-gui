package pkgs

import (
	"encoding/base64"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"
)

func TestPackAndPublish(t *testing.T) {
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, "asuka.toml"), []byte("name = \"@acme/charts\"\nversion = \"1.2.0\"\n"), 0o644)
	os.MkdirAll(filepath.Join(dir, "node_modules", "x"), 0o755)
	os.WriteFile(filepath.Join(dir, "node_modules", "x", "y.js"), []byte("no"), 0o644)
	os.WriteFile(filepath.Join(dir, "chart.asuka"), []byte("pub component Chart {}\n"), 0o644)
	p := Package{Name: "@acme/charts", Version: "1.2.0", Dependencies: map[string]string{"b": "^1.0.0"}}
	tgz, pj, err := Pack(dir, p)
	if err != nil {
		t.Fatal(err)
	}
	again, _, _ := Pack(dir, p)
	if string(again) != string(tgz) {
		t.Fatal("packing isn't reproducible")
	}
	out := t.TempDir()
	if err := Extract(tgz, out); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(out, "node_modules")); err == nil {
		t.Fatal("node_modules was packed")
	}
	if b, _ := os.ReadFile(filepath.Join(out, "chart.asuka")); string(b) != "pub component Chart {}\n" {
		t.Fatalf("chart.asuka: %q", b)
	}

	var got map[string]any
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "PUT" || r.URL.Path != "/@acme/charts" || r.Header.Get("Authorization") != "Bearer tok" {
			t.Errorf("%s %s %q", r.Method, r.URL.Path, r.Header.Get("Authorization"))
		}
		b, _ := io.ReadAll(r.Body)
		json.Unmarshal(b, &got)
		w.WriteHeader(201)
	}))
	defer srv.Close()
	c := NewClient(NPMRC{Registry: srv.URL + "/", Scopes: map[string]string{}, Tokens: map[string]string{nerf(srv.URL + "/"): "tok"}})
	if err := c.Publish(pj, tgz, "", ""); err != nil {
		t.Fatal(err)
	}
	att := got["_attachments"].(map[string]any)["charts-1.2.0.tgz"].(map[string]any)
	data, _ := base64.StdEncoding.DecodeString(att["data"].(string))
	if string(data) != string(tgz) {
		t.Fatal("the attachment isn't the tarball")
	}
	v := got["versions"].(map[string]any)["1.2.0"].(map[string]any)
	if v["dist"].(map[string]any)["integrity"] != Integrity(tgz) || v["asuka"] != "asuka.toml" {
		t.Fatalf("version: %v", v)
	}

	if _, _, err := Pack(dir, Package{Name: "x", Version: "1.0.0", Dependencies: map[string]string{"y": "file:../y"}}); err == nil {
		t.Fatal("want an error for a file: dependency")
	}
}
