//go:build darwin

package mactest

import (
	"encoding/json"
	"fmt"
	"image/png"
	"math"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

type shellApp struct {
	Name string `json:"name"`
	ID   string `json:"id"`
	Path string `json:"path"`
}

func shellApps(t *testing.T, s string) []shellApp {
	t.Helper()
	b, ok := strings.CutPrefix(s, "ok [")
	if !ok {
		t.Fatalf("apps.list: %s", s)
	}
	var list []shellApp
	if err := json.Unmarshal([]byte(strings.TrimSuffix(b, "]")), &list); err != nil {
		t.Fatalf("apps.list: %v: %s", err, b)
	}
	return list
}

// The list has the system's apps, named and with their bundle IDs, and
// Finder; each once.
func TestShellAppsList(t *testing.T) {
	a := Run(t, sysCallSrc+`app { Sys("apps", "list", bytes()) { label: "list"; text: true } }`)
	list := shellApps(t, sysText(t, a, "list"))
	byID := map[string]shellApp{}
	paths := map[string]bool{}
	for _, x := range list {
		if x.Name == "" || x.ID == "" || x.Path == "" || strings.HasSuffix(x.Name, ".app") {
			t.Errorf("an app %+v", x)
		}
		if paths[x.Path] {
			t.Errorf("%s twice", x.Path)
		}
		paths[x.Path] = true
		byID[x.ID] = x
	}
	for id, path := range map[string]string{
		"com.apple.calculator": "/System/Applications/Calculator.app",
		"com.apple.TextEdit":   "/System/Applications/TextEdit.app",
		"com.apple.finder":     "/System/Library/CoreServices/Finder.app",
		"com.apple.Terminal":   "/System/Applications/Utilities/Terminal.app", // a folder down
	} {
		if x, ok := byID[id]; !ok || x.Path != path {
			t.Errorf("%s: %+v, want at %s", id, x, path)
		}
	}
	// Safari is a link in /Applications to the system's cryptex: listed
	// once, at the link.
	if x, ok := byID["com.apple.Safari"]; ok && x.Path != "/Applications/Safari.app" {
		t.Errorf("Safari at %s", x.Path)
	}
	// Sorted by name, case aside.
	if !slices.IsSortedFunc(list, func(a, b shellApp) int { return strings.Compare(strings.ToLower(a.Name), strings.ToLower(b.Name)) }) {
		t.Log("not sorted by name (accents may order them otherwise)")
	}
}

// A folder of the test's: apps one level down, not inside a bundle, nor in
// a hidden folder; one without a bundle ID is its path; a link to one it
// has is left out.
func TestShellAppsDirs(t *testing.T) {
	dir, _ := filepath.EvalSymlinks(t.TempDir())
	mkApp := func(p, id string) {
		os.MkdirAll(filepath.Join(p, "Contents", "MacOS"), 0o755)
		plist := `<?xml version="1.0" encoding="UTF-8"?><plist version="1.0"><dict><key>CFBundleName</key><string>` +
			strings.TrimSuffix(filepath.Base(p), ".app") + `</string>`
		if id != "" {
			plist += `<key>CFBundleIdentifier</key><string>` + id + `</string>`
		}
		os.WriteFile(filepath.Join(p, "Contents", "Info.plist"), []byte(plist+`</dict></plist>`), 0o644)
	}
	mkApp(filepath.Join(dir, "One.app"), "test.asuka.one")
	mkApp(filepath.Join(dir, "Folder", "Two.app"), "test.asuka.two")
	mkApp(filepath.Join(dir, "One.app", "Contents", "Helper.app"), "test.asuka.helper")
	mkApp(filepath.Join(dir, "Folder", "Deeper", "Three.app"), "test.asuka.three")
	mkApp(filepath.Join(dir, ".Hidden", "Four.app"), "test.asuka.four")
	mkApp(filepath.Join(dir, "NoID.app"), "")
	os.Symlink(filepath.Join(dir, "One.app"), filepath.Join(dir, "Folder", "Link.app"))
	a := RunWith(t, sysCallSrc+`app { Sys("apps", "list", bytes()) { label: "list"; text: true } }`,
		Options{Env: []string{"ASUKA_APP_DIRS=" + dir}})
	var names []string
	for _, x := range shellApps(t, sysText(t, a, "list")) {
		if x.Path == "/System/Library/CoreServices/Finder.app" {
			continue
		}
		names = append(names, fmt.Sprintf("%s %s %s", x.Name, x.ID, strings.TrimPrefix(x.Path, dir)))
	}
	slices.Sort(names)
	want := []string{
		"NoID " + filepath.Join(dir, "NoID.app") + " /NoID.app",
		"One test.asuka.one /One.app",
		"Two test.asuka.two /Folder/Two.app",
	}
	if strings.Join(names, "\n") != strings.Join(want, "\n") {
		t.Fatalf("the list:\n%s\nwant\n%s", strings.Join(names, "\n"), strings.Join(want, "\n"))
	}
}

// icon writes a PNG of the size at the main display's scale in the caches,
// kept and used again; a missing app fails, as does opening one.
func TestShellAppsIcon(t *testing.T) {
	cache, _ := filepath.EvalSymlinks(t.TempDir())
	a := RunWith(t, sysCallSrc+`
		fn args(path: string, size: float) -> bytes = "\{\"path\": \"{path}\", \"size\": {size}}".bytes()
		state again = false
		app {
		  Column {
		    Button("Again") { on press { again = true } }
		    Sys("apps", "icon", args("/System/Applications/Calculator.app", 32.0)) { label: "32"; text: true }
		    Sys("apps", "icon", args("/System/Applications/Calculator.app", 17.5)) { label: "17"; text: true }
		    if again { Sys("apps", "icon", args("/System/Applications/Calculator.app", 32.0)) { label: "again"; text: true } }
		    Sys("apps", "icon", args("/nowhere/None.app", 32.0)) { label: "missing"; text: true }
		    Sys("apps", "open", "/nowhere/None.app".bytes()) { label: "open"; text: true }
		    Sys("apps", "icon", "nope".bytes()) { label: "bad"; text: true }
		  }
		}
	`, Options{Env: []string{"ASUKA_CACHE=" + cache}})
	scale := 2.0
	if s, ok := shellJSON(t, a.Tree())["environment"].(map[string]any)["scale"].(float64); ok && s > 0 {
		scale = s
	}
	first := ""
	for _, c := range []struct {
		label string
		size  float64
	}{{"32", 32}, {"17", 17.5}} {
		s := sysText(t, a, c.label)
		p, ok := strings.CutPrefix(s, "ok [")
		p = strings.TrimSuffix(p, "]")
		if !ok || !strings.HasPrefix(p, cache) {
			t.Fatalf("icon %s: %s, not in the caches %s", c.label, s, cache)
		}
		f, err := os.Open(p)
		if err != nil {
			t.Fatal(err)
		}
		img, err := png.Decode(f)
		f.Close()
		if err != nil {
			t.Fatal(err)
		}
		px := int(math.Ceil(c.size * scale))
		if b := img.Bounds(); b.Dx() != px || b.Dy() != px {
			t.Logf("a %v point icon is %v pixels, want %d (scale %v: the main display's may differ)", c.size, b, px, scale)
		}
		if _, _, _, al := img.At(img.Bounds().Dx()/2, img.Bounds().Dy()/2).RGBA(); al == 0 {
			t.Fatal("the icon's middle is transparent")
		}
		if c.label == "32" {
			first = p
		}
	}
	st, _ := os.Stat(first)
	a.Do("tap Again")
	if s := sysText(t, a, "again"); s != "ok ["+first+"]" {
		t.Fatalf("again: %s, want %s", s, first)
	}
	if st2, _ := os.Stat(first); !st2.ModTime().Equal(st.ModTime()) {
		t.Fatal("drawn again")
	}
	sysWant(t, a, "missing", "err no app at /nowhere/None.app")
	sysWant(t, a, "open", "err no app at /nowhere/None.app")
	sysWant(t, a, "bad", "err apps.icon: bad arguments")
}

// search ranks names as a launcher does: exact, prefix, a word's start or
// the words' starts, anywhere, the letters in order; case and accents
// aside.
func TestShellAppsSearch(t *testing.T) {
	names := []string{"Visual Studio Code", "Calculator", "Xcode", "TextEdit", "Café", "Google Chrome", "Cal"}
	nj, _ := json.Marshal(names)
	cases := map[string]string{
		"cal":  "[6,1]",
		"vsc":  "[0]",
		"edit": "[3]",
		"ode":  "[0,2]",
		"cafe": "[4]",
		"gc":   "[5]",
		"clc":  "[1]",
		"zzz":  "[]",
		"":     "[0,1,2,3,4,5,6]",
	}
	var src strings.Builder
	src.WriteString(sysCallSrc + "app {\n Column {\n")
	i := 0
	var labels []string
	for q := range cases {
		args, _ := json.Marshal(map[string]any{"names": json.RawMessage(nj), "query": q})
		l := fmt.Sprintf("q%d", i)
		labels = append(labels, q)
		fmt.Fprintf(&src, "Sys(\"apps\", \"search\", %s.bytes()) { label: \"%s\"; text: true }\n", shellString(string(args)), l)
		i++
	}
	src.WriteString("}\n}\n")
	a := Run(t, src.String())
	for i, q := range labels {
		got := strings.ReplaceAll(sysText(t, a, fmt.Sprintf("q%d", i)), " ", "")
		if got != "ok["+cases[q]+"]" {
			t.Errorf("search %q: %s, want %s", q, got, cases[q])
		}
	}
}

// shellString is s as an Asuka string literal.
func shellString(s string) string {
	r := strings.NewReplacer(`\`, `\\`, `"`, `\"`, "{", `\{`, "\n", `\n`)
	return `"` + r.Replace(s) + `"`
}
