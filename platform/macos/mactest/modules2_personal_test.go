//go:build darwin

package mactest

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// The personal data modules (contacts, calendar, photoLibrary, library),
// called by name with the arguments platform/contacts, platform/calendar,
// platform/photos and platform/library send, each answer or error shown in a
// Text. They never ask the user here: the automation's seam
// (ASUKA_TEST_ACCESS, the access step) answers for the system, so the
// tests check the arguments, the encodings, the alerts and the paths
// taken when the user hasn't allowed it.

// m2Src is a component with a button ("go <label>") that calls
// module.method with args and shows "<label>: ok <answer>" (as hex, or as
// text with text: true) or "<label>: err <error>".
const m2Src = `
	import platform/host
	import std/encoding

	component Call(label: string, module: string, method: string, args: bytes) {
	  prop text = false
	  state s = "idle"
	  Row {
	    gap: 8
	    Button("go {label}") {
	      on press {
	        s = "pending"
	        do {
	          let b = try await host.call(module, method, args)
	          s = if text { "ok {b.string()}" } else { "ok {encoding.hexEncode(b)}" }
	        } catch e {
	          s = "err {e.message}"
	        }
	      }
	    }
	    Text("{label}: {s}")
	  }
	}
`

// m2Str is s as an Asuka string literal.
func m2Str(s string) string {
	r := strings.NewReplacer(`\`, `\\`, `"`, `\"`, "{", `\{`, "\n", `\n`)
	return `"` + r.Replace(s) + `"`
}

// m2Bytes is s's bytes, in Asuka.
func m2Bytes(s string) string { return m2Str(s) + ".bytes()" }

// m2JSON is v as JSON's bytes, in Asuka.
func m2JSON(v any) string {
	b, _ := json.Marshal(v)
	return m2Bytes(string(b))
}

// m2Text is what label's Text says after "<label>: " (nil: no such Text).
func m2Text(tr *Tree, label string) *string {
	for _, n := range tr.All() {
		if s, ok := strings.CutPrefix(n.Text, label+": "); ok {
			return &s
		}
	}
	return nil
}

// m2Answer waits for label's answer (its Text no longer idle or pending).
func m2Answer(t *testing.T, a *App, label string) string {
	t.Helper()
	var s string
	a.Until(func(tr *Tree) bool {
		p := m2Text(tr, label)
		if p == nil || *p == "idle" || *p == "pending" {
			return false
		}
		s = *p
		return true
	})
	return s
}

// m2Go presses label's button and waits for its answer.
func m2Go(t *testing.T, a *App, label string) string {
	t.Helper()
	a.Do("tap go " + label)
	return m2Answer(t, a, label)
}

// m2Extra is the modules2 part of the host's JSON: the seams' asks, the
// contact picker, the last alert.
type m2Extra struct {
	Asked          map[string][]string `json:"asked"`
	ContactsPicker bool                `json:"contactsPicker"`
	Popovers       int                 `json:"popovers"`
	Alert          struct{ Title, Info string }
}

func m2Of(tr *Tree) m2Extra {
	var o struct {
		M m2Extra `json:"modules2"`
	}
	json.Unmarshal([]byte(tr.Raw()), &o)
	return o.M
}

// m2Sheet presses label's button, waits for the alert over the window,
// presses its button, and waits for the answer; it returns the alert's
// texts too.
func m2Sheet(t *testing.T, a *App, label, button string) (string, m2Extra) {
	t.Helper()
	a.Do("tap go " + label)
	tr := a.Until(func(tr *Tree) bool {
		for _, w := range tr.Windows {
			if w.Props["sheet"] != nil {
				return true
			}
		}
		return false
	})
	x := m2Of(tr)
	a.Do("sheet " + button)
	ans := m2Answer(t, a, label)
	a.Until(func(tr *Tree) bool {
		for _, w := range tr.Windows {
			if w.Props["sheet"] != nil {
				return false
			}
		}
		return true
	})
	return ans, x
}

func m2Want(t *testing.T, what, got, want string) {
	t.Helper()
	if got != want {
		t.Fatalf("%s: %q, want %q", what, got, want)
	}
}

// contacts: what's answered denied (request without asking, list, add
// after Add), cancelled and with bad arguments; asked once when it's not
// been asked. The alert shows the contact.
func TestModules2Contacts(t *testing.T) {
	add := map[string]any{"givenName": "Ada", "familyName": "Lovelace", "organization": "Analytical Engines",
		"phones": []map[string]string{{"label": "", "value": "+44 20 7946 0000"}},
		"emails": []map[string]string{{"label": "work", "value": "ada@example.com"}}}
	a := RunWith(t, m2Src+`
		app {
		  Column {
		    Call("request", "contacts", "request", bytes())
		    Call("list", "contacts", "list", `+m2JSON(map[string]any{"name": "", "limit": 0})+`)
		    Call("add", "contacts", "add", `+m2JSON(add)+`)
		    Call("bad", "contacts", "add", "\{".bytes())
		  }
		}
	`, Options{Env: []string{"ASUKA_TEST_ACCESS=contacts=2"}})
	m2Want(t, "request, denied", m2Go(t, a, "request"), "ok 01")
	if x := m2Of(a.Tree()); len(x.Asked["contacts"]) != 0 {
		t.Fatalf("request, denied: asked %v", x.Asked)
	}
	m2Want(t, "list, denied", m2Go(t, a, "list"), "err not allowed")
	// Denied, Add: not saved, "not allowed"; Cancel: u8 0.
	got, x := m2Sheet(t, a, "add", "Add to Contacts")
	m2Want(t, "add, denied", got, "err not allowed")
	if x.Alert.Title != "Ada Lovelace" || x.Alert.Info != "Analytical Engines\n+44 20 7946 0000\nwork: ada@example.com" {
		t.Errorf("the alert: %q, %q", x.Alert.Title, x.Alert.Info)
	}
	got, _ = m2Sheet(t, a, "add", "Cancel")
	m2Want(t, "add, cancelled", got, "ok 00")
	m2Want(t, "add, bad JSON", m2Go(t, a, "bad"), "err contacts.add: bad arguments")

	a.Do("access contacts=0>3")
	m2Want(t, "request, not asked", m2Go(t, a, "request"), "ok 03")
	if x := m2Of(a.Tree()); len(x.Asked["contacts"]) != 1 {
		t.Fatalf("request, not asked: asked %v, want once", x.Asked)
	}
}

// pick shows the system's contact picker (a popover, out of the app's
// process: no permission), and answers "" once it closes without a pick.
func TestModules2ContactsPick(t *testing.T) {
	a := Run(t, m2Src+`
		app { Call("pick", "contacts", "pick", bytes()) }
	`)
	a.Do("tap go pick")
	a.Until(func(tr *Tree) bool { x := m2Of(tr); return x.ContactsPicker && x.Popovers > 0 })
	if s := m2Text(a.Tree(), "pick"); s == nil || *s != "pending" {
		t.Fatalf("pick answered while the picker shows: %v", s)
	}
	a.Do("closepicker")
	m2Want(t, "pick, closed", strings.TrimSpace(m2Answer(t, a, "pick")), "ok")
	a.Until(func(tr *Tree) bool { x := m2Of(tr); return !x.ContactsPicker && x.Popovers == 0 })
}

// calendar: denied and write only (no reading), cancelled, refused once
// asked for write access as Add is pressed; the alert shows the event's
// title, its dates and its place.
func TestModules2Calendar(t *testing.T) {
	ev := map[string]any{"title": "Launch", "location": "Cupertino", "startMs": 1718035200000, "endMs": 1718038800000, "allDay": false}
	a := RunWith(t, m2Src+`
		app {
		  Column {
		    Call("request", "calendar", "request", bytes())
		    Call("calendars", "calendar", "calendars", bytes())
		    Call("events", "calendar", "events", `+m2JSON(map[string]any{"from": 0, "to": 1000, "calendars": []string{}})+`)
		    Call("add", "calendar", "add", `+m2JSON(ev)+`)
		    Call("bad", "calendar", "add", "[".bytes())
		  }
		}
	`, Options{Env: []string{"ASUKA_TEST_ACCESS=calendar=2"}})
	m2Want(t, "request, denied", m2Go(t, a, "request"), "ok 01")
	m2Want(t, "calendars, denied", m2Go(t, a, "calendars"), "err not allowed")
	m2Want(t, "events, denied", m2Go(t, a, "events"), "err not allowed")
	a.Do("access calendar=4") // write only: still no reading
	m2Want(t, "request, write only", m2Go(t, a, "request"), "ok 02")
	m2Want(t, "events, write only", m2Go(t, a, "events"), "err not allowed")
	got, x := m2Sheet(t, a, "add", "Cancel")
	m2Want(t, "add, cancelled", got, "ok 00")
	if x.Alert.Title != "Launch" || !strings.HasSuffix(x.Alert.Info, "\nCupertino") || len(x.Alert.Info) <= len("\nCupertino") {
		t.Errorf("the alert: %q, %q", x.Alert.Title, x.Alert.Info)
	}
	// Not asked yet, Add: write access asked for, refused.
	a.Do("access calendar=0")
	before := len(m2Of(a.Tree()).Asked["calendar"])
	got, _ = m2Sheet(t, a, "add", "Add to Calendar")
	m2Want(t, "add, refused", got, "err not allowed")
	if asked := m2Of(a.Tree()).Asked["calendar"]; len(asked) != before+1 || asked[len(asked)-1] != "write" {
		t.Errorf("add, refused: asked %v (want write only once more)", asked)
	}
	m2Want(t, "add, bad JSON", m2Go(t, a, "bad"), "err calendar.add: bad arguments")
}

// photoLibrary: each status as the wire names it; request asks once;
// denied, list answers none and thumbnail and export "not allowed";
// manage does nothing.
func TestModules2PhotoLibrary(t *testing.T) {
	a := RunWith(t, m2Src+`
		app {
		  Column {
		    Call("current", "photoLibrary", "current", bytes()) { text: true }
		    Call("request", "photoLibrary", "request", bytes()) { text: true }
		    Call("list", "photoLibrary", "list", `+m2JSON(map[string]any{"offset": 0, "limit": 10})+`) { text: true }
		    Call("thumbnail", "photoLibrary", "thumbnail", `+m2JSON(map[string]any{"id": "x", "size": 100})+`)
		    Call("export", "photoLibrary", "export", `+m2JSON(map[string]any{"id": "x"})+`)
		    Call("manage", "photoLibrary", "manage", bytes())
		  }
		}
	`, Options{Env: []string{"ASUKA_TEST_ACCESS=photos=0"}})
	for _, c := range []struct{ status, want string }{{"0", "notAsked"}, {"1", "denied"}, {"2", "denied"}, {"3", "allowed"}, {"4", "limited"}} {
		a.Do("access photos=" + c.status)
		m2Want(t, "current, status "+c.status, m2Go(t, a, "current"), "ok "+c.want)
	}
	a.Do("access photos=0>2")
	m2Want(t, "request", m2Go(t, a, "request"), "ok denied")
	m2Want(t, "request again", m2Go(t, a, "request"), "ok denied")
	if asked := m2Of(a.Tree()).Asked["photos"]; len(asked) != 1 {
		t.Errorf("asked %d times, want once", len(asked))
	}
	m2Want(t, "list, denied", m2Go(t, a, "list"), "ok []")
	m2Want(t, "thumbnail, denied", m2Go(t, a, "thumbnail"), "err not allowed")
	m2Want(t, "export, denied", m2Go(t, a, "export"), "err not allowed")
	m2Want(t, "manage", strings.TrimSpace(m2Go(t, a, "manage")), "ok")
}

// library.save: a download under its folder, numbered when it's taken;
// music moved; a picture with Photos' add access denied; a missing file
// and bad arguments.
func TestModules2Library(t *testing.T) {
	dir := t.TempDir()
	src := filepath.Join(t.TempDir(), "report.txt")
	os.WriteFile(src, []byte("hello"), 0o644)
	a := RunWith(t, m2Src+`
		app {
		  Column {
		    Call("download", "library", "save", `+m2JSON(map[string]any{"path": src, "collection": 0, "folder": "Reports"})+`) { text: true }
		    Call("music", "library", "save", `+m2JSON(map[string]any{"path": src, "collection": 2, "name": "song.txt", "move": true})+`) { text: true }
		    Call("picture", "library", "save", `+m2JSON(map[string]any{"path": src, "collection": 3})+`)
		    Call("missing", "library", "save", `+m2JSON(map[string]any{"path": filepath.Join(dir, "missing.png"), "collection": 0})+`)
		    Call("bad", "library", "save", "\{".bytes())
		  }
		}
	`, Options{Env: []string{"ASUKA_TEST_LIBRARY_DIR=" + dir, "ASUKA_TEST_ACCESS=photosAdd=2"}})
	m2Want(t, "save to Downloads", m2Go(t, a, "download"), "ok "+filepath.Join(dir, "Downloads", "Reports", "report.txt"))
	m2Want(t, "save again (numbered)", m2Go(t, a, "download"), "ok "+filepath.Join(dir, "Downloads", "Reports", "report (1).txt"))
	p := filepath.Join(dir, "Music", "song.txt")
	m2Want(t, "move to Music", m2Go(t, a, "music"), "ok "+p)
	if b, _ := os.ReadFile(p); string(b) != "hello" {
		t.Errorf("the moved file holds %q", b)
	}
	if _, err := os.Stat(src); !os.IsNotExist(err) {
		t.Errorf("moved, the source is still there: %v", err)
	}
	os.WriteFile(src, []byte("again"), 0o644)
	m2Want(t, "a picture, Photos denied", m2Go(t, a, "picture"), "err not allowed")
	m2Want(t, "a missing file", m2Go(t, a, "missing"), "err no file at "+filepath.Join(dir, "missing.png"))
	m2Want(t, "bad arguments", m2Go(t, a, "bad"), "err bad arguments")
}
