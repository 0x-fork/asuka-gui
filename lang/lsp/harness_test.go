package lsp

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"
)

// A client drives a server in-process, over pipes, with LSP messages.
type client struct {
	t      *testing.T
	conn   *conn
	mu     sync.Mutex
	nextID int
	waits  map[string]chan *message
	notes  chan *message
	done   chan error
	cancel context.CancelFunc

	diags map[string][]Diagnostic // the latest published, by URI
	seen  map[string]int          // how many times each URI's were published
	log   *syncBuffer
}

type syncBuffer struct {
	mu sync.Mutex
	b  strings.Builder
}

func (b *syncBuffer) Write(p []byte) (int, error) {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.b.Write(p)
}

func (b *syncBuffer) String() string {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.b.String()
}

func newClient(t *testing.T, roots ...string) *client {
	t.Helper()
	cr, sw := io.Pipe()
	sr, cw := io.Pipe()
	ctx, cancel := context.WithCancel(context.Background())
	c := &client{t: t, conn: newConn(cr, cw), waits: map[string]chan *message{}, notes: make(chan *message), done: make(chan error, 1), cancel: cancel, diags: map[string][]Diagnostic{}, seen: map[string]int{}, log: &syncBuffer{}}
	go func() {
		c.done <- Serve(ctx, sr, sw, &Options{Debounce: 5 * time.Millisecond, Log: c.log})
		sw.Close()
	}()
	// Notifications queue without bound, so the reader never blocks on
	// ones no test waits for (a test that only makes requests gets a
	// publishDiagnostics for each change).
	in := make(chan *message)
	go func(in chan *message) {
		var q []*message
		for in != nil || len(q) > 0 {
			var out chan *message
			var head *message
			if len(q) > 0 {
				out, head = c.notes, q[0]
			}
			select {
			case m, ok := <-in:
				if !ok {
					in = nil
					continue
				}
				q = append(q, m)
			case out <- head:
				q = q[1:]
			}
		}
		close(c.notes)
	}(in)
	go func() {
		for {
			m, err := c.conn.read()
			if err != nil {
				close(in)
				return
			}
			if m.Method != "" {
				in <- m
				continue
			}
			c.mu.Lock()
			ch := c.waits[string(m.ID)]
			delete(c.waits, string(m.ID))
			c.mu.Unlock()
			if ch != nil {
				ch <- m
			}
		}
	}()
	t.Cleanup(func() {
		c.notify("exit", nil)
		select {
		case err := <-c.done:
			if err != nil {
				t.Errorf("server: %v", err)
			}
		case <-time.After(10 * time.Second):
			t.Error("the server didn't exit")
		}
		cancel()
		cw.Close()
		// The server survives a panic, and logs it: a test fails on one.
		if l := c.log.String(); strings.Contains(l, "panic") {
			t.Errorf("the server panicked:\n%s", l)
		}
	})
	var folders []WorkspaceFolder
	for _, r := range roots {
		folders = append(folders, WorkspaceFolder{URI: pathToURI(r), Name: filepath.Base(r)})
	}
	var res InitializeResult
	c.call("initialize", map[string]any{
		"processId":        nil,
		"workspaceFolders": folders,
		"capabilities": map[string]any{
			"textDocument": map[string]any{"completion": map[string]any{"completionItem": map[string]any{"snippetSupport": true}}},
		},
	}, &res)
	if !res.Capabilities.HoverProvider || res.Capabilities.PositionEncoding != "utf-16" {
		t.Fatalf("initialize: %+v", res)
	}
	c.notify("initialized", map[string]any{})
	return c
}

func (c *client) call(method string, params, result any) *rpcError {
	c.t.Helper()
	c.mu.Lock()
	c.nextID++
	id := json.RawMessage(fmt.Sprint(c.nextID))
	ch := make(chan *message, 1)
	c.waits[string(id)] = ch
	c.mu.Unlock()
	p, _ := json.Marshal(params)
	if err := c.conn.write(&message{ID: id, Method: method, Params: p}); err != nil {
		c.t.Fatal(err)
	}
	select {
	case m := <-ch:
		if m.Error != nil {
			return m.Error
		}
		if result != nil {
			if err := json.Unmarshal(m.Result, result); err != nil {
				c.t.Fatalf("%s: %v: %s", method, err, m.Result)
			}
		}
		return nil
	case <-time.After(20 * time.Second):
		c.t.Fatalf("%s: no response", method)
	}
	return nil
}

