//go:build darwin

package mactest

import (
	"fmt"
	"image"
	"image/png"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"testing"
)

// The tests of languages and direction: the foundation's Locale.swift,
// and what reads a way in the list and text parts. An app reads right to
// left from its launch with ASUKA_DIRECTION (AppKit too: asuka dev --dir
// rtl), or changes language as it runs (i18n.setLocale, AppKit's
// direction staying as it launched).

const localePlayerID = "dev.asuka.mactest.locale"

var (
	localeOnce sync.Once
	localeExe  string
	localeErr  error
)

// localePlayer is a copy of the player with a bundle ID of its own, so the
// language a test saves (setLocale keeps it in the app's defaults, where
// AppKit reads it at launch) is never another test's app's.
func localePlayer(t testing.TB) string {
	t.Helper()
	exe := Player(t)
	localeOnce.Do(func() {
		app := filepath.Dir(filepath.Dir(filepath.Dir(exe)))
		dir, err := os.MkdirTemp("", "asuka-mactest-locale-")
		if err != nil {
			localeErr = err
			return
		}
		cp := filepath.Join(dir, filepath.Base(app))
		for _, c := range [][]string{
			{"ditto", app, cp},
			{"plutil", "-replace", "CFBundleIdentifier", "-string", localePlayerID, filepath.Join(cp, "Contents", "Info.plist")},
			{"codesign", "--force", "--deep", "-s", "-", cp},
		} {
			if b, err := exec.Command(c[0], c[1:]...).CombinedOutput(); err != nil {
				localeErr = fmt.Errorf("%s: %v\n%s", c[0], err, b)
				return
			}
		}
		localeExe = filepath.Join(cp, "Contents", "MacOS", filepath.Base(exe))
	})
	if localeErr != nil {
		t.Fatal(localeErr)
	}
	exec.Command("defaults", "delete", localePlayerID, "AppleLanguages").Run()
	t.Cleanup(func() { exec.Command("defaults", "delete", localePlayerID, "AppleLanguages").Run() })
	return localeExe
}

// runRTL runs src reading right to left from its launch.
func runRTL(t *testing.T, src string) *App {
	t.Helper()
	return RunWith(t, src, Options{Env: []string{"ASUKA_DIRECTION=rtl"}})
}

// runLive runs src, which changes its language with the buttons of
// langButtons, on the locale player.
func runLive(t *testing.T, src string) *App {
	t.Helper()
	return RunWith(t, "import asuka/i18n\n"+Dedent(src)+langButtons, Options{Player: localePlayer(t)})
}

// langButtons are buttons that show the app in English and in Arabic (the
// languages it says it's written in), and as the system's.
const langButtons = `
component Lang {
  Row {
    Button("Arabic") { on press { i18n.setLanguages(["en", "ar"]); i18n.setLocale("ar") } }
    Button("English") { on press { i18n.setLanguages(["en", "ar"]); i18n.setLocale("en") } }
    Text("locale {i18n.locale()}")
  }
}
`

// label is a Text's "label" JSON.
func label(n *Node) map[string]any {
	m, _ := n.Get("label").(map[string]any)
	return m
}

func rtl(n *Node) bool { return n.Bool("rtl") }

// A Row's children are placed from the right, in physical frames the host
// doesn't touch, and a text's start is the right; its writing direction
// is its own (TestLocaleRowAndText).
func TestLocaleRowAndText(t *testing.T) {
	// The language changes as it runs: launched reading right to left
	// (ASUKA_DIRECTION), the core lays its first frames out left to right
	// (its root's box takes the direction only as it changes).
	a := runLive(t, `
		app {
		  Column {
		    padding: 10
		    Lang()
		    Row {
		      gap: 8
		      Button("One") {}
		      Button("Two") {}
		    }
		    Text("start")
		    Text("end") { textAlign: .end }
		    Text("center") { textAlign: .center }
		  }
		}
	`)
	a.UntilText("locale en")
	a.Do("tap Arabic")
	tr := a.Until(func(tr *Tree) bool {
		n := tr.FindText("start")
		return n != nil && rtl(n) && tr.FindText("locale ar") != nil
	})
	one, two := tr.Find("Button", "One"), tr.Find("Button", "Two")
	if two.Frame[0]+two.Frame[2] > one.Frame[0] {
		t.Fatalf("the second button at %v isn't left of the first at %v:\n%s", two.Frame, one.Frame, a.Dump())
	}
	width := tr.Main().Content[2]
	if right := one.InWindow[0] + one.InWindow[2]; right != width-10 {
		t.Fatalf("the first button ends at %v of a %v wide window", right, width)
	}
	start := tr.FindText("start")
	if !rtl(start) || !rtl(one) {
		t.Fatal("views aren't right to left")
	}
	if l := label(start); l["alignment"] != "right" || l["writingDirection"] != -1.0 {
		t.Fatalf("a text's start: %v", l)
	}
	if l := label(tr.FindText("end")); l["alignment"] != "left" {
		t.Fatalf("a text's end: %v", l)
	}
	if l := label(tr.FindText("center")); l["alignment"] != "center" {
		t.Fatalf("a centered text: %v", l)
	}
}

