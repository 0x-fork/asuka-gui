//go:build darwin

package mactest

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// What the user types reaches the app's state, Return submits, and the
// app's write reaches the field (core_test.go's TestTextField).
func TestTextFieldTyping(t *testing.T) {
	a := Run(t, `
		component Form {
		  state value = ""
		  state submitted = 0
		  Column {
		    TextField { bind text: value; placeholder: "f"; on submit { submitted += 1 } }
		    Text("{value} {submitted}")
		    Button("Set") { on press { value = "set" } }
		  }
		}
		app { Form() }
	`)
	a.UntilText(" 0")
	a.Do("tap f", "type hello")
	a.UntilText("hello 0")
	a.Do("keyin enter")
	a.UntilText("hello 1")
	a.Do("tap Set")
	a.Until(func(tr *Tree) bool {
		n, _ := fieldOf(t, tr, "f")
		return n.Str("stringValue") == "set" && tr.FindText("set 1") != nil
	})
}

// A long text wraps in a narrow column (core_test.go's TestTextWraps).
func TestTextWraps(t *testing.T) {
	a := Run(t, `
		app {
		  Column {
		    width: 120
		    Text("The quick brown fox jumps over the lazy dog, again and again, until the line has to wrap.")
		  }
		}
	`)
	tr := a.Until(func(tr *Tree) bool { return len(tr.Kind("Text")) == 1 && len(tr.Kind("Text")[0].Frame) == 4 })
	f := tr.Kind("Text")[0].Frame
	if f[2] > 120 || f[3] < 40 {
		t.Fatalf("a long text in 120pt: %v", f)
	}
}

// A field's selection set from code moves its editor's; insert goes in at
// it, as typing, and the field reports the text and where the cursor went.
// A one-line field and a multiline one (TestTextFieldSelectionAndInsert).
func TestTextFieldSelectionAndInsert(t *testing.T) {
	for _, multiline := range []string{"false", "true"} {
		t.Run("multiline="+multiline, func(t *testing.T) {
			a := Run(t, `
				component F {
				  state text = "hello world"
				  state sel = TextRange()
				  let field = Ref()
				  Column {
				    Text("text {text} sel {sel.start},{sel.end}")
				    Button("Place") { on press { sel = TextRange(start: 5, end: 5) } }
				    Button("Comma") { on press { field.insert(",") } }
				    TextField { ref: field; bind text: text; bind selection: sel; placeholder: "f"; multiline: `+multiline+` }
				  }
				}
				app { F() }
			`)
			a.UntilText("text hello world sel 0,0")
			a.Do("tap f", "tap Place")
			tr := a.Until(func(tr *Tree) bool { _, f := fieldOf(t, tr, "f"); return sel(f) == "5,5" })
			if _, f := fieldOf(t, tr, "f"); f["multiline"] != (multiline == "true") || f["editorClass"] != "AsukaFieldEditor" {
				t.Fatalf("multiline %v, edited by %v", f["multiline"], f["editorClass"])
			}
			a.Do("tap Comma")
			a.UntilText("text hello, world sel 6,6")
			// The user moves it: the app hears of it.
			a.Do("select 0,5")
			a.UntilText("text hello, world sel 0,5")
		})
	}
}

// A selection set while the field isn't being edited is where its editing
// starts.
func TestTextFieldPendingSelection(t *testing.T) {
	a := Run(t, `
		component F {
		  state sel = TextRange(start: 2, end: 4)
		  Column {
		    Text("sel {sel.start},{sel.end}")
		    TextField { text: "abcdef"; bind selection: sel; placeholder: "f" }
		  }
		}
		app { F() }
	`)
	a.UntilText("sel 2,4")
	tr := a.Tree()
	if _, f := fieldOf(t, tr, "f"); sel(f) != "" {
		t.Fatalf("the field is edited already: %v", f["selection"])
	}
	a.Do("tap f")
	a.Until(func(tr *Tree) bool { _, f := fieldOf(t, tr, "f"); return sel(f) == "2,4" })
}