func (c *client) notify(method string, params any) {
	p, _ := json.Marshal(params)
	c.conn.write(&message{Method: method, Params: p})
}

// waitDiags waits until uri's diagnostics satisfy ok, and returns them.
func (c *client) waitDiags(uri string, ok func([]Diagnostic) bool) []Diagnostic {
	c.t.Helper()
	if d, have := c.diags[uri]; have && ok(d) {
		return d
	}
	timeout := time.After(20 * time.Second)
	for {
		select {
		case m, open := <-c.notes:
			if !open {
				c.t.Fatal("the server closed the connection")
			}
			if m.Method != "textDocument/publishDiagnostics" {
				continue
			}
			var p PublishDiagnosticsParams
			json.Unmarshal(m.Params, &p)
			c.diags[p.URI] = p.Diagnostics
			c.seen[p.URI]++
			if p.URI == uri && ok(p.Diagnostics) {
				return p.Diagnostics
			}
		case <-timeout:
			c.t.Fatalf("%s: diagnostics never satisfied the test (last: %+v)", uri, c.diags[uri])
		}
	}
}

func (c *client) open(path string) (uri, text string) {
	c.t.Helper()
	b, err := os.ReadFile(path)
	if err != nil {
		c.t.Fatal(err)
	}
	uri = pathToURI(path)
	c.notify("textDocument/didOpen", map[string]any{"textDocument": TextDocumentItem{URI: uri, LanguageID: "asuka", Version: 1, Text: string(b)}})
	return uri, string(b)
}

func (c *client) openText(path, text string) string {
	uri := pathToURI(path)
	c.notify("textDocument/didOpen", map[string]any{"textDocument": TextDocumentItem{URI: uri, LanguageID: "asuka", Version: 1, Text: text}})
	return uri
}

// change replaces the document's text.
func (c *client) change(uri string, version int, text string) {
	c.notify("textDocument/didChange", map[string]any{
		"textDocument":   VersionedTextDocumentIdentifier{URI: uri, Version: version},
		"contentChanges": []map[string]any{{"text": text}},
	})
}

// posAt is the position of the | in marked, a piece of text.
func posAt(t *testing.T, text, marked string) Position {
	t.Helper()
	i := strings.IndexByte(marked, '|')
	if i < 0 {
		t.Fatalf("no | in %q", marked)
	}
	needle := marked[:i] + marked[i+1:]
	j := strings.Index(text, needle)
	if j < 0 {
		t.Fatalf("%q isn't in the text", needle)
	}
	if strings.Contains(text[j+1:], needle) {
		t.Fatalf("%q is in the text twice", needle)
	}
	return newMapper([]byte(text), false).position(j + i)
}

func tdp(uri string, p Position) TextDocumentPositionParams {
	return TextDocumentPositionParams{TextDocument: TextDocumentIdentifier{URI: uri}, Position: p}
}

// copyTree copies testdata's dir into a temporary directory, so tests
// may change it.
func copyTree(t *testing.T, dir string) string {
	t.Helper()
	dst := t.TempDir()
	err := filepath.Walk(dir, func(p string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		rel, _ := filepath.Rel(dir, p)
		out := filepath.Join(dst, rel)
		if info.IsDir() {
			return os.MkdirAll(out, 0o755)
		}
		b, err := os.ReadFile(p)
		if err != nil {
			return err
		}
		return os.WriteFile(out, b, 0o644)
	})
	if err != nil {
		t.Fatal(err)
	}
	// Resolve symlinks (macOS's /var → /private/var): the server sees
	// paths as the client gives them.
	if r, err := filepath.EvalSymlinks(dst); err == nil {
		dst = r
	}
	return dst
}