// layoutDirection's views read its way in an app that reads the other, and
// what's beside them is the app's; a list made in it has its columns from
// the left (TestLayoutDirectionProp).
func TestLocaleLayoutDirectionProp(t *testing.T) {
	a := runRTL(t, `
		app {
		  Column {
		    Text("app")
		    Row {
		      layoutDirection: .leftToRight
		      gap: 8
		      Text("first")
		      Text("second")
		      Symbol("chevron.forward")
		      List {
		        columns: 2
		        width: 100
		        height: 40
		        for i in [1, 2] key i { Text("cell {i}") }
		      }
		    }
		  }
		}
	`)
	tr := a.UntilText("cell 2")
	app, first, second := tr.FindText("app"), tr.FindText("first"), tr.FindText("second")
	if rtl(first) || !rtl(app) {
		t.Fatalf("the override's text is RTL %v, the app's %v", rtl(first), rtl(app))
	}
	if first.Frame[0] >= second.Frame[0] {
		t.Fatal("the override's row runs from the right")
	}
	if l := label(first); l["alignment"] != "left" || l["writingDirection"] != 0.0 {
		t.Fatalf("the override's text: %v", l)
	}
	if rtl(tr.Kind("Symbol")[0]) {
		t.Fatal("the override's symbol is RTL")
	}
	l, _ := listOf(t, tr)
	var row0 []*Node
	for _, c := range l.Children {
		if c.Kind == "Cell" && c.Row == 0 {
			row0 = append(row0, c)
		}
	}
	if len(row0) != 2 || row0[0].Frame[0] != 0 || rtl(row0[0]) {
		t.Fatalf("the override's list isn't left to right: %v", row0)
	}
}

// A layoutDirection says the writing direction of its text and its rich
// text, whichever way the app reads; without one, it's the text's
// (TestWritingDirection).
func TestLocaleWritingDirection(t *testing.T) {
	a := runLive(t, `
		app {
		  Column {
		    Lang()
		    Text("app")
		    RichText([Span(text: "rich app")])
		    Column {
		      layoutDirection: .rightToLeft
		      Text("right")
		      RichText([Span(text: "rich right")])
		    }
		    Column {
		      layoutDirection: .leftToRight
		      Text("left")
		      RichText([Span(text: "rich left")])
		    }
		  }
		}
	`)
	check := func(locale string) {
		t.Helper()
		tr := a.UntilText("locale " + locale)
		for text, want := range map[string]float64{"app": -1, "right": 1, "left": 0} {
			if d := label(tr.FindText(text))["writingDirection"]; d != want {
				t.Fatalf("%s: %q has writing direction %v", locale, text, d)
			}
			_, r := richOf(tr, "rich "+text)
			if r == nil || r["writingDirection"] != want {
				t.Fatalf("%s: the rich text in %q has writing direction %v", locale, text, r["writingDirection"])
			}
		}
	}
	check("en")
	a.Do("tap Arabic")
	check("ar")
}

