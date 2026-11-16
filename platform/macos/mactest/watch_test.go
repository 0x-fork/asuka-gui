//go:build darwin

package mactest

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// std/watch on the Mac (its Swift: FSEvents): a directory's creates,
// writes, renames and removes, a batch at a time, what its patterns
// ignore left out; the directory gone ends the watch with a remove of ".".
func TestWatch(t *testing.T) {
	dir := t.TempDir()
	src := fmt.Sprintf(`
		import std/watch

		component Main {
		  state log = ""
		  state root = ""
		  task {
		    do {
		      let w = try await watch.path(%q, ignore: ["*.tmp"], recursive: true)
		      root = w.root
		      for await events in w.events {
		        for e in events {
		          log = log + "{e.op} {e.path} {e.oldPath};"
		        }
		      }
		      log = log + "ended;"
		    } catch e {
		      log = "error: " + e.message
		    }
		  }
		  Column {
		    Text("root: {root}")
		    Text("log: {log}")
		  }
		}
		app { Main() }
	`, dir)
	a := RunWith(t, src, Options{Player: NativePlayer(t, "watch", src)})
	real, _ := filepath.EvalSymlinks(dir)
	a.UntilText("root: " + real)
	logText := func() string {
		for _, n := range a.Tree().Kind("Text") {
			if s, ok := strings.CutPrefix(n.Text, "log: "); ok {
				return s
			}
		}
		return ""
	}
	waitLog := func(want string) {
		t.Helper()
		a.Until(func(tr *Tree) bool {
			for _, n := range tr.Kind("Text") {
				if strings.HasPrefix(n.Text, "log: ") && strings.Contains(n.Text, want) {
					return true
				}
			}
			return false
		})
	}
	os.WriteFile(filepath.Join(dir, "a.txt"), []byte("one"), 0o644)
	os.WriteFile(filepath.Join(dir, "skip.tmp"), []byte("x"), 0o644)
	waitLog("create a.txt ;")
	os.WriteFile(filepath.Join(dir, "a.txt"), []byte("one two"), 0o644)
	waitLog("write a.txt ;")
	os.Rename(filepath.Join(dir, "a.txt"), filepath.Join(dir, "b.txt"))
	waitLog("rename b.txt a.txt;")
	os.Mkdir(filepath.Join(dir, "sub"), 0o755)
	waitLog("create sub ;")
	os.WriteFile(filepath.Join(dir, "sub", "c.txt"), []byte("c"), 0o644)
	waitLog("create sub/c.txt ;")
	os.Remove(filepath.Join(dir, "b.txt"))
	waitLog("remove b.txt ;")
	if s := logText(); strings.Contains(s, "skip.tmp") {
		t.Fatalf("an ignored file was reported: %s", s)
	}
	os.RemoveAll(dir)
	waitLog("remove . ;ended;")
}
