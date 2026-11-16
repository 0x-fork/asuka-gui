package lsp

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// A checker's "did you mean" is a quick fix.
func TestCodeAction(t *testing.T) {
	root := copyTree(t, "testdata/app")
	c := newClient(t, root)
	uri, text := c.open(filepath.Join(root, "main.asuka"))
	c.waitDiags(uri, noDiags)
	broken := strings.Replace(text, "Text(todo.title)", "Text(todo.titel)", 1)
	c.change(uri, 2, broken)
	ds := c.waitDiags(uri, hasDiag("titel"))
	var p CodeActionParams
	p.TextDocument.URI = uri
	p.Range = ds[0].Range
	p.Context.Diagnostics = ds
	var actions []CodeAction
	if err := c.call("textDocument/codeAction", &p, &actions); err != nil {
		t.Fatal(err)
	}
	if len(actions) != 1 || actions[0].Title != "Change to title" {
		t.Fatalf("got %+v", actions)
	}
	if got := applyEdits(broken, actions[0].Edit.Changes[uri]); got != text {
		t.Errorf("fixed:\n%s", got)
	}
}

// Positions count UTF-16 units, as LSP says: text before a name on its
// line that's outside the BMP (an emoji) or multi-byte (é) is counted so.
func TestUTF16AndIncrementalEdits(t *testing.T) {
	root := t.TempDir()
	c := newClient(t, root)
	text := "component A {\n  state café = \"🎉\"\n  Text(\"🎉 {café}\") { color: .accent }\n}\n"
	uri := c.openText(filepath.Join(root, "a.asuka"), text)
	c.waitDiags(uri, noDiags)

	var h *Hover
	p := tdp(uri, posAt(t, text, "{caf|é}"))
	if err := c.call("textDocument/hover", &p, &h); err != nil || h == nil || !strings.Contains(h.Contents.Value, "state café: string") {
		t.Fatalf("hover: %v %+v", err, h)
	}
	// The emoji is 2 units: `  Text("🎉 {` is 12 units to café, which
	// is 4 (é is 1).
	if h.Range == nil || h.Range.Start.Character != 12 || h.Range.End.Character != 16 {
		t.Errorf("range %+v", h.Range)
	}

	// An incremental edit after the emoji: café → cafe (a mistake).
	start := posAt(t, text, "{caf|é}")
	c.notify("textDocument/didChange", map[string]any{
		"textDocument":   VersionedTextDocumentIdentifier{URI: uri, Version: 2},
		"contentChanges": []map[string]any{{"range": Range{Start: start, End: Position{Line: start.Line, Character: start.Character + 1}}, "text": "e"}},
	})
	ds := c.waitDiags(uri, hasDiag("unknown name cafe"))
	if ds[0].Range.Start.Character != 12 {
		t.Errorf("diagnostic at %+v", ds[0].Range)
	}
	// And back.
	c.notify("textDocument/didChange", map[string]any{
		"textDocument":   VersionedTextDocumentIdentifier{URI: uri, Version: 3},
		"contentChanges": []map[string]any{{"range": Range{Start: start, End: Position{Line: start.Line, Character: start.Character + 1}}, "text": "é"}},
	})
	c.waitDiags(uri, noDiags)
}

// A prop's value before its . is typed offers the values with it.
func TestCompletionPropValue(t *testing.T) {
	root := t.TempDir()
	c := newClient(t, root)
	text := "component A {\n  Button(\"Go\") {\n    variant: \n  }\n}\n"
	uri := c.openText(filepath.Join(root, "a.asuka"), text)
	version := 1
	items := complete(t, c, uri, &version, text, "component A {\n  Button(\"Go\") {\n    variant: |")
	found := false
	for _, it := range items {
		if it.Label == ".filled" {
			found = true
			if it.InsertText != ".filled" || it.FilterText != "filled" {
				t.Errorf("item %+v", it)
			}
		}
	}
	if !found {
		t.Errorf("no .filled in %v", labels(items))
	}
}

// The standard library: hover and definition reach into std's files.
func TestStandardLibrary(t *testing.T) {
	root := t.TempDir()
	if r, err := filepath.EvalSymlinks(root); err == nil {
		root = r
	}
	os.WriteFile(filepath.Join(root, "asuka.toml"), []byte("name = \"app\"\n"), 0o644)
	text := "import std/store\nimport std/math\n\nfn units() -> string = store.read(\"units\", \"metric\")\nfn two() -> float = math.sqrt(4)\n"
	c := newClient(t, root)
	uri := c.openText(filepath.Join(root, "a.asuka"), text)
	c.waitDiags(uri, noDiags)

	var locs []Location
	p := tdp(uri, posAt(t, text, "store.re|ad("))
	if err := c.call("textDocument/definition", &p, &locs); err != nil || len(locs) != 1 || !strings.HasSuffix(locs[0].URI, "/store/store.asuka") {
		t.Fatalf("definition: %v %+v", err, locs)
	}
	var h *Hover
	p = tdp(uri, posAt(t, text, "store.re|ad("))
	if err := c.call("textDocument/hover", &p, &h); err != nil || h == nil || !strings.Contains(h.Contents.Value, "fn read<T>(key: string, def: T) -> T") {
		t.Fatalf("hover: %v %+v", err, h)
	}
	p = tdp(uri, posAt(t, text, "import std/sto|re"))
	if err := c.call("textDocument/hover", &p, &h); err != nil || h == nil || !strings.Contains(h.Contents.Value, "keeps small values") {
		t.Fatalf("package hover: %v %+v", err, h)
	}
	p = tdp(uri, posAt(t, text, "math.sq|rt"))
	if err := c.call("textDocument/hover", &p, &h); err != nil || h == nil || !strings.Contains(h.Contents.Value, "The square root.") {
		t.Fatalf("math hover: %v %+v", err, h)
	}
	version := 1
	text2 := text + "fn x() -> float = math.\n"
	items := complete(t, c, uri, &version, text2, text2[:len(text2)-1]+"|")
	if got := labels(items); !contains(got, "sqrt") || !contains(got, "pi") {
		t.Errorf("math.: %v", got)
	}
}
