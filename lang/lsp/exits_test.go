package lsp

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// The early exits (let … else, condition chains, jumps and catch as
// values): their names hover, color and complete as any local's.
func TestEarlyExits(t *testing.T) {
	root := t.TempDir()
	src := `enum MailError: Error {
  missing(id: string)
}

fn mail(cache: [string: string], id: string) throws -> string {
  let found = cache[id] else { throw MailError.missing(id) }
  return found
}

fn port(text: string) -> int {
  let n = text.toInt() ?? return 80
  return n
}

fn lookup(cache: [string: string], first: string?, second: string?) -> string {
  if let a = first, let b = second, a < b {
    return a + b
  }
  let text = try mail(cache, "x") catch MailError.missing(which) { which } catch err { err.message }
  return text
}

component Pair {
  state left: int? = none
  state right: int? = none

  if let l = left, let r = right, l < r {
    Text("{l} {r}")
  }
}

app { Pair() }
`
	if err := os.WriteFile(filepath.Join(root, "asuka.toml"), []byte("name = \"exits\"\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	main := filepath.Join(root, "main.asuka")
	if err := os.WriteFile(main, []byte(src), 0o644); err != nil {
		t.Fatal(err)
	}
	c := newClient(t, root)
	uri, text := c.open(main)
	c.waitDiags(uri, noDiags)

	for _, tc := range []struct {
		at   string
		want string
	}{
		{"let fou|nd = cache", "let found: string"},
		{"return fo|und", "let found: string"},
		{"let n| = text", "let n: int"},
		{"let a| = first", "let a: string"},
		{"a < b| {", "let b: string"},
		{"return a| + b", "let a: string"},
		{"{ wh|ich }", "which: string"},
		{"{ err|.message }", "err: Error"},
		{"let tex|t = try", "let text: string"},
		{"Text(\"{l|} {r}\")", "l: int"},
		{"l| < r", "l: int"},
	} {
		t.Run(tc.at, func(t *testing.T) {
			var h *Hover
			p := tdp(uri, posAt(t, text, tc.at))
			if err := c.call("textDocument/hover", &p, &h); err != nil {
				t.Fatal(err)
			}
			if h == nil || !strings.Contains(h.Contents.Value, tc.want) {
				t.Errorf("hover: want %q, got %+v", tc.want, h)
			}
		})
	}

	// Semantic tokens: the bindings are variables, the error's case an
	// enum member.
	var toks SemanticTokens
	if err := c.call("textDocument/semanticTokens/full", &SemanticTokensParams{TextDocument: TextDocumentIdentifier{URI: uri}}, &toks); err != nil {
		t.Fatal(err)
	}
	m := newMapper([]byte(text), false)
	kinds := map[string]string{}
	line, char := 0, 0
	for i := 0; i+4 < len(toks.Data); i += 5 {
		d := toks.Data[i : i+5]
		if d[0] > 0 {
			line, char = line+int(d[0]), int(d[1])
		} else {
			char += int(d[1])
		}
		start := m.offset(Position{Line: line, Character: char})
		kinds[text[start:start+int(d[2])]+"@"+itoa(line)] = tokenTypes[d[3]]
	}
	lineOf := func(s string) int { return strings.Count(text[:strings.Index(text, s)], "\n") }
	for _, tc := range []struct{ name, on, want string }{
		{"found", "let found", "variable"},
		{"which", "catch MailError", "variable"},
		{"missing", "catch MailError", "enumMember"},
		{"b", "let b = second", "variable"},
		{"l", "if let l = left", "variable"},
		{"r", "Text(\"{l} {r}\")", "variable"},
	} {
		if got := kinds[tc.name+"@"+itoa(lineOf(tc.on))]; got != tc.want {
			t.Errorf("%s on %q: %q, want %q", tc.name, tc.on, got, tc.want)
		}
	}

	// Completion: a chain's later clause sees the earlier names.
	version := 1
	marked := strings.Replace(text, "let b = second, a < b", "let b = second, |", 1)
	i := strings.IndexByte(marked, '|')
	edited := marked[:i] + marked[i+1:]
	got := labels(complete(t, c, uri, &version, edited, marked[:i+1]))
	for _, w := range []string{"a", "b", "first"} {
		if !contains(got, w) {
			t.Errorf("no %q in %v", w, got)
		}
	}
}

func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	s := ""
	for ; n > 0; n /= 10 {
		s = string(rune('0'+n%10)) + s
	}
	return s
}