// A RichText reading right to left: its paragraphs start at the right,
// their writing direction their own, drawn at the view's right; its link,
// at the left of a right to left line, still takes a click (TestRichTextRTL).
func TestLocaleRichTextRTL(t *testing.T) {
	a := runRTL(t, `
		component R {
		  state linked = ""
		  Column {
		    padding: 10
		    Text("linked {linked}")
		    RichText([Span(text: "مرحبا "), Span(text: "link", link: "https://example.com")]) {
		      width: 300
		      on link(url) { linked = url }
		    }
		  }
		}
		app { R() }
	`)
	tr := a.Until(func(tr *Tree) bool { n, _ := richOf(tr, "مرحبا link"); return n != nil })
	n, r := richOf(tr, "مرحبا link")
	if r["alignment"] != "right" || r["writingDirection"] != -1.0 || !rtl(n) {
		t.Fatalf("the rich text: %v (rtl %v)", r, rtl(n))
	}
	used := nums(r, "used")
	if n.Frame[2] != 300 || used[0]+used[2] < 299 {
		t.Fatalf("the text is at %v in a %v wide view", used, n.Frame[2])
	}
	a.Do("link https://example.com")
	a.UntilText("linked https://example.com")
}

// scrollState is a Scroll's "scroll" JSON: its content's frame and what
// it shows.
func scrollState(n *Node) (doc, clip []float64) {
	m, _ := n.Get("scroll").(map[string]any)
	return nums(m, "doc"), nums(m, "clip")
}

// A horizontal Scroll reading right to left starts at its end, keeps its
// distance from the end as its content changes, and its offsets are
// physical; a language reading the other way mirrors its offset
// (TestHorizontalScrollRTL).
func TestLocaleHorizontalScrollRTL(t *testing.T) {
	a := runLive(t, `
		component S {
		  state count = 30
		  state x = -1.0
		  Column {
		    Lang()
		    Text("x {x}")
		    Button("More") { on press { count = 40 } }
		    Button("Fewer") { on press { count = 20 } }
		    Scroll {
		      horizontal: true
		      height: 40
		      width: 300
		      gap: 4
		      on scroll(sx, sy) { x = sx }
		      for i in 0..<count key i { Text("item {i}") }
		    }
		  }
		}
		app { S() }
	`)
	a.UntilText("locale en")
	a.Do("tap Arabic")
	a.UntilText("locale ar")
	scroll := func(tr *Tree) *Node { return tr.Kind("Scroll")[0] }
	tr := a.Until(func(tr *Tree) bool {
		doc, clip := scrollState(scroll(tr))
		return len(doc) == 4 && rtl(scroll(tr)) && doc[2] > clip[2] && clip[0] == doc[2]-clip[2]
	})
	doc, clip := scrollState(scroll(tr))
	if doc[0] != 0 {
		t.Fatalf("the content is at %v", doc)
	}
	if f := tr.FindText("item 0").Frame; f[0]+f[2] != doc[2] {
		t.Fatalf("the first item is at %v of %v", f, doc[2])
	}
	// 50 from the end: offsets are physical.
	x := doc[2] - clip[2] - 50
	a.Do(fmt.Sprintf("scrollx %v", x))
	a.Until(func(tr *Tree) bool { _, c := scrollState(scroll(tr)); return c[0] == x })
	// Reported physically: the host's offset is what the app hears.
	a.Until(func(tr *Tree) bool {
		var seen float64
		fmt.Sscanf(findPrefix(tr, "x "), "x %g", &seen)
		return seen == x
	})
	// The content grows, then shrinks: what shows stays, the same distance
	// from the end.
	for _, b := range []string{"More", "Fewer"} {
		a.Do("tap " + b)
		a.Until(func(tr *Tree) bool {
			d, c := scrollState(scroll(tr))
			return d[2] != doc[2] && c[0] == d[2]-c[2]-50
		})
		doc, _ = scrollState(scroll(a.Tree()))
	}
	// Another direction mirrors the offset; back again, it's as far from
	// the end as it was.
	a.Do("tap English")
	a.Until(func(tr *Tree) bool { _, c := scrollState(scroll(tr)); return !rtl(scroll(tr)) && c[0] == 50 })
	a.Do("tap Arabic")
	a.Until(func(tr *Tree) bool {
		d, c := scrollState(scroll(tr))
		return rtl(scroll(tr)) && d[2]-c[2]-c[0] == 50
	})
}

