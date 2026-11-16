// asuka/markdown on Windows (a rich edit, markdown_windows.go) and Linux
// (a label's Pango markup, markdown_linux.go); macOS runs the package's
// Swift. Both read the text with Asuka's parser (sdk/markdown/parser:
// CommonMark, GitHub's extensions, footnotes and math) into styled
// paragraphs, here.
package markdown

import (
	"strconv"
	"strings"

	"github.com/egoist/asuka/sdk/markdown/parser"
)

// run is styled text.
type run struct {
	text                       string
	bold, italic, mono, strike bool
	link                       string
	quote                      bool // secondary text
}

// para is a paragraph: its runs, how deep in lists and quotes it is, its
// list marker, and what kind of block it's of.
type para struct {
	runs    []run
	indent  int
	marker  string
	heading int  // 1 to 6
	code    bool // a code block's lines (runs are its text, monospaced)
	rule    bool // a thematic break
	cells   bool // a table row: its cells separated by tabs
	tight   bool // little space after it (a tight list's item)
}

// paragraphs is the Markdown as paragraphs, in order.
func paragraphs(src string) []para {
	d := parser.Parse(src)
	var out []para
	var walk func(i, indent int, quote bool, marker string, tight bool)
	walk = func(i, indent int, quote bool, marker string, tight bool) {
		b := d.Block(i)
		switch b.Kind {
		case parser.Paragraph, parser.Heading:
			p := para{indent: indent, marker: marker, tight: tight}
			if b.Kind == parser.Heading {
				p.heading = b.Level
			}
			p.runs = inlineRuns(d.Inlines(i), quote)
			out = append(out, p)
		case parser.CodeBlock, parser.MathBlock, parser.HTMLBlock:
			text := strings.TrimSuffix(d.Lines(i), "\n")
			out = append(out, para{indent: indent, marker: marker, code: b.Kind != parser.HTMLBlock,
				runs: []run{{text: text, mono: b.Kind != parser.HTMLBlock, quote: quote}}})
		case parser.ThematicBreak:
			out = append(out, para{rule: true, indent: indent})
		case parser.Quote:
			first := true
			for c := range d.Children(i) {
				m := ""
				if first {
					m, first = marker, false
				}
				walk(c, indent+1, true, m, false)
			}
		case parser.List:
			n := b.Number
			for c := range d.Children(i) {
				m := "•"
				if b.Ordered {
					m = strconv.Itoa(n) + "."
					n++
				}
				if t := d.Block(c).Task; t == parser.Checked {
					m += " ☑"
				} else if t == parser.Unchecked {
					m += " ☐"
				}
				walk(c, indent+1, quote, m, b.Tight)
			}
		case parser.Item, parser.FootnoteDefinition:
			first := true
			if b.Kind == parser.FootnoteDefinition {
				marker = "[" + b.Label() + "]"
				indent++
			}
			for c := range d.Children(i) {
				m := ""
				if first {
					m, first = marker, false
				}
				walk(c, indent, quote, m, tight)
			}
		case parser.Table:
			for r := range d.Children(i) {
				row := para{indent: indent, cells: true, tight: true}
				header := d.Block(r).Header
				k := 0
				for c := range d.Children(r) {
					if k > 0 {
						row.runs = append(row.runs, run{text: "\t"})
					}
					for _, x := range inlineRuns(d.Inlines(c), quote) {
						x.bold = x.bold || header
						row.runs = append(row.runs, x)
					}
					k++
				}
				out = append(out, row)
			}
		default:
			for c := range d.Children(i) {
				walk(c, indent, quote, marker, tight)
			}
		}
	}
	walk(0, 0, false, "", false)
	return out
}

// inlineRuns is a block's inlines as runs.
func inlineRuns(in []parser.Inline, quote bool) []run {
	var out []run
	var walk func(from, to int, st run)
	walk = func(from, to int, st run) {
		for i := from; i < to; {
			x := in[i]
			next := max(x.Next, i+1)
			s := st
			switch x.Kind {
			case parser.Text, parser.HTML:
				s.text = x.Text
				out = append(out, s)
			case parser.SoftBreak:
				s.text = " "
				out = append(out, s)
			case parser.LineBreak:
				s.text = "\n"
				out = append(out, s)
			case parser.Code, parser.Math:
				s.text, s.mono = x.Text, true
				out = append(out, s)
			case parser.FootnoteRef:
				s.text = "[" + x.Text + "]"
				out = append(out, s)
			case parser.Emphasis:
				s.italic = true
				walk(i+1, next, s)
			case parser.Strong:
				s.bold = true
				walk(i+1, next, s)
			case parser.Strikethrough:
				s.strike = true
				walk(i+1, next, s)
			case parser.Link:
				if safeLink(x.Dest) {
					s.link = x.Dest
				}
				walk(i+1, next, s)
			case parser.Image:
				walk(i+1, next, s) // its description
			default:
				walk(i+1, next, s)
			}
			i = next
		}
	}
	walk(0, len(in), run{quote: quote})
	return out
}

// safeLink says whether a link opens: a web, mail or phone address.
func safeLink(dest string) bool {
	d := strings.ToLower(strings.TrimSpace(dest))
	for _, p := range []string{"http://", "https://", "mailto:", "tel:"} {
		if strings.HasPrefix(d, p) {
			return true
		}
	}
	return false
}
