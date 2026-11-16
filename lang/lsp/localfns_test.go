package lsp

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// Local fns hover and color as functions' values; a closure calling
// itself through its var is a warning (severity 2), not an error.
func TestLocalFns(t *testing.T) {
	root := t.TempDir()
	src := `fn depth(n: int) -> int {
  fn down(k: int) -> int = if k == 0 { 0 } else { 1 + down(k - 1) }
  var walk: fn(int) -> int = k => 0
  walk = k => if k == 0 { 0 } else { walk(k - 1) }
  return down(n) + walk(n)
}

test "depth" {
  expect(depth(2) == 2)
}
`
	if err := os.WriteFile(filepath.Join(root, "asuka.toml"), []byte("name = \"localfns\"\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	main := filepath.Join(root, "main.asuka")
	if err := os.WriteFile(main, []byte(src), 0o644); err != nil {
		t.Fatal(err)
	}
	c := newClient(t, root)
	uri, text := c.open(main)
	ds := c.waitDiags(uri, func(ds []Diagnostic) bool { return len(ds) > 0 })
	if len(ds) != 1 || ds[0].Severity != 2 || !strings.Contains(ds[0].Message, "walk calls itself through the variable it's stored in") {
		t.Fatalf("want one warning, got %+v", ds)
	}
	if line := strings.Count(text[:strings.Index(text, "walk = k =>")], "\n"); ds[0].Range.Start.Line != line {
		t.Errorf("the warning is on line %d, want %d", ds[0].Range.Start.Line, line)
	}
	for _, at := range []string{"fn do|wn(k", "1 + do|wn(k - 1)", "return do|wn(n)"} {
		var h *Hover
		p := tdp(uri, posAt(t, text, at))
		if err := c.call("textDocument/hover", &p, &h); err != nil {
			t.Fatal(err)
		}
		if h == nil || !strings.Contains(h.Contents.Value, "fn down(k: int) -> int") {
			t.Errorf("%s: hover %+v", at, h)
		}
	}
	// Completion: in a fn of a block, the fns declared next to it.
	version := 1
	edited := strings.Replace(text, "1 + down(k - 1)", "1 + do(k - 1)", 1)
	i := strings.Index(edited, "1 + do(") + len("1 + do")
	if got := labels(complete(t, c, uri, &version, edited, edited[:i]+"|")); !contains(got, "down") {
		t.Errorf("no down in %v", got)
	}
	c.change(uri, version+1, text)

	var toks SemanticTokens
	if err := c.call("textDocument/semanticTokens/full", &SemanticTokensParams{TextDocument: TextDocumentIdentifier{URI: uri}}, &toks); err != nil {
		t.Fatal(err)
	}
	m := newMapper([]byte(text), false)
	line, char := 0, 0
	found := 0
	for i := 0; i+4 < len(toks.Data); i += 5 {
		d := toks.Data[i : i+5]
		if d[0] > 0 {
			line, char = line+int(d[0]), int(d[1])
		} else {
			char += int(d[1])
		}
		start := m.offset(Position{Line: line, Character: char})
		if text[start:start+int(d[2])] == "down" {
			found++
			if tokenTypes[d[3]] != "function" {
				t.Errorf("down at %d:%d is a %s, want a function", line, char, tokenTypes[d[3]])
			}
		}
	}
	if found != 3 {
		t.Errorf("%d tokens for down, want 3", found)
	}
}