// Short content hugs the right, at the scroll view's origin; a left to
// right scroll in a right to left app stays at its start
// (TestHorizontalScrollShort).
func TestLocaleHorizontalScrollShort(t *testing.T) {
	a := runRTL(t, `
		app {
		  Column {
		    Scroll { horizontal: true; height: 40; width: 300; Text("short") }
		    Scroll {
		      layoutDirection: .leftToRight
		      horizontal: true
		      height: 40
		      width: 100
		      Text("a line wider than its scroll view, by a good way")
		    }
		  }
		}
	`)
	tr := a.UntilText("short")
	for _, n := range tr.Kind("Scroll") {
		doc, clip := scrollState(n)
		if !rtl(n) {
			if clip[0] != 0 || doc[0] != 0 {
				t.Fatalf("a left to right scroll at %v, content %v", clip, doc)
			}
			continue
		}
		if clip[0] != 0 || doc[0] != 0 || doc[2] != clip[2] {
			t.Fatalf("short content %v in %v", doc, clip)
		}
		if f := tr.FindText("short").Frame; f[0]+f[2] != doc[2] {
			t.Fatalf("the short text is at %v of %v", f, doc[2])
		}
	}
}

// A grid's columns run from the right; going left to right, the rows are
// placed again (TestListGridRTL).
func TestLocaleListGridRTL(t *testing.T) {
	a := runLive(t, `
		app {
		  Column {
		    grow: 1
		    Lang()
		    List {
		      grow: 1
		      columns: 3
		      gap: 10
		      for i in [1, 2, 3, 4] key i { Text("cell {i}") { height: 30 } }
		    }
		  }
		}
	`)
	a.UntilText("cell 4")
	a.Do("tap Arabic")
	xs := func(tr *Tree, row int) []float64 {
		l, _ := listOf(t, tr)
		var out []float64
		for _, c := range l.Children {
			if c.Kind == "Cell" && c.Row == row && !c.Hidden {
				out = append(out, c.Frame[0])
			}
		}
		return out
	}
	tr := a.Until(func(tr *Tree) bool {
		_, m := listOf(t, tr)
		cw := (num(m, "width") - 20) / 3
		r0, r1 := xs(tr, 0), xs(tr, 1)
		return len(r0) == 3 && r0[0] == 2*(cw+10) && r0[1] == cw+10 && r0[2] == 0 && len(r1) == 1 && r1[0] == 2*(cw+10)
	})
	_ = tr
	a.Do("tap English")
	a.Until(func(tr *Tree) bool { r0 := xs(tr, 0); return len(r0) == 3 && r0[0] == 0 })
}

// The language changes as the app runs: what follows it does (a text's
// direction and start, a row's order), and the host's own words.
func TestLocaleLive(t *testing.T) {
	a := runLive(t, `
		app {
		  Column {
		    padding: 16
		    gap: 8
		    Lang()
		    Text("language {i18n.locale()}")
		    Row {
		      gap: 8
		      Button("One") {}
		      Button("Two") {}
		    }
		    Slider { value: 0.25; width: 200 }
		  }
		}
	`)
	tr := a.UntilText("language en")
	if rtl(tr.FindText("language en")) {
		t.Fatal("starts right to left")
	}
	a.Do("tap Arabic")
	tr = a.Until(func(tr *Tree) bool {
		n := tr.FindText("language ar")
		return n != nil && rtl(n) && label(n)["alignment"] == "right"
	})
	one, two := tr.Find("Button", "One"), tr.Find("Button", "Two")
	if two.Frame[0] >= one.Frame[0] || !rtl(tr.Kind("Slider")[0]) {
		t.Fatalf("the row didn't mirror: %v, %v", one.Frame, two.Frame)
	}
	if env := tr.Environment; env["locale"] == nil {
		t.Logf("environment %v", env)
	}
	a.Do("tap English")
	a.Until(func(tr *Tree) bool {
		n := tr.FindText("language en")
		return n != nil && !rtl(n) && label(n)["alignment"] == "left" && label(n)["writingDirection"] == -1.0
	})
}

