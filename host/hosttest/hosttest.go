// Package hosttest runs Asuka apps on a desktop host in its own tests, as
// the host runs them: the C core (built once for this computer, as a
// library, and loaded through host/bridge), an app compiled from Asuka
// source (lang/compile), and the host under test, in the test's process.
//
//	h := newHost()
//	core := hosttest.Mount(t, h, onMain, `
//	  component Counter { state n = 0; Button("Add {n}") { on press { n += 1 } } }
//	  app { Counter() }
//	`)
//
// The host's views are its own to inspect and drive (a click on a
// button, its title after); the core sends the host what changed as an
// app does, and the test lets the app go when it ends.
package hosttest

import (
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"

	"github.com/egoist/asuka/host"
	"github.com/egoist/asuka/host/bridge"
	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/lang/compile"
)

var (
	once    sync.Once
	core    *bridge.Core
	coreErr error
	libDir  string
)

// Core is the core library, built for this computer (its objects cached
// across runs) and opened once per process.
func Core(t testing.TB) *bridge.Core {
	t.Helper()
	once.Do(func() {
		tc, err := cbuild.ForTarget(cbuild.Host())
		if err != nil {
			coreErr = err
			return
		}
		root, err := cbuild.CoreDir()
		if err != nil {
			coreErr = err
			return
		}
		libDir, err = os.MkdirTemp("", "asuka-hosttest-")
		if err != nil {
			coreErr = err
			return
		}
		lib := filepath.Join(libDir, bridge.LibName())
		if coreErr = tc.Lib(root, cbuild.Shared, nil, false, lib); coreErr != nil {
			return
		}
		core, coreErr = bridge.Open(lib)
	})
	if coreErr != nil {
		t.Skipf("the C core for this computer: %v", coreErr)
	}
	return core
}

// Compile compiles src, an Asuka file (with its app { }), to a module.
func Compile(t testing.TB, src string) []byte {
	t.Helper()
	res := compile.Files(map[string][]byte{"app.asuka": []byte(dedent(src))}, nil)
	if err := res.Err(); err != nil {
		t.Fatalf("the app doesn't compile:\n%v", err)
	}
	return res.Module.Encode()
}

// Mount starts the core for h (on the main thread, through onMain) and
// runs src's app on it; the app goes when the test ends, before the
// test's own cleanups (which close the host's window) run.
func Mount(t testing.TB, h host.Host, onMain func(func()), src string) *bridge.Core {
	t.Helper()
	m := Compile(t, src)
	c := Core(t)
	var err error
	onMain(func() {
		c.Start(h)
		err = c.Load(m)
	})
	if err != nil {
		t.Fatalf("loading the app: %v", err)
	}
	t.Cleanup(func() { onMain(c.Unload) })
	return c
}

// dedent takes the indentation common to src's lines away, so a test's
// app can be written in a raw string indented with the test.
func dedent(src string) string {
	lines := strings.Split(src, "\n")
	indent := -1
	for _, l := range lines {
		if strings.TrimSpace(l) == "" {
			continue
		}
		n := len(l) - len(strings.TrimLeft(l, " \t"))
		if indent < 0 || n < indent {
			indent = n
		}
	}
	for i, l := range lines {
		if len(l) >= indent && indent > 0 {
			lines[i] = l[indent:]
		}
	}
	return strings.Join(lines, "\n")
}
