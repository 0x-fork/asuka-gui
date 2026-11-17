package windows_test

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// A window reopens where it was left (docs/navigation.md, "Routes"):
// the app navigates to a message and quits; launched again, its router
// starts at the message (asuka_set_start, from asuka-routes.json).
// Automation keeps no routes unless ASUKA_ROUTES=1 says so: without it,
// the app starts at its initial route.
func TestRouteReopens(t *testing.T) {
	w := onWindows(t)
	local, guest := w.job(t)
	src, err := os.ReadFile(filepath.Join("testdata", "routes.asuka"))
	if err != nil {
		t.Fatal(err)
	}
	buildApp(t, w, string(src), local)
	run := func(name string, keep bool, steps ...string) string {
		t.Helper()
		snap := guest + `\` + name + ".png"
		env := map[string]string{
			"ASUKA_DATA":     guest + `\data`,
			"ASUKA_DUMP":     "1",
			"ASUKA_SNAPSHOT": snap,
			"ASUKA_SCRIPT":   strings.Join(append(steps, "snap:"+snap), ";"),
		}
		if keep {
			env["ASUKA_ROUTES"] = "1"
		}
		out, err := w.run(t, guest, guest+`\app.exe`, env)
		if err != nil {
			t.Fatalf("%s: %v\n%s", name, err, out)
		}
		b, err := os.ReadFile(filepath.Join(local, name+".png.txt"))
		if err != nil {
			t.Fatalf("%s: no dump: %v\n%s", name, err, out)
		}
		return string(b)
	}
	first := run("first", true, "wait:1s", "tap:Open 7", "wait:1s")
	shows(t, first, "the first run", []string{"Showing message 7"})
	if b, _ := os.ReadFile(filepath.Join(local, "data", "asuka-routes.json")); !strings.Contains(string(b), "/home/7") {
		t.Fatalf("asuka-routes.json:\n%s", b)
	}
	again := run("again", true, "wait:1s")
	shows(t, again, "launched again", []string{"Showing message 7"}, "The home screen")
	// Automation alone keeps nothing: the initial route.
	fresh := run("fresh", false, "wait:1s")
	shows(t, fresh, "an automated run", []string{"The home screen"}, "Showing message 7")
}