// Each control reads the app's way, as the view it is; a pop-up's menu
// too, and a field's text, a multiline one's as the app sets it
// (TestControlsMirror).
func TestLocaleControlsMirror(t *testing.T) {
	a := runRTL(t, `
		component C {
		  state on = true
		  state value = 0.25
		  state seg = 0
		  state notes = ""
		  Column {
		    gap: 8
		    padding: 12
		    Toggle { bind on: on }
		    Slider { bind value: value }
		    Segmented(["One", "Two", "Three"]) { bind selected: seg }
		    Stepper { bind value: value; max: 10 }
		    Picker(["First", "Second"]) { bind selected: seg }
		    Progress { value: value }
		    Button("Icon") { icon: "trash" }
		    TextField { placeholder: "Placeholder" }
		    TextField { bind text: notes; multiline: true; placeholder: "Notes" }
		    Button("Write") { on press { notes = "line" } }
		  }
		}
		app { C() }
	`)
	tr := a.UntilText("Write")
	for _, k := range []string{"Toggle", "Slider", "Segmented", "Stepper", "Picker", "Progress", "Button", "TextField"} {
		ns := tr.Kind(k)
		if len(ns) == 0 {
			t.Fatalf("no %s:\n%s", k, tr.Raw())
		}
		for _, n := range ns {
			if !rtl(n) {
				t.Fatalf("a %s isn't right to left", k)
			}
		}
	}
	for _, k := range []string{"Picker"} {
		if tr.Kind(k)[0].Get("menuRTL") != true {
			t.Fatalf("the %s's menu isn't right to left", k)
		}
	}
	for _, p := range []string{"Placeholder", "Notes"} {
		if _, f := fieldOf(t, tr, p); f["alignment"] != "right" || f["writingDirection"] != -1.0 {
			t.Fatalf("the field %s: %v", p, f)
		}
	}
	a.Do("tap Write")
	a.Until(func(tr *Tree) bool {
		_, f := fieldOf(t, tr, "Notes")
		return f["string"] == "line" && f["alignment"] == "right"
	})
}

// A vertical Scroll's scroller is on the side the view ends, where AppKit
// reads that way (TestScrollerSide).
func TestLocaleScrollerSide(t *testing.T) {
	a := runRTL(t, `
		app {
		  Scroll {
		    width: 200
		    height: 100
		    View { height: 1000; Text("tall") }
		  }
		}
	`)
	tr := a.UntilText("tall")
	m, _ := tr.Kind("Scroll")[0].Get("scroll").(map[string]any)
	f := nums(m, "verticalScroller")
	if len(f) != 4 || f[0]+f[2]/2 >= 100 {
		t.Fatalf("the scroller is at %v in a 200 wide view, reading right to left", f)
	}
}

// The environment reports the user's languages and formats: the first
// weekday and the hour cycle (TestEnvironmentLanguages; the hour cycle of
// a pattern: KeysTests' asukaHourCycle).
func TestLocaleEnvironment(t *testing.T) {
	a := Run(t, `
		import asuka/i18n
		app {
		  Text("langs {i18n.preferredLanguages().count()} fw {i18n.region().firstWeekday} hc {i18n.region().hourCycle}")
		}
	`)
	tr := a.Until(func(tr *Tree) bool { return len(tr.Kind("Text")) == 1 })
	var langs, fw, hc int
	if _, err := fmt.Sscanf(tr.Kind("Text")[0].Text, "langs %d fw %d hc %d", &langs, &fw, &hc); err != nil {
		t.Fatal(err)
	}
	if langs == 0 || fw < 1 || fw > 7 || hc != 12 && hc != 24 {
		t.Fatalf("languages %d, first weekday %d, hour cycle %d", langs, fw, hc)
	}
}

// A saved language comes first in what's reported, with the system's after
// it, and removing it gives the system's back in this session; AppKit
// reads it from the app's defaults at its next launch (TestSaveLocale).
func TestLocaleSave(t *testing.T) {
	a := runLive(t, `
		app {
		  Column {
		    Lang()
		    Text("preferred {i18n.preferredLanguages().joined(separator: ",")}")
		    Button("German") { on press { i18n.setLanguages(["en", "de"]); i18n.setLocale("de") } }
		    Button("System") { on press { i18n.setLocale("") } }
		  }
		}
	`)
	tr := a.Until(func(tr *Tree) bool { return findPrefix(tr, "preferred ") != "" })
	system := strings.TrimPrefix(findPrefix(tr, "preferred "), "preferred ")
	a.Do("tap German")
	tr = a.Until(func(tr *Tree) bool { return strings.HasPrefix(findPrefix(tr, "preferred "), "preferred de") })
	if got := strings.TrimPrefix(findPrefix(tr, "preferred "), "preferred "); got != "de,"+system && got != "de" && !strings.HasPrefix(got, "de,") {
		t.Fatalf("saved de: %q, the system's %q", got, system)
	}
	out, _ := exec.Command("defaults", "read", localePlayerID, "AppleLanguages").CombinedOutput()
	if !strings.Contains(string(out), "de") {
		t.Fatalf("the app's defaults have %s", out)
	}
	a.Do("tap System")
	a.Until(func(tr *Tree) bool { return findPrefix(tr, "preferred ") == "preferred "+system })
	if out, err := exec.Command("defaults", "read", localePlayerID, "AppleLanguages").CombinedOutput(); err == nil {
		t.Fatalf("the app's defaults still have %s", out)
	}
}

