//go:build linux && !android

package markdown

import (
	"fmt"
	"strings"

	. "github.com/egoist/asuka/internal/gtk"
)

// Linux: the paragraphs as Pango markup in a GtkLabel that wraps and is
// selectable: one selection, links GTK opens, the theme's font and its
// text scaling.

type gtkDocument struct {
	label  Ptr
	source string
	set    bool
}

func newDocument(events DocumentEvents) DocumentNative {
	l := LabelNew("")
	LabelSetWrap(l, true)
	LabelSetWrapMode(l, 2) // PANGO_WRAP_WORD_CHAR
	LabelSetSelectable(l, true)
	LabelSetXAlign(l, 0)
	LabelSetYAlign(l, 0)
	return &gtkDocument{label: l}
}

func (v *gtkDocument) Handle() uintptr { return v.label }

func (v *gtkDocument) SetText(s string) {
	if v.set && s == v.source {
		return
	}
	v.source, v.set = s, true
	LabelSetMarkup(v.label, pango(paragraphs(s)))
	WidgetQueueResize(v.label)
}

// Measure is its height at the width it's given (its natural width when
// it's given none).
func (v *gtkDocument) Measure(w, h float32) (float32, float32) {
	var mn, nat int32
	if w != w { // NaN: as wide as it likes
		Call(FnWidgetMeasure, v.label, OrientationHorizontal, ^uintptr(0), Addr(&mn), Addr(&nat), 0, 0)
		w = float32(nat)
	}
	Call(FnWidgetMeasure, v.label, OrientationVertical, uintptr(int32(w)), Addr(&mn), Addr(&nat), 0, 0)
	return w, float32(nat)
}

var headingSizes = [...]string{"xx-large", "x-large", "large", "medium", "medium", "medium"}

// pango is the paragraphs as Pango markup.
func pango(ps []para) string {
	var b strings.Builder
	for i, p := range ps {
		if i > 0 {
			b.WriteString("\n")
			if !ps[i-1].tight || !p.tight {
				b.WriteString("\n")
			}
		}
		b.WriteString(strings.Repeat("    ", max(p.indent-1, 0)))
		if p.marker != "" {
			b.WriteString(escape(p.marker) + " ")
		} else if p.indent > 0 {
			b.WriteString("    ")
		}
		if p.rule {
			b.WriteString(`<span fgalpha="35%">` + strings.Repeat("―", 24) + `</span>`)
			continue
		}
		if p.heading > 0 {
			fmt.Fprintf(&b, `<span size="%s" weight="bold">`, headingSizes[min(p.heading, 6)-1])
		}
		if p.code {
			b.WriteString(`<span font_family="monospace" bgalpha="12%" background="#808080">`)
		}
		for _, r := range p.runs {
			text := escape(r.text)
			if p.code {
				// Each line of the block indented as the block is.
				text = strings.ReplaceAll(text, "\n", "\n"+strings.Repeat("    ", p.indent))
			}
			b.WriteString(span(r, text))
		}
		if p.code {
			b.WriteString(`</span>`)
		}
		if p.heading > 0 {
			b.WriteString(`</span>`)
		}
	}
	return b.String()
}

// span is a run's text in its style.
func span(r run, text string) string {
	if r.mono {
		text = `<span font_family="monospace" bgalpha="12%" background="#808080">` + text + `</span>`
	}
	if r.bold {
		text = "<b>" + text + "</b>"
	}
	if r.italic {
		text = "<i>" + text + "</i>"
	}
	if r.strike {
		text = "<s>" + text + "</s>"
	}
	if r.quote {
		text = `<span fgalpha="65%">` + text + `</span>`
	}
	if r.link != "" {
		text = `<a href="` + escape(r.link) + `">` + text + `</a>`
	}
	return text
}

var escaper = strings.NewReplacer("&", "&amp;", "<", "&lt;", ">", "&gt;", `"`, "&quot;", "'", "&#39;")

func escape(s string) string { return escaper.Replace(s) }
