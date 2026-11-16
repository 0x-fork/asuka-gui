package markdown

import (
	"fmt"
	"strings"
	"testing"
)

func TestParagraphs(t *testing.T) {
	ps := paragraphs("# Plan\n\nThree **steps**, *in* `order`:\n\n1. Read [docs](https://example.com) and [no](javascript:x)\n2. Write\n   - [x] nested\n\n> quoted\n\n---\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n```go\nfmt.Println(1)\n```\n")
	var b strings.Builder
	for _, p := range ps {
		fmt.Fprintf(&b, "%d %q h%d code=%v rule=%v cells=%v:", p.indent, p.marker, p.heading, p.code, p.rule, p.cells)
		for _, r := range p.runs {
			fmt.Fprintf(&b, " [%s", r.text)
			if r.bold {
				b.WriteString(" b")
			}
			if r.italic {
				b.WriteString(" i")
			}
			if r.mono {
				b.WriteString(" m")
			}
			if r.link != "" {
				b.WriteString(" ->" + r.link)
			}
			if r.quote {
				b.WriteString(" q")
			}
			b.WriteString("]")
		}
		b.WriteString("\n")
	}
	want := `0 "" h1 code=false rule=false cells=false: [Plan]
0 "" h0 code=false rule=false cells=false: [Three ] [steps b] [, ] [in i] [ ] [order m] [:]
1 "1." h0 code=false rule=false cells=false: [Read ] [docs ->https://example.com] [ and ] [no]
1 "2." h0 code=false rule=false cells=false: [Write]
2 "• ☑" h0 code=false rule=false cells=false: [nested]
1 "" h0 code=false rule=false cells=false: [quoted q]
0 "" h0 code=false rule=true cells=false:
0 "" h0 code=false rule=false cells=true: [a b] [	] [b b]
0 "" h0 code=false rule=false cells=true: [1] [	] [2]
0 "" h0 code=true rule=false cells=false: [fmt.Println(1) m]
`
	if got := b.String(); got != want {
		t.Errorf("got\n%s\nwant\n%s", got, want)
	}
}
