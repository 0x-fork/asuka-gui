package compile

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/cbuild"
)

// docs/c.md's examples (and its translations', whose code is the same
// but for its comments) compile: each Asuka block as a package, each C
// block with the core's include directory (asuka_ffi.h).
func TestCDocExamples(t *testing.T) {
	for _, page := range []string{"../../docs/c.md", "../../docs/zh/c.md", "../../docs/ja/c.md"} {
		doc, err := os.ReadFile(page)
		if err != nil {
			t.Fatal(err)
		}
		blocks := func(lang string) []string {
			var out []string
			for _, part := range strings.Split(string(doc), "```"+lang+"\n")[1:] {
				out = append(out, part[:strings.Index(part, "```")])
			}
			return out
		}
		asuka := blocks("asuka")
		if len(asuka) < 7 {
			t.Fatalf("%s: %d Asuka examples", page, len(asuka))
		}
		for _, src := range asuka {
			if !strings.Contains(src, "app {") {
				src += "\napp { Text(\"x\") }\n"
			}
			res := Files(map[string][]byte{"example.asuka": []byte(src)}, &Options{MainTest: true})
			if err := res.Err(); err != nil {
				t.Errorf("%s:\n%s\n%v", page, src, err)
			} else if res.Module.C == nil {
				t.Errorf("%s: no C:\n%s", page, src)
			}
		}
		cc, err := cbuild.Find()
		if err != nil {
			continue
		}
		core, err := FindCore()
		if err != nil {
			t.Fatal(err)
		}
		dir := t.TempDir()
		for i, src := range blocks("c") {
			p := filepath.Join(dir, filepath.Base(filepath.Dir(page))+"-"+string(rune('a'+i))+".c")
			if err := os.WriteFile(p, []byte(src), 0o644); err != nil {
				t.Fatal(err)
			}
			if _, err := cc.CompileWith([]string{p}, filepath.Join(dir, "obj"), []string{"-std=c17", "-Wall", "-Werror", "-I" + filepath.Join(core, "include")}); err != nil {
				t.Errorf("%s: a C example doesn't compile:\n%s\n%v", page, src, err)
			}
		}
	}
}
