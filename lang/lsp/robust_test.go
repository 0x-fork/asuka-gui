package lsp

import (
	"math/rand"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// Code being typed is broken code: every request on every prefix of a
// file (and on files with lines dropped or doubled) answers, and the
// server never panics (the harness fails a test on a panic it logs).
func TestBrokenInput(t *testing.T) {
	var files []string
	for _, pat := range []string{"../compile/testdata/ui/*.asuka", "../compile/testdata/corpus/*.asuka", "../syntax/testdata/*.asuka", "testdata/app/*.asuka"} {
		m, _ := filepath.Glob(pat)
		files = append(files, m...)
	}
	if len(files) == 0 {
		t.Fatal("no files")
	}
	cuts := 30
	if testing.Short() {
		cuts = 6
	}
	root := t.TempDir()
	c := newClient(t, root)
	path := filepath.Join(root, "a.asuka")
	uri := c.openText(path, "")
	version := 1
	rnd := rand.New(rand.NewSource(1))
	try := func(text string) {
		version++
		c.change(uri, version, text)
		m := newMapper([]byte(text), false)
		for k := 0; k < 4; k++ {
			off := 0
			if len(text) > 0 {
				off = rnd.Intn(len(text) + 1)
			}
			if k == 0 {
				off = len(text)
			}
			p := tdp(uri, m.position(off))
			var any1 any
			c.call("textDocument/hover", &p, &any1)
			c.call("textDocument/completion", &CompletionParams{TextDocumentPositionParams: p}, &any1)
			c.call("textDocument/definition", &p, &any1)
			c.call("textDocument/signatureHelp", &p, &any1)
			c.call("textDocument/documentHighlight", &p, &any1)
			c.call("textDocument/prepareRename", &p, &any1)
		}
		doc := TextDocumentIdentifier{URI: uri}
		var any1 any
		c.call("textDocument/semanticTokens/full", &SemanticTokensParams{TextDocument: doc}, &any1)
		c.call("textDocument/documentSymbol", &DocumentSymbolParams{TextDocument: doc}, &any1)
		c.call("textDocument/formatting", &DocumentFormattingParams{TextDocument: doc}, &any1)
	}
	for _, f := range files {
		b, err := os.ReadFile(f)
		if err != nil {
			t.Fatal(err)
		}
		src := string(b)
		// Prefixes: the file as it's typed.
		for i := 0; i <= cuts; i++ {
			try(src[:len(src)*i/cuts])
		}
		// Lines dropped and doubled: edits half done.
		lines := strings.SplitAfter(src, "\n")
		for i := 0; i < cuts/2 && len(lines) > 1; i++ {
			j := rnd.Intn(len(lines))
			mut := append([]string{}, lines[:j]...)
			if i%2 == 0 {
				mut = append(mut, lines[j+1:]...)
			} else {
				mut = append(mut, lines[j], lines[j])
				mut = append(mut, lines[j+1:]...)
			}
			try(strings.Join(mut, ""))
		}
		// Characters deleted mid-line: a . with nothing after it, an
		// unclosed brace or string.
		for i := 0; i < cuts/2 && len(src) > 2; i++ {
			j := rnd.Intn(len(src) - 1)
			try(src[:j] + src[j+1+rnd.Intn(min(8, len(src)-j-1)):])
		}
	}
}