// A Rich field shows its styles, formats its selection from code (the text
// view's undo takes it back), and reports the user's styled text in the
// core's encoding, and the formats where the selection is (TestTextFieldRich).
func TestTextFieldRich(t *testing.T) {
	a := Run(t, `
		fn describe(s: Span) -> string = "{s.text.replace("\n", "/")}|{s.italic}|{s.font.weight == .bold}"
		component F {
		  state rich: [Span] = [Span(text: "Hello "), Span(text: "world", font: Font(weight: .bold)), Span(text: "\nbye")]
		  state italic = false
		  let field = Ref()
		  Column {
		    Text("spans {rich.map(describe).joined(separator: ";")}")
		    Text("italic {italic}")
		    Button("Italic") { on press { field.toggleFormat(.italic) } }
		    Button("Link") { on press { field.setLink("https://example.com") } }
		    TextField {
		      ref: field
		      bind rich: rich
		      placeholder: "r"
		      on formats(f) { italic = f.italic }
		    }
		  }
		}
		app { F() }
	`)
	tr := a.UntilText("italic false")
	_, f := fieldOf(t, tr, "r")
	if f["string"] != "Hello world\nbye" || f["multiline"] != true {
		t.Fatalf("the field: %v", f)
	}
	runs, _ := f["runs"].([]any)
	bold := false
	for _, r := range runs {
		r := r.(map[string]any)
		if strings.TrimSpace(r["text"].(string)) == "world" && r["bold"] == true {
			bold = true
		}
	}
	if !bold {
		t.Fatalf("world isn't bold: %v", runs)
	}
	// "Hello" italic, from code.
	a.Do("tap r", "select 0,5", "tap Italic")
	a.UntilText("spans Hello|true|false; |false|false;world/|false|true;bye|false|false")
	a.UntilText("italic true")
	// Undone, as the text view's own changes are.
	a.Do("undo")
	a.UntilText("spans Hello |false|false;world/|false|true;bye|false|false")
	// A link on the selection.
	a.Do("select 6,11", "tap Link")
	tr = a.Until(func(tr *Tree) bool {
		_, f := fieldOf(t, tr, "r")
		return f["link"] == "https://example.com"
	})
}

// A one-line field's padding is inside it: its cell's drawing rect (its
// text's, and its field editor's) is inset by it (TestTextFieldPadding).
func TestTextFieldPadding(t *testing.T) {
	a := Run(t, `
		component Fields {
		  state a = "hi"
		  state b = "hi"
		  Column {
		    align: .start
		    TextField { bind text: a; width: 200; placeholder: "padded"; padding: Edges(top: 4, left: 30) }
		    TextField { bind text: b; width: 200; placeholder: "plain" }
		  }
		}
		app { Fields() }
	`)
	tr := a.UntilText("hi")
	_, p := fieldOf(t, tr, "padded")
	_, q := fieldOf(t, tr, "plain")
	pr, _ := p["drawingRect"].([]any)
	qr, _ := q["drawingRect"].([]any)
	if len(pr) != 4 || len(qr) != 4 {
		t.Fatalf("drawing rects %v %v", pr, qr)
	}
	if pr[0].(float64)-qr[0].(float64) != 30 || pr[2].(float64) != qr[2].(float64)-30 {
		t.Fatalf("padded %v, plain %v", pr, qr)
	}
}

// A field keeps what the app set before a prop made it another view:
// secure's field keeps the background, border, corners and opacity; a
// multiline field's scroll view the corners and opacity (a scroll view
// draws its own background and border, whenever the app sets them).
func TestTextFieldStyleBeforeItsView(t *testing.T) {
	a := Run(t, `
		style box {
		  width: 200
		  height: 56
		  background: #ffffff
		  radius: 12
		  border: 2
		  borderColor: #ccd8e0
		  opacity: 0.5
		}
		component F {
		  Column {
		    align: .start
		    Text("styled")
		    TextField { style: box; plain: true; placeholder: "One line" }
		    TextField { style: box; plain: true; multiline: true; placeholder: "Multiline" }
		    TextField { style: box; plain: true; secure: true; placeholder: "Secure" }
		  }
		}
		app { F() }
	`)
	tr := a.UntilText("styled")
	for _, c := range []struct {
		placeholder string
		layer       bool // background and border too
	}{{"One line", true}, {"Multiline", false}, {"Secure", true}} {
		n, _ := fieldOf(t, tr, c.placeholder)
		bg := n.Str("layerBackground")
		if n.Num("layerCornerRadius") != 12 || n.Num("alpha") != 0.5 {
			t.Errorf("%s (a %s): corner radius %v, opacity %v; want 12, 0.5", c.placeholder, n.Class, n.Num("layerCornerRadius"), n.Num("alpha"))
		}
		if c.layer && (!strings.HasPrefix(bg, "#ffffff") || n.Num("layerBorderWidth") != 2) {
			t.Errorf("%s (a %s): background %q, border %v; want #ffffff, 2", c.placeholder, n.Class, bg, n.Num("layerBorderWidth"))
		}
	}
}

