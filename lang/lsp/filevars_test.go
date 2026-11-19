package lsp

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// A file's var hovers as a var, colors as a variable that changes (not
// readonly), and completes.
func TestFileVars(t *testing.T) {
	root := t.TempDir()
	src := `var hits = 0
let limit = 3

fn bump() {
  hits += 1
  print(hits, limit)
}
`
	if err := os.WriteFile(filepath.Join(root, "asuka.toml"), []byte("name = \"vars\"\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	main := filepath.Join(root, "main.asuka")
	if err := os.WriteFile(main, []byte(src), 0o644); err != nil {
		t.Fatal(err)
	}
	c := newClient(t, root)
	uri, text := c.open(main)
	c.waitDiags(uri, noDiags)
	for _, tc := range []struct{ at, want string }{
		{"  hi|ts += 1", "var hits: int"},
		{"print(hits, lim|it)", "let limit: int = 3"},
	} {
		var h *Hover
		p := tdp(uri, posAt(t, text, tc.at))
		if err := c.call("textDocument/hover", &p, &h); err != nil {
			t.Fatal(err)
		}
		if h == nil || !strings.Contains(h.Contents.Value, tc.want) {
			t.Errorf("hover at %q: want %q, got %+v", tc.at, tc.want, h)
		}
	}
	var toks SemanticTokens
	if err := c.call("textDocument/semanticTokens/full", &SemanticTokensParams{TextDocument: TextDocumentIdentifier{URI: uri}}, &toks); err != nil {
		t.Fatal(err)
	}
	m := newMapper([]byte(text), false)
	mods := map[string]int{}
	line, char := 0, 0
	for i := 0; i+4 < len(toks.Data); i += 5 {
		d := toks.Data[i : i+5]
		if d[0] > 0 {
			line, char = line+int(d[0]), int(d[1])
		} else {
			char += int(d[1])
		}
		start := m.offset(Position{Line: line, Character: char})
		mods[text[start:start+int(d[2])]+"@"+itoa(line)] = int(d[4])
	}
	if mods["hits@4"]&modReadonly != 0 || mods["limit@5"]&modReadonly == 0 {
		t.Errorf("modifiers: hits %d, limit %d", mods["hits@4"], mods["limit@5"])
	}
	version := 1
	marked := strings.Replace(text, "print(hits, limit)", "print(h|)", 1)
	i := strings.IndexByte(marked, '|')
	edited := marked[:i] + marked[i+1:]
	if got := labels(complete(t, c, uri, &version, edited, marked[:i+1])); !contains(got, "hits") {
		t.Errorf("no hits in %v", got)
	}
}
