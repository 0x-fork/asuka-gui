package core_test

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"testing"

	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/lang/compile"
	"github.com/egoist/asuka/plugin"
	"github.com/egoist/asuka/std"
)

// The player's core as WebAssembly (no app compiled in), built once for
// the tests that run apps on it under Node (testdata/web/run.mjs).
var (
	playerOnce sync.Once
	playerWasm string
	playerErr  error
)

func webPlayer(t *testing.T) string {
	t.Helper()
	tc, err := cbuild.ForTarget(cbuild.Target{OS: "wasm", Arch: "wasm32"})
	if err != nil {
		t.Skip(err)
	}
	playerOnce.Do(func() {
		dir, err := os.MkdirTemp("", "asuka-web-player")
		if err != nil {
			playerErr = err
			return
		}
		root, _ := filepath.Abs(".")
		playerWasm = filepath.Join(dir, "asuka.wasm")
		playerErr = tc.Lib(root, cbuild.Module, nil, false, playerWasm)
	})
	if playerErr != nil {
		t.Fatal(playerErr)
	}
	return playerWasm
}

// runWebNode compiles the Asuka file src (testdata/web) with opts and
// runs it on the web host's core under Node, returning what it printed.
func runWebNode(t *testing.T, src string, opts *compile.Options, extra ...string) string {
	t.Helper()
	if testing.Short() {
		t.Skip("WebAssembly under Node (not -short)")
	}
	node, err := exec.LookPath("node")
	if err != nil {
		t.Skip("no Node")
	}
	wasm := webPlayer(t)
	b, err := os.ReadFile(filepath.Join("testdata", "web", src))
	if err != nil {
		t.Fatal(err)
	}
	res := compile.Files(map[string][]byte{src: b}, opts)
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	akb := filepath.Join(t.TempDir(), "app.akb")
	if err := os.WriteFile(akb, res.Module.Encode(), 0o644); err != nil {
		t.Fatal(err)
	}
	web, _ := filepath.Abs(filepath.Join("..", "platform", "web", "src"))
	cmd := exec.Command(node, append([]string{filepath.Join("testdata", "web", "run.mjs"), web, wasm, akb}, extra...)...)
	var stderr strings.Builder
	cmd.Stderr = &stderr
	out, err := cmd.Output()
	if err != nil {
		t.Fatalf("%v\n%s%s", err, out, stderr.String())
	}
	return string(out)
}

// spawn on the web runs on workers, each the same core with the same
// app: values and errors there and back, the app's files on both sides,
// several at once, a cancelled one.
func TestWebSpawn(t *testing.T) {
	got := runWebNode(t, "spawn.asuka", &compile.Options{Lang: "fr"})
	want := `500500
d3 3 2 -1 true 0
path 3 5 2
0 3
caught no to 2
not found nowhere.txt
from the page
from a worker
changed
false
worker.txt 13
55 210 465 820
first -1
15
(15 jobs on workers)
done
`
	if strings.TrimSpace(got) != strings.TrimSpace(want) {
		t.Fatalf("got:\n%s\nwant:\n%s", got, want)
	}
}

// asqliteWasm is the sqlite plugin's library for the web (its build, or
// its published file, downloaded once into the user's cache).
func asqliteWasm(t *testing.T) string {
	t.Helper()
	dir, ok := std.Package("asuka/sqlite")
	if !ok {
		t.Fatal("no asuka/sqlite")
	}
	data, err := os.ReadFile(filepath.Join(dir, plugin.ManifestFile))
	if err != nil {
		t.Fatal(err)
	}
	m, err := plugin.Parse(data)
	if err != nil {
		t.Fatal(err)
	}
	l, ok := m.Library("asqlite")
	if !ok {
		t.Fatal("no asqlite library")
	}
	p, err := plugin.FilePath(dir, l, "wasm")
	if err != nil {
		t.Skip(err)
	}
	return p
}

// asuka/sqlite on the web: the core's calls go to the page's SQLite
// worker and come back decoded; open and migrate run before what follows
// them, and a failed migration fails the next statement; a live query
// follows the writes; spawn's workers open the app's database at init
// harmlessly.
func TestWebSQLite(t *testing.T) {
	got := runWebNode(t, "sqlite.asuka", nil, asqliteWasm(t))
	want := `1 milk false
2 eggs true
count 2
1 true 2 2.5
2 false 0 -1
caught sqlite: no such table: nowhere (1)
caught sqlite: migration 2: sqlite: near "this": syntax error (1)
t 1 2
live 1 milk
live 2 bread
42
(1 jobs on workers)
done
`
	if strings.TrimSpace(got) != strings.TrimSpace(want) {
		t.Fatalf("got:\n%s\nwant:\n%s", got, want)
	}
}