// A multiline field is a text view in a scroll view: its placeholder, its
// plain look, its autocorrection, and its height following its text up to
// its lines.
func TestTextFieldMultiline(t *testing.T) {
	a := Run(t, `
		component F {
		  state notes = ""
		  state plainNotes = ""
		  Column {
		    align: .start
		    Text("notes {notes.count()}")
		    TextField { bind text: notes; multiline: true; lines: 3; width: 200; placeholder: "Notes"; autocorrect: .no }
		    TextField { bind text: plainNotes; multiline: true; plain: true; width: 200; placeholder: "Plain" }
		  }
		}
		app { F() }
	`)
	tr := a.UntilText("notes 0")
	n, f := fieldOf(t, tr, "Notes")
	if n.Class != "NSScrollView" || f["editorClass"] != nil && f["editorClass"] != "AsukaFieldEditor" {
		t.Fatalf("a multiline field is a %s (%v)", n.Class, f["editorClass"])
	}
	if f["placeholder"] != "Notes" || f["autocorrect"] != false || f["bordered"] != true || f["drawsBackground"] != true {
		t.Fatalf("the field: %v", f)
	}
	_, pf := fieldOf(t, tr, "Plain")
	if pf["bordered"] != false || pf["drawsBackground"] != false {
		t.Fatalf("the plain field: %v", pf)
	}
	one := n.Frame[3]
	a.Do("tap Notes", "type a", "keyin enter", "type b")
	tr = a.UntilText("notes 3")
	n, _ = fieldOf(t, tr, "Notes")
	two := n.Frame[3]
	if two <= one {
		t.Fatalf("two lines are %v tall, one %v", two, one)
	}
	a.Do("keyin enter", "type c", "keyin enter", "type d", "keyin enter", "type e", "keyin enter", "type f")
	tr = a.UntilText("notes 11")
	n, _ = fieldOf(t, tr, "Notes")
	if n.Frame[3] <= two || n.Frame[3] > one+2*(two-one)+0.5 {
		t.Fatalf("six lines are %v tall (one %v, two %v): want three's", n.Frame[3], one, two)
	}
}

// What's pasted of a field's paste types goes to on paste (a file, the
// app's own copy); text pastes as it would.
func TestTextFieldPaste(t *testing.T) {
	png := filepath.Join(t.TempDir(), "dot.png")
	if err := os.WriteFile(png, onePixelPNG, 0o644); err != nil {
		t.Fatal(err)
	}
	a := Run(t, `
		component F {
		  state text = ""
		  state pasted = ""
		  Column {
		    Text("text {text} pasted {pasted}")
		    TextField {
		      bind text: text
		      placeholder: "f"
		      pasteTypes: ["image/*"]
		      on paste(items) {
		        for it in items { pasted = pasted + "{it.kind == .image} {it.name} {it.mime} {it.path.contains("Drops")};" }
		      }
		    }
		  }
		}
		app { F() }
	`)
	a.UntilText("text  pasted ")
	a.Do("tap f", "pastefile "+png)
	a.UntilText("text  pasted true dot.png image/png true;")
	a.Do("paste plain words")
	a.UntilText("text plain words pasted true dot.png image/png true;")
}

// A 1×1 PNG.
var onePixelPNG = []byte{
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
	0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
	0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x89, 0x99, 0x3d, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
	0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
}
