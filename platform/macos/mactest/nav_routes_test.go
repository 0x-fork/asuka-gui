//go:build darwin

package mactest

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// The app of platform/windows/testdata/routes.asuka: Home, then a message
// pushed.
const routesApp = `
	app { Routes() }

	component Routes {
	  Router(initial: "/home") {
	    Route("/home", layout: .stack) {
	      Home
	      Message
	    }
	  }
	}

	screen Home at "" {
	  title: "Home"
	  Column {
	    padding: 16; gap: 8
	    Text("The home screen")
	    Button("Open 7") { on press { navigate("/home/7") } }
	  }
	}

	screen Message(id: int) at ":id" {
	  title: "Message {id}"
	  Column {
	    padding: 16
	    Text("Showing message {id}")
	  }
	}
`

// shownText reports whether a view showing text is on screen.
func shownText(tr *Tree, text string) bool {
	for _, n := range tr.All() {
		if n.Shown && (n.Text == text || n.Title == text) {
			return true
		}
	}
	return false
}

// showing waits for the screen to show want and not other.
func showing(a *App, want, other string) {
	a.t.Helper()
	a.Until(func(tr *Tree) bool { return shownText(tr, want) && !shownText(tr, other) })
}

// A window reopens where it was left (docs/navigation.md, "Routes"): the
// app navigates to a message and quits; launched again, its router
// starts at the message (asuka_set_start, from asuka-routes.json), with
// home under it. A run under asuka dev or automation keeps no routes
// unless ASUKA_ROUTES=1 says so: without it, the app starts at its
// initial route.
func TestRouteReopens(t *testing.T) {
	data := t.TempDir()
	keep := Options{Env: []string{"ASUKA_ROUTES=1", "ASUKA_DATA=" + data}}
	a := RunWith(t, routesApp, keep)
	showing(a, "The home screen", "Showing message 7")
	a.Do("tap Open 7")
	showing(a, "Showing message 7", "The home screen")
	a.Stop()
	if b, err := os.ReadFile(filepath.Join(data, "asuka-routes.json")); err != nil || !strings.Contains(string(b), "/home/7") {
		t.Fatalf("asuka-routes.json: %s %v\n%s", b, err, a.Logs())
	}

	// Without ASUKA_ROUTES: the initial route (and nothing written).
	a = RunWith(t, routesApp, Options{Env: []string{"ASUKA_DATA=" + data}})
	showing(a, "The home screen", "Showing message 7")
	a.Stop()

	a = RunWith(t, routesApp, keep)
	showing(a, "Showing message 7", "The home screen")
	a.Do("back")
	showing(a, "The home screen", "Showing message 7")
}
