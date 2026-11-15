package syntax

import (
	"bytes"
	"flag"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
)

var update = flag.Bool("update", false, "rewrite the golden files of testdata")

func corpus(t testing.TB) []string {
	files, err := filepath.Glob("testdata/*.asuka")
	if err != nil || len(files) == 0 {
		t.Fatal("no testdata/*.asuka", err)
	}
	return files
}

func parseFile(t testing.TB, name string) (*SourceFile, []byte) {
	t.Helper()
	src, err := os.ReadFile(name)
	if err != nil {
		t.Fatal(err)
	}
	f, errs := ParseFile(NewFileSet(), filepath.Base(name), src)
	if len(errs) > 0 {
		t.Fatalf("%s: %v", name, errs)
	}
	return f, src
}

// TestGolden compares each corpus file's tree with its .golden dump
// (go test -update rewrites them).
func TestGolden(t *testing.T) {
	for _, name := range corpus(t) {
		f, _ := parseFile(t, name)
		var b bytes.Buffer
		if err := Fdump(&b, f.File, f); err != nil {
			t.Fatal(err)
		}
		golden := strings.TrimSuffix(name, ".asuka") + ".golden"
		if *update {
			if err := os.WriteFile(golden, b.Bytes(), 0o644); err != nil {
				t.Fatal(err)
			}
			continue
		}
		want, err := os.ReadFile(golden)
		if err != nil {
			t.Fatalf("%v (go test -update writes it)", err)
		}
		if !bytes.Equal(b.Bytes(), want) {
			t.Errorf("%s: the tree differs from %s (go test -update rewrites it); first difference:\n%s", name, golden, firstDiff(b.String(), string(want)))
		}
	}
}

func firstDiff(got, want string) string {
	g, w := strings.Split(got, "\n"), strings.Split(want, "\n")
	for i := range max(len(g), len(w)) {
		var gl, wl string
		if i < len(g) {
			gl = g[i]
		}
		if i < len(w) {
			wl = w[i]
		}
		if gl != wl {
			return "line " + strconv.Itoa(i+1) + ":\n got " + gl + "\nwant " + wl
		}
	}
	return ""
}
