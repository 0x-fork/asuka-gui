package appbuild_test

import (
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/internal/appbuild/apptest"
	"github.com/egoist/asuka/internal/cbuild"
)

func needCC(t *testing.T) {
	t.Helper()
	if _, err := cbuild.Find(); err != nil {
		t.Skip(err)
	}
}

// A module's tests run on the fake host: each reported, a failure with
// where its test block is.
func TestRunTests(t *testing.T) {
	needCC(t)
	m := apptest.Counter("Count: ")
	apptest.Failing(m, "Count: ")
	var out strings.Builder
	s, err := appbuild.RunTests(m, appbuild.TestOptions{}, &out)
	if err != nil {
		t.Fatalf("%v\n%s", err, out.String())
	}
	if len(s.Results) != 2 || !s.Results[0].OK || s.Results[1].OK || s.Failed() != 1 {
		t.Fatalf("results %+v\n%s", s.Results, out.String())
	}
	want := "ok   Add counts\nFAIL shows seven (app.asuka:18)\n     the screen doesn't show \"Count: 7\""
	if !strings.HasPrefix(out.String(), want) {
		t.Fatalf("got:\n%s\nwant it to start:\n%s", out.String(), want)
	}
	// A pattern picks tests.
	out.Reset()
	s, err = appbuild.RunTests(m, appbuild.TestOptions{Pattern: "Add", Platform: "android"}, &out)
	if err != nil || len(s.Results) != 1 || s.Failed() != 0 {
		t.Fatalf("%v %+v\n%s", err, s, out.String())
	}
}

// ShowApp prints what the app's screen shows.
func TestShowApp(t *testing.T) {
	needCC(t)
	var out strings.Builder
	if err := appbuild.ShowApp(apptest.Counter("Total: "), &out); err != nil {
		t.Fatalf("%v\n%s", err, out.String())
	}
	if !strings.Contains(out.String(), `Text 0,0 390x20 "Total: 0"`) || !strings.Contains(out.String(), `"Add"`) {
		t.Fatalf("the screen:\n%s", out.String())
	}
}

// Without the compiler, Compile says so.
func TestNoCompiler(t *testing.T) {
	p, err := appbuild.Load(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	if _, _, err := p.Compile(); err != appbuild.ErrNoCompiler {
		t.Fatalf("err %v", err)
	}
}

// A module without its tests keeps what its app and init reach, and
// calls none of the test builtins; it runs as before.
func TestStripTests(t *testing.T) {
	needCC(t)
	m := apptest.Counter("Count: ")
	apptest.Failing(m, "Count: ")
	s := appbuild.StripTests(m)
	if len(s.Tests) != 0 || len(s.Funcs) != len(m.Funcs)-2 || len(m.Tests) != 2 {
		t.Fatalf("funcs %d of %d, tests %d", len(s.Funcs), len(m.Funcs), len(s.Tests))
	}
	for _, b := range s.Builtins {
		if strings.HasPrefix(b, "test.") {
			t.Errorf("calls %s", b)
		}
	}
	if s.Funcs[s.App].Name != "Counter" || s.Funcs[s.Init].Name != "init" {
		t.Errorf("app %s, init %s", s.Funcs[s.App].Name, s.Funcs[s.Init].Name)
	}
	var out strings.Builder
	if err := appbuild.ShowApp(s, &out); err != nil || !strings.Contains(out.String(), `"Count: 0"`) {
		t.Fatalf("%v\n%s", err, out.String())
	}
}
