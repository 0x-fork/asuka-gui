package compile

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	bc "github.com/egoist/asuka/lang/bytecode"
)

// UI programs compile now; they run once the runtime has the ui.*
// builtins (core/src/ui), and skip until then.
func TestUIPrograms(t *testing.T) {
	// A program is a file, or a directory: an app with its catalogs
	// (locales/).
	files, _ := filepath.Glob("testdata/ui/*")
	for _, path := range files {
		path := path
		info, err := os.Stat(path)
		if err != nil || !info.IsDir() && !strings.HasSuffix(path, ".asuka") {
			continue
		}
		t.Run(strings.TrimSuffix(filepath.Base(path), ".asuka"), func(t *testing.T) {
			var res *Result
			if info.IsDir() {
				var err error
				if res, err = Dir(path, nil); err != nil {
					t.Fatal(err)
				}
			} else {
				src, err := os.ReadFile(path)
				if err != nil {
					t.Fatal(err)
				}
				res = Files(map[string][]byte{path: src}, nil)
			}
			if err := res.Err(); err != nil {
				t.Fatalf("compile:\n%v", err)
			}
			if err := res.IR.Validate(); err != nil {
				t.Fatalf("invalid IR: %v", err)
			}
			if *dumpFlag {
				for i := range res.Module.Funcs {
					t.Log(bc.Disasm(res.Module, &res.Module.Funcs[i]))
				}
			}
			if res.Module.App < 0 {
				t.Fatal("no app")
			}
			out, err := runBoth(t, res.Module)
			if strings.Contains(out, "no builtin ui.") || strings.Contains(out, "no builtin test.") {
				t.Skip("the runtime has no UI builtins yet: " + strings.TrimSpace(out))
			}
			if err != nil {
				t.Fatalf("%v\n%s", err, out)
			}
			if strings.Contains(out, "FAIL") {
				t.Fatal(out)
			}
		})
	}
}
