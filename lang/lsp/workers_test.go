package lsp

import (
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

// A worker in the editor: its declaration's hover, its members' (a var,
// a pub fn), completion of its pub fns from outside and of its body's
// keywords and members inside, and definitions.
func TestWorkers(t *testing.T) {
	root := t.TempDir()
	c := newClient(t, root)
	text := `/// Words to the notes that have them.
worker SearchIndex(language: string) {
  var words: [string: [int]] = [:]
  let stop = ["the", language]

  pub fn add(id: int, text: string) {
    for w in text.split(" ") { words[w] = [id] }
  }
  pub fn find(q: string) -> [int] = words[q] ?? []
  fn clear() { words = [:] }
}

store Notes {
  let index = SearchIndex(language: "en")
  state results: [int] = []

  pub async fn search(q: string) { results = await index.find(q) }
}
`
	uri := c.openText(filepath.Join(root, "a.asuka"), text)
	c.waitDiags(uri, noDiags)
	for _, tc := range []struct{ at, want string }{
		{"worker Search|Index", "worker SearchIndex(language: string)"},
		{"worker Search|Index", "Words to the notes that have them."},
		{"let index = Search|Index(", "worker SearchIndex(language: string)"},
		{"{ wor|ds[w] = [id] }", "var words: [string: [int]]"},
		{"await index.fi|nd(q)", "pub fn SearchIndex.find(q: string) -> [int]"},
	} {
		if got := hoverAt(t, c, uri, text, tc.at); !strings.Contains(got, tc.want) {
			t.Errorf("hover at %s: %q lacks %q", tc.at, got, tc.want)
		}
	}
	for _, tc := range []struct{ at, decl string }{
		{"await index.fi|nd(q)", "pub fn |find("},
		{"{ wor|ds[w] = [id] }", "var |words"},
		{"let index = Search|Index(", "worker |SearchIndex"},
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
	version := 1
	// at completes doc at the | of marked, a part of it.
	at := func(doc, marked string) []string {
		t.Helper()
		bare := strings.Replace(marked, "|", "", 1)
		if !strings.Contains(doc, bare) {
			t.Fatalf("%q isn't in the document", bare)
		}
		return labels(complete(t, c, uri, &version, doc, strings.Replace(doc, bare, marked, 1)))
	}
	// From outside: its pub fns only.
	got := at(strings.Replace(text, "await index.find(q)", "await index.", 1), "await index.|")
	for _, w := range []string{"add", "find"} {
		if !slices.Contains(got, w) {
			t.Errorf("completion after index.: %v lacks %s", got, w)
		}
	}
	for _, w := range []string{"clear", "words", "stop"} {
		if slices.Contains(got, w) {
			t.Errorf("completion after index.: %v has %s, the worker's own", got, w)
		}
	}
	// In its body: its keywords; in its fns, its members.
	body := strings.Replace(text, "  fn clear() { words = [:] }\n", "  \n", 1)
	got = at(body, "  pub fn find(q: string) -> [int] = words[q] ?? []\n  |")
	for _, w := range []string{"var", "let", "fn", "pub"} {
		if !slices.Contains(got, w) {
			t.Errorf("completion in a worker's body: %v lacks %s", got, w)
		}
	}
	if slices.Contains(got, "state") {
		t.Errorf("completion in a worker's body: %v has state", got)
	}
	inFn := strings.Replace(text, "fn clear() { words = [:] }", "fn clear() { w }", 1)
	got = at(inFn, "fn clear() { w| }")
	if !slices.Contains(got, "words") {
		t.Errorf("completion in a worker's fn: %v lacks words", got)
	}
}