// findPrefix is the first text that starts with p ("" if none).
func findPrefix(tr *Tree, p string) string {
	for _, n := range tr.All() {
		if strings.HasPrefix(n.Text, p) {
			return n.Text
		}
	}
	return ""
}

// The main window's title follows the app's, and an empty one is the
// app's name (TestWindowTitleCommand).
func TestLocaleWindowTitle(t *testing.T) {
	a := Run(t, `
		store Titles { state title = "First" }
		let titles = Titles()
		app {
		  Window {
		    title: titles.title
		    Column {
		      Button("Second") { on press { titles.title = "Second" } }
		      Button("Empty") { on press { titles.title = "" } }
		    }
		  }
		}
	`)
	a.Until(func(tr *Tree) bool { return tr.Main() != nil && tr.Main().Title == "First" })
	a.Do("tap Second")
	a.Until(func(tr *Tree) bool { return tr.Main().Title == "Second" })
	a.Do("tap Empty")
	a.Until(func(tr *Tree) bool { return tr.Main().Title == "Mac Test" })
}

// A forward symbol points where the view reads, and chevron.right where it
// says, whichever way (TestSymbolsMirror): read from the window's image.
func TestLocaleSymbolsMirror(t *testing.T) {
	a := runRTL(t, `
		app {
		  Column {
		    gap: 8
		    padding: 8
		    align: .start
		    Symbol("chevron.forward") { font: Font(size: 40) }
		    Symbol("chevron.backward") { font: Font(size: 40) }
		    Symbol("chevron.right") { font: Font(size: 40) }
		    View {
		      layoutDirection: .leftToRight
		      Symbol("chevron.forward") { font: Font(size: 40) }
		    }
		  }
		}
	`)
	tr := a.Until(func(tr *Tree) bool { return len(tr.Kind("Symbol")) == 4 })
	shot := filepath.Join(t.TempDir(), "symbols.content.png")
	a.Snap(shot)
	img := readPNG(t, shot)
	k := float64(img.Bounds().Dx()) / tr.Main().Content[2]
	want := []string{"left", "right", "right", "right"}
	for i, n := range tr.Kind("Symbol") {
		r := n.InWindow
		if got := pointsTo(img, int(r[0]*k), int(r[1]*k), int((r[0]+r[2])*k), int((r[1]+r[3])*k)); got != want[i] {
			t.Errorf("symbol %d (rtl %v) points %s, want %s", i, rtl(n), got, want[i])
		}
	}
}

func readPNG(t *testing.T, path string) image.Image {
	t.Helper()
	f, err := os.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	img, err := png.Decode(f)
	if err != nil {
		t.Fatal(err)
	}
	return img
}

// pointsTo says which way a chevron drawn in the rectangle points: the
// tip's row (the middle) starts further from the left than the ends' when
// it points right, nearer when it points left. Ink is what differs from
// the rectangle's corner.
func pointsTo(img image.Image, x0, y0, x1, y1 int) string {
	bg := img.At(x0, y0)
	ink := func(x, y int) bool {
		r, g, b, _ := img.At(x, y).RGBA()
		R, G, B, _ := bg.RGBA()
		d := func(p, q uint32) int64 { v := int64(p) - int64(q); return v * v }
		return d(r, R)+d(g, G)+d(b, B) > 3*0x4000*0x4000
	}
	first := func(y int) int {
		for x := x0; x < x1; x++ {
			if ink(x, y) {
				return x
			}
		}
		return x1
	}
	top, bottom := -1, -1
	for y := y0; y < y1; y++ {
		if first(y) < x1 {
			if top < 0 {
				top = y
			}
			bottom = y
		}
	}
	if top < 0 {
		return "nothing"
	}
	if first((top+bottom)/2) > first(top+1) {
		return "right"
	}
	return "left"
}
