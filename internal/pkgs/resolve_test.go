package pkgs

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// registry serves packuments and tarballs for pkgs: name → version →
// dependencies.
func registry(t *testing.T, pkgs map[string]map[string]map[string]string) *Client {
	t.Helper()
	var srv *httptest.Server
	tarballs := map[string][]byte{}
	srv = httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if b, ok := tarballs[r.URL.Path]; ok {
			w.Write(b)
			return
		}
		name := strings.TrimPrefix(r.URL.Path, "/")
		vs, ok := pkgs[name]
		if !ok {
			http.NotFound(w, r)
			return
		}
		doc := Packument{Name: name, Versions: map[string]PackageMeta{}}
		for v, deps := range vs {
			tgz := tarball(t, map[string]string{"package/asuka.toml": "name = \"" + name + "\"\n", "package/src/main.asuka": "// " + name + " " + v + "\n"})
			p := "/-/" + strings.ReplaceAll(name, "/", "_") + "-" + v + ".tgz"
			tarballs[p] = tgz
			m := PackageMeta{Name: name, Version: v, Dependencies: deps}
			m.Dist.Tarball = srv.URL + p
			m.Dist.Integrity = Integrity(tgz)
			doc.Versions[v] = m
		}
		json.NewEncoder(w).Encode(doc)
	}))
	t.Cleanup(srv.Close)
	return NewClient(NPMRC{Registry: srv.URL + "/", Scopes: map[string]string{}, Tokens: map[string]string{}})
}

func tarball(t *testing.T, files map[string]string) []byte {
	var buf bytes.Buffer
	zw := gzip.NewWriter(&buf)
	tw := tar.NewWriter(zw)
	for name, body := range files {
		tw.WriteHeader(&tar.Header{Name: name, Mode: 0o644, Size: int64(len(body)), Typeflag: tar.TypeReg})
		tw.Write([]byte(body))
	}
	tw.Close()
	zw.Close()
	return buf.Bytes()
}

var world = map[string]map[string]map[string]string{
	"a":       {"1.0.0": {"b": "^1.2.0"}, "1.1.0": {"b": "^1.3.0"}},
	"b":       {"1.1.0": nil, "1.2.0": nil, "1.3.0": nil, "2.0.0": nil},
	"@acme/c": {"0.1.0": {"b": "^1.1.0"}},
}

func versions(l *Lock) string {
	var s []string
	for _, p := range l.Packages {
		s = append(s, p.ID+"="+p.Version)
	}
	return strings.Join(s, " ")
}

func TestMinimalVersionSelection(t *testing.T) {
	c := registry(t, world)
	l, err := Resolve(c, map[string]string{"a": "^1.0.0", "@acme/c": "^0.1.0"}, ".")
	if err != nil {
		t.Fatal(err)
	}
	// a's least accepted is 1.0.0, which needs b >= 1.2.0; c needs b >= 1.1.0: b 1.2.0.
	if got := versions(l); got != "@acme/c=0.1.0 a=1.0.0 b=1.2.0" {
		t.Fatalf("got %s", got)
	}
	if l.Root["a"] != "a" || l.Find("a").Deps["b"] != "b" {
		t.Fatalf("graph: %+v", l)
	}
}

func TestTwoMajors(t *testing.T) {
	c := registry(t, world)
	_, err := Resolve(c, map[string]string{"a": "^1.0.0", "b": "^2.0.0"}, ".")
	if err == nil || !strings.Contains(err.Error(), "alias") {
		t.Fatalf("want a two-majors error, got %v", err)
	}
	l, err := Resolve(c, map[string]string{"a": "^1.0.0", "b2": "npm:b@^2.0.0"}, ".")
	if err != nil {
		t.Fatal(err)
	}
	if got := versions(l); got != "a=1.0.0 b=1.2.0 b@2=2.0.0" {
		t.Fatalf("got %s", got)
	}
	if l.Root["b2"] != "b@2" {
		t.Fatalf("root: %v", l.Root)
	}
}

func TestFetch(t *testing.T) {
	t.Setenv("ASUKA_PKG_CACHE", t.TempDir())
	c := registry(t, world)
	l, err := Resolve(c, map[string]string{"b": "1.3.0"}, ".")
	if err != nil {
		t.Fatal(err)
	}
	dir, err := Fetch(c, l.Find("b"))
	if err != nil {
		t.Fatal(err)
	}
	b, err := os.ReadFile(filepath.Join(dir, "src", "main.asuka"))
	if err != nil || string(b) != "// b 1.3.0\n" {
		t.Fatalf("%q %v", b, err)
	}
	// A tampered lock fails the integrity check.
	bad := *l.Find("b")
	bad.Integrity = Integrity([]byte("other"))
	if _, err := Fetch(c, &bad); err == nil {
		t.Fatal("want an integrity error")
	}
}

func TestNPMRC(t *testing.T) {
	d := t.TempDir()
	os.WriteFile(filepath.Join(d, ".npmrc"), []byte("registry=https://r.example.com\n@acme:registry=https://npm.acme.dev/\n//npm.acme.dev/:_authToken=${ACME}\n"), 0o644)
	t.Setenv("ACME", "tok")
	t.Setenv("HOME", t.TempDir())
	rc := LoadNPMRC(d)
	if rc.RegistryFor("@acme/x") != "https://npm.acme.dev/" || rc.RegistryFor("y") != "https://r.example.com/" {
		t.Fatalf("%+v", rc)
	}
	if rc.TokenFor("https://npm.acme.dev/@acme%2fx") != "tok" {
		t.Fatalf("token: %+v", rc.Tokens)
	}
}
