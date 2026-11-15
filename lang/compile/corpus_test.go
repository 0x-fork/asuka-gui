package compile

import (
	"flag"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

var update = flag.Bool("update", false, "rewrite testdata/corpus.golden")

// The parser's corpus (lang/syntax/testdata) checked, with stubs of
// what it uses that the prelude and the standard library don't have
// (testdata/corpus). The corpus was written before the language's
// library was settled (xs.count as a property, append, on tap); what's
// left of its diagnostics is in corpus.golden, so a change in what the
// checker says shows up in review.
func TestCorpus(t *testing.T) {
	read := func(p string) []byte {
		b, err := os.ReadFile(p)
		if err != nil {
			t.Fatal(err)
		}
		return b
	}
	corpus := func(name string) (string, []byte) {
		p := filepath.Join("..", "syntax", "testdata", name+".asuka")
		return p, read(p)
	}
	stub := func(name string) []byte { return read(filepath.Join("testdata", "corpus", name+".asuka")) }
	pkg := func(names ...string) map[string][]byte {
		m := map[string][]byte{}
		for _, n := range names {
			m[n+".asuka"] = stub(n)
		}
		return m
	}
	groups := []struct {
		files []string
		stubs []string
		pkgs  map[string]map[string][]byte
	}{
		{files: []string{"counter"}},
		{files: []string{"todo"}, stubs: []string{"ui"}},
		{files: []string{"styles"}, stubs: []string{"ui"}},
		{files: []string{"model", "mailbox"}, stubs: []string{"mail"}, pkgs: map[string]map[string][]byte{"asuka/http": pkg("http")}},
		{files: []string{"concurrency"}, stubs: []string{"ui", "concurrency"}, pkgs: map[string]map[string][]byte{"asuka/http": pkg("http"), "asuka/sensors": pkg("sensors")}},
		{files: []string{"app"}, stubs: []string{"ui", "app"}, pkgs: map[string]map[string][]byte{"./screens": pkg("screens")}},
		{files: []string{"inbox"}, stubs: []string{"ui", "inbox"}, pkgs: map[string]map[string][]byte{"asuka/http": pkg("http"), "./components": pkg("components")}},
		{files: []string{"native"}, stubs: []string{"native"}},
		{files: []string{"tests"}, pkgs: map[string]map[string][]byte{"./app": pkg("counterapp")}},
	}
	var got strings.Builder
	for _, g := range groups {
		files := map[string][]byte{}
		for _, f := range g.files {
			p, src := corpus(f)
			files[p] = src
		}
		for _, s := range g.stubs {
			files[filepath.Join("testdata", "corpus", s+".asuka")] = stub(s)
		}
		res := Files(files, &Options{Packages: g.pkgs})
		got.WriteString("== " + strings.Join(g.files, ", ") + "\n")
		for _, d := range res.Diags {
			name := d.Pos.Filename
			if !strings.Contains(name, filepath.Join("syntax", "testdata")) {
				t.Errorf("a stub has a problem: %s", d)
				continue
			}
			d.Pos.Filename = filepath.Base(name)
			got.WriteString(d.String() + "\n")
		}
	}
	golden := filepath.Join("testdata", "corpus.golden")
	if *update {
		if err := os.WriteFile(golden, []byte(got.String()), 0o644); err != nil {
			t.Fatal(err)
		}
		return
	}
	want, _ := os.ReadFile(golden)
	if got.String() != string(want) {
		t.Errorf("the corpus's diagnostics changed (go test -run Corpus -update):\n%s", got.String())
	}
}
