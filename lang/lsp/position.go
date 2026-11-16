package lsp

import (
	"net/url"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"unicode/utf8"
)

// A mapper converts between byte offsets in a file's text and LSP
// positions: a line, and a column counted in the negotiated encoding's
// units (UTF-16's by default, as LSP says; UTF-8's when the client
// takes them).
type mapper struct {
	src   []byte
	lines []int // each line's first byte
	utf8  bool
}

func newMapper(src []byte, utf8 bool) *mapper {
	m := &mapper{src: src, lines: []int{0}, utf8: utf8}
	for i, b := range src {
		if b == '\n' {
			m.lines = append(m.lines, i+1)
		}
	}
	return m
}

// lineEnd is where line i's text ends (before its newline).
func (m *mapper) lineEnd(i int) int {
	end := len(m.src)
	if i+1 < len(m.lines) {
		end = m.lines[i+1] - 1
	}
	if end > m.lines[i] && end-1 < len(m.src) && end > 0 && m.src[end-1] == '\r' {
		end--
	}
	return end
}

// offset is p's byte offset, clamped to the text.
func (m *mapper) offset(p Position) int {
	if p.Line < 0 {
		return 0
	}
	if p.Line >= len(m.lines) {
		return len(m.src)
	}
	start, end := m.lines[p.Line], m.lineEnd(p.Line)
	if m.utf8 {
		return min(start+max(p.Character, 0), end)
	}
	units := 0
	for i := start; i < end; {
		if units >= p.Character {
			return i
		}
		r, size := utf8.DecodeRune(m.src[i:])
		if r >= 0x10000 {
			units += 2
		} else {
			units++
		}
		i += size
	}
	return end
}

// position is the position of byte offset off.
func (m *mapper) position(off int) Position {
	off = max(0, min(off, len(m.src)))
	line := sort.Search(len(m.lines), func(i int) bool { return m.lines[i] > off }) - 1
	start := m.lines[line]
	if m.utf8 {
		return Position{Line: line, Character: off - start}
	}
	units := 0
	for i := start; i < off; {
		r, size := utf8.DecodeRune(m.src[i:])
		if r >= 0x10000 {
			units += 2
		} else {
			units++
		}
		i += size
	}
	return Position{Line: line, Character: units}
}

func (m *mapper) rng(start, end int) Range {
	return Range{Start: m.position(start), End: m.position(end)}
}

// units is the length of s in the encoding's units.
func units(s string, utf8Enc bool) int {
	if utf8Enc {
		return len(s)
	}
	n := 0
	for _, r := range s {
		if r >= 0x10000 {
			n += 2
		} else {
			n++
		}
	}
	return n
}

// uriToPath is a file: URI's path ("" for another scheme).
func uriToPath(uri string) string {
	u, err := url.Parse(uri)
	if err != nil || u.Scheme != "file" {
		return ""
	}
	p := u.Path
	if runtime.GOOS == "windows" {
		// file:///c:/x → c:/x
		p = strings.TrimPrefix(p, "/")
	}
	return filepath.Clean(filepath.FromSlash(p))
}

// pathToURI is the file: URI of an absolute path.
func pathToURI(path string) string {
	p := filepath.ToSlash(path)
	if !strings.HasPrefix(p, "/") {
		p = "/" + p // c:/x
	}
	return (&url.URL{Scheme: "file", Path: p}).String()
}
