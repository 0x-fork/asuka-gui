package lsp

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// An argument's label names a parameter of what it's passed to: an
// emit's event, a style, a function, whichever element is around it.
func TestLabels(t *testing.T) {
	root := t.TempDir()
	c := newClient(t, root)
	text := `style chip(tint: Color = .blue) { color: tint }

component Row(title: string) {
  event archive(id: string)

  View {
    on press { emit archive(id: title) }
    Text(title) { style: chip(tint: .red) }
  }
}

app { Row("a") { on archive(id) { print(id) } } }
`
	uri := c.openText(filepath.Join(root, "a.asuka"), text)
	c.waitDiags(uri, noDiags)
	for _, tc := range []struct{ at, want string }{
		{"emit archive(i|d: title)", "(parameter) id: string"},
		{"emit archive(i|d: title)", "Parameter of event `archive` of `Row`."},
		{"event archive(i|d: string)", "(parameter) id: string"},
		{"chip(ti|nt: .red)", "(parameter) tint: Color"},
	} {
		if got := hoverAt(t, c, uri, text, tc.at); !strings.Contains(got, tc.want) {
			t.Errorf("hover at %s: %q lacks %q", tc.at, got, tc.want)
		}
	}
	for _, tc := range []struct{ at, decl string }{
		{"emit archive(i|d: title)", "event archive(|id: string)"},
		{"chip(ti|nt: .red)", "style chip(|tint"},
	} {
		var locs []Location
		p := tdp(uri, posAt(t, text, tc.at))
		if err := c.call("textDocument/definition", &p, &locs); err != nil {
			t.Fatal(err)
		}
		if want := posAt(t, text, tc.decl); len(locs) != 1 || locs[0].Range.Start != want {
			t.Errorf("definition at %s: %+v, want %+v", tc.at, locs, want)
		}
	}

	// Renaming the event's parameter renames the emit's label; the
	// handler's own name for it stays.
	var edit WorkspaceEdit
	rp := RenameParams{TextDocumentPositionParams: tdp(uri, posAt(t, text, "event archive(i|d")), NewName: "key"}
	if err := c.call("textDocument/rename", &rp, &edit); err != nil {
		t.Fatal(err)
	}
	got := applyEdits(text, edit.Changes[uri])
	if !strings.Contains(got, "event archive(key: string)") || !strings.Contains(got, "emit archive(key: title)") || !strings.Contains(got, "on archive(id) { print(id) }") {
		t.Errorf("renamed:\n%s", got)
	}

	// A label that names nothing has no hover: not the element's around
	// it.
	version := 1
	broken := strings.Replace(text, "emit archive(id: title)", "emit archive(nope: title)", 1)
	version++
	c.change(uri, version, broken)
	if got := hoverAt(t, c, uri, broken, "emit archive(no|pe: title)"); got != "" {
		t.Errorf("hover on a label that names nothing: %q", got)
	}
}

// Renaming a parameter renames the labels that name it in the package's
// other files: a function's, a style's.
func TestRenameLabelledParams(t *testing.T) {
	root := t.TempDir()
	a := "fn area(width: float, height: float = 1) -> float = width * height\n\nstyle chip(tint: Color) { color: tint }\n"
	b := "fn big() -> float = area(3, height: 4)\n\ncomponent Tag { Text(\"a\") { style: chip(tint: .red) } }\n"
	if err := os.WriteFile(filepath.Join(root, "a.asuka"), []byte(a), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(root, "b.asuka"), []byte(b), 0o644); err != nil {
		t.Fatal(err)
	}
	c := newClient(t, root)
	uri := c.openText(filepath.Join(root, "a.asuka"), a)
	c.waitDiags(uri, noDiags)
	buri := pathToURI(filepath.Join(root, "b.asuka"))
	for _, tc := range []struct{ at, wantA, wantB string }{
		{"he|ight: float", "fn area(width: float, size: float = 1) -> float = width * size", "area(3, size: 4)"},
		{"ti|nt: Color", "style chip(size: Color) { color: size }", "chip(size: .red)"},
	} {
		var edit WorkspaceEdit
		rp := RenameParams{TextDocumentPositionParams: tdp(uri, posAt(t, a, tc.at)), NewName: "size"}
		if err := c.call("textDocument/rename", &rp, &edit); err != nil {
			t.Fatal(err)
		}
		if got := applyEdits(a, edit.Changes[uri]); !strings.Contains(got, tc.wantA) {
			t.Errorf("rename at %s: a.asuka:\n%s", tc.at, got)
		}
		if got := applyEdits(b, edit.Changes[buri]); !strings.Contains(got, tc.wantB) {
			t.Errorf("rename at %s: b.asuka:\n%s", tc.at, got)
		}
	}
}

// A type parameter is declared where it's written (<T>), not at its
// type's or function's name: renaming it renames it and its uses only,
// and its uses' definition is it.
func TestTypeParams(t *testing.T) {
	root := t.TempDir()
	a := `struct Page<T> {
  items: [T]

  fn first() -> T? = items.first()
}

enum Box<E> { full(E), empty }

fn wrap<U>(x: U) -> [U] = [x]

fn pages() -> Page<int> = Page(items: wrap(1))
`
	if err := os.WriteFile(filepath.Join(root, "a.asuka"), []byte(a), 0o644); err != nil {
		t.Fatal(err)
	}
	c := newClient(t, root)
	uri := c.openText(filepath.Join(root, "a.asuka"), a)
	c.waitDiags(uri, noDiags)
	for _, tc := range []struct{ at, decl, renamed string }{
		{"items: [|T]", "struct Page<|T>", "struct Page<Item> {\n  items: [Item]\n\n  fn first() -> Item? = items.first()\n}\n\nenum Box<E>"},
		{"fn first() -> |T?", "struct Page<|T>", "struct Page<Item> {\n  items: [Item]\n\n  fn first() -> Item? = items.first()\n}\n\nenum Box<E>"},
		{"struct Page<|T>", "struct Page<|T>", "struct Page<Item> {\n  items: [Item]\n\n  fn first() -> Item? = items.first()\n}\n\nenum Box<E>"},
		{"full(|E)", "enum Box<|E>", "enum Box<Item> { full(Item), empty }"},
		{"fn wrap<U>(x: |U)", "fn wrap<|U>", "fn wrap<Item>(x: Item) -> [Item] = [x]"},
		{"-> [|U]", "fn wrap<|U>", "fn wrap<Item>(x: Item) -> [Item] = [x]"},
	} {
		t.Run(tc.at, func(t *testing.T) {
			var locs []Location
			p := tdp(uri, posAt(t, a, tc.at))
			if err := c.call("textDocument/definition", &p, &locs); err != nil {
				t.Fatal(err)
			}
			if want := posAt(t, a, tc.decl); len(locs) != 1 || locs[0].Range.Start != want {
				t.Errorf("definition: %+v, want %+v", locs, want)
			}
			var edit WorkspaceEdit
			rp := RenameParams{TextDocumentPositionParams: p, NewName: "Item"}
			if err := c.call("textDocument/rename", &rp, &edit); err != nil {
				t.Fatal(err)
			}
			got := applyEdits(a, edit.Changes[uri])
			if !strings.Contains(got, tc.renamed) || !strings.Contains(got, "fn pages() -> Page<int> = Page(items: wrap(1))") {
				t.Errorf("renamed:\n%s", got)
			}
		})
	}
}
