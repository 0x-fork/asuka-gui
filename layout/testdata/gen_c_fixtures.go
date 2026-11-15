//go:build ignore

// gen_c_fixtures writes core/test/testdata/layout_fixtures.txt: the Chrome
// fixtures css_test.go runs (testdata/css/*/*.xml, testdata/ltr_chrome.xml
// and testdata/rtl_chrome.xml), parsed as css_test.go parses them, in a
// format C reads a token at a time, with what Chrome drew and what the Go
// engine lays out. The C core's layout test (core/test/layout_test.c)
// checks its engine against both. From the repository's root:
//
//	go run layout/testdata/gen_c_fixtures.go
//
// The format, one record per line, tokens separated by spaces:
//
//	D n tokens...                  the default style's n fixed fields (first)
//	F name rounding vw vh gopass   a fixture: rounding 0 or 1; the viewport
//	                               (inf: max-content, -inf: min-content);
//	                               gopass 1 when the Go engine matches Chrome
//	N nkids style...               a node in preorder, then its nkids children
//	                               (style: see writer.style)
//	E nkids x y w h                Chrome's frames, in preorder
//	G nkids x y w h                the Go engine's, in preorder over the tree
//	.                              the end of the fixture
package main

import (
	"bufio"
	"encoding/xml"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	. "github.com/egoist/asuka/layout"
)

type xNode struct {
	XMLName xml.Name
	Attrs   []xml.Attr `xml:",any,attr"`
	Text    string     `xml:",chardata"`
	Kids    []xNode    `xml:",any"`
}

func (x *xNode) attr(name string) (string, bool) {
	for _, a := range x.Attrs {
		if a.Name.Local == name {
			return strings.TrimSpace(a.Value), true
		}
	}
	return "", false
}

type xTest struct {
	Name     string `xml:"name,attr"`
	Rounding string `xml:"use-rounding,attr"`
	Viewport struct {
		Width  string `xml:"width,attr"`
		Height string `xml:"height,attr"`
	} `xml:"viewport"`
	Input  xNode `xml:"input"`
	Expect xNode `xml:"expectations"`
}

// The parsing below is css_test.go's.

func parseLength(s string) Length {
	switch {
	case s == "" || s == "auto" || s == "none":
		return Auto
	case strings.HasSuffix(s, "%"):
		v, _ := strconv.ParseFloat(strings.TrimSuffix(s, "%"), 32)
		return Pct(float32(v))
	case strings.HasSuffix(s, "px"):
		v, _ := strconv.ParseFloat(strings.TrimSuffix(s, "px"), 32)
		return Pt(float32(v))
	}
	if v, err := strconv.ParseFloat(s, 32); err == nil {
		return Pt(float32(v))
	}
	return Auto
}

func parseAlign(s string) Align {
	s = strings.TrimPrefix(strings.TrimPrefix(s, "safe "), "unsafe ")
	switch s {
	case "start", "left":
		return AlignStart
	case "end", "right":
		return AlignEnd
	case "self-start":
		return AlignSelfStart
	case "self-end":
		return AlignSelfEnd
	case "flex-start":
		return AlignFlexStart
	case "flex-end":
		return AlignFlexEnd
	case "center":
		return AlignCenter
	case "baseline", "first baseline", "last baseline":
		return AlignBaseline
	case "stretch":
		return AlignStretch
	}
	return AlignAuto
}

func parseContent(s string) Content {
	s = strings.TrimPrefix(strings.TrimPrefix(s, "safe "), "unsafe ")
	switch s {
	case "start", "left":
		return ContentStart
	case "end", "right":
		return ContentEnd
	case "flex-start":
		return ContentFlexStart
	case "flex-end":
		return ContentFlexEnd
	case "center":
		return ContentCenter
	case "stretch":
		return ContentStretch
	case "space-between":
		return ContentSpaceBetween
	case "space-around":
		return ContentSpaceAround
	case "space-evenly":
		return ContentSpaceEvenly
	}
	return ContentNormal
}

func parseOverflow(s string) Overflow {
	switch s {
	case "hidden":
		return OverflowHidden
	case "clip":
		return OverflowClip
	case "scroll", "auto":
		return OverflowScroll
	}
	return OverflowVisible
}

func splitTop(s string) []string {
	var out []string
	depth, start := 0, 0
	for i, r := range s {
		switch r {
		case '(', '[':
			depth++
		case ')', ']':
			depth--
		case ' ':
			if depth == 0 {
				if i > start {
					out = append(out, s[start:i])
				}
				start = i + 1
			}
		}
	}
	if start < len(s) {
		out = append(out, s[start:])
	}
	return out
}

func parseBreadth(s string) Breadth {
	s = strings.TrimSpace(s)
	switch {
	case s == "auto":
		return BAuto()
	case s == "min-content":
		return BMinContent()
	case s == "max-content":
		return BMaxContent()
	case strings.HasSuffix(s, "fr"):
		v, _ := strconv.ParseFloat(strings.TrimSuffix(s, "fr"), 32)
		return BFr(float32(v))
	case strings.HasSuffix(s, "%"):
		v, _ := strconv.ParseFloat(strings.TrimSuffix(s, "%"), 32)
		return BPct(float32(v))
	case strings.HasSuffix(s, "px"):
		v, _ := strconv.ParseFloat(strings.TrimSuffix(s, "px"), 32)
		return BPx(float32(v))
	}
	v, _ := strconv.ParseFloat(s, 32)
	return BPx(float32(v))
}

func parseTrackSize(s string) TrackSize {
	switch {
	case strings.HasPrefix(s, "minmax("):
		args := strings.SplitN(strings.TrimSuffix(strings.TrimPrefix(s, "minmax("), ")"), ",", 2)
		return Minmax(parseBreadth(args[0]), parseBreadth(args[1]))
	case strings.HasPrefix(s, "fit-content("):
		arg := strings.TrimSuffix(strings.TrimPrefix(s, "fit-content("), ")")
		b := parseBreadth(arg)
		if b.Kind == BreadthPercent {
			return FitContentPct(b.V)
		}
		return FitContent(b.V)
	}
	b := parseBreadth(s)
	if b.Kind == BreadthFr {
		return Fr(b.V)
	}
	return TrackSize{Min: b, Max: b}
}

func parseTracks(s string) []GridTrack {
	var out []GridTrack
	for _, tok := range splitTop(s) {
		if strings.HasPrefix(tok, "[") || tok == "none" {
			continue
		}
		if strings.HasPrefix(tok, "repeat(") {
			inner := strings.TrimSuffix(strings.TrimPrefix(tok, "repeat("), ")")
			i := strings.Index(inner, ",")
			countS, rest := strings.TrimSpace(inner[:i]), strings.TrimSpace(inner[i+1:])
			count := 0
			switch countS {
			case "auto-fill":
				count = RepeatAutoFill
			case "auto-fit":
				count = RepeatAutoFit
			default:
				count, _ = strconv.Atoi(countS)
			}
			var sizes []TrackSize
			for _, t := range splitTop(rest) {
				if !strings.HasPrefix(t, "[") {
					sizes = append(sizes, parseTrackSize(t))
				}
			}
			out = append(out, Repeat(count, sizes...))
			continue
		}
		out = append(out, T(parseTrackSize(tok)))
	}
	return out
}

func parseAutoTracks(s string) []TrackSize {
	var out []TrackSize
	for _, t := range splitTop(s) {
		out = append(out, parseTrackSize(t))
	}
	return out
}

func parseLine(s string) GridLine {
	s = strings.TrimSpace(s)
	if strings.HasPrefix(s, "span ") {
		n, err := strconv.Atoi(strings.TrimPrefix(s, "span "))
		if err != nil {
			return GridLine{}
		}
		return Span(n)
	}
	if n, err := strconv.Atoi(s); err == nil {
		return Line(n)
	}
	return GridLine{}
}

func styleOf(x *xNode) Style {
	s := DefaultStyle
	get := func(k string) string { v, _ := x.attr(k); return v }
	switch get("display") {
	case "block", "flow-root":
		s.Display = DisplayBlock
	case "grid":
		s.Display = DisplayGrid
	case "none":
		s.Display = DisplayNone
	}
	if get("position") == "absolute" {
		s.Position = Absolute
	}
	switch get("direction") {
	case "ltr":
		s.Direction = DirLTR
	case "rtl":
		s.Direction = DirRTL
	}
	if get("box-sizing") == "content-box" {
		s.BoxSizing = ContentBox
	}
	s.OverflowX, s.OverflowY = parseOverflow(get("overflow-x")), parseOverflow(get("overflow-y"))
	if v, err := strconv.ParseFloat(get("scrollbar-width"), 32); err == nil {
		s.ScrollbarWidth = float32(v)
	}
	s.Width, s.Height = parseLength(get("width")), parseLength(get("height"))
	s.MinWidth, s.MinHeight = parseLength(get("min-width")), parseLength(get("min-height"))
	s.MaxWidth, s.MaxHeight = parseLength(get("max-width")), parseLength(get("max-height"))
	if v := get("aspect-ratio"); v != "" {
		if a, b, ok := strings.Cut(v, "/"); ok {
			x, _ := strconv.ParseFloat(strings.TrimSpace(a), 32)
			y, _ := strconv.ParseFloat(strings.TrimSpace(b), 32)
			s.AspectRatio = float32(x / y)
		} else {
			r, _ := strconv.ParseFloat(v, 32)
			s.AspectRatio = float32(r)
		}
	}
	edge := func(prefix string, zeroDefault bool) Rect[Length] {
		r := Rect[Length]{}
		def := Auto
		if zeroDefault {
			def = Pt(0)
		}
		for _, side := range []struct {
			name string
			dst  *Length
		}{{"top", &r.Top}, {"right", &r.Right}, {"bottom", &r.Bottom}, {"left", &r.Left}} {
			*side.dst = def
			if v, ok := x.attr(prefix + side.name); ok {
				*side.dst = parseLength(v)
			}
		}
		return r
	}
	s.Margin = edge("margin-", true)
	s.Padding = edge("padding-", true)
	s.Border = edge("border-", true)
	s.Inset = edge("", false)
	s.ColumnGap, s.RowGap = parseLength(get("column-gap")), parseLength(get("row-gap"))
	s.AlignItems, s.AlignSelf = parseAlign(get("align-items")), parseAlign(get("align-self"))
	s.JustifyItems, s.JustifySelf = parseAlign(get("justify-items")), parseAlign(get("justify-self"))
	s.AlignContent, s.JustifyContent = parseContent(get("align-content")), parseContent(get("justify-content"))
	switch get("flex-direction") {
	case "column":
		s.FlexDirection = Column
	case "row-reverse":
		s.FlexDirection = RowReverse
	case "column-reverse":
		s.FlexDirection = ColumnReverse
	}
	switch get("flex-wrap") {
	case "wrap":
		s.FlexWrap = Wrap
	case "wrap-reverse":
		s.FlexWrap = WrapReverse
	}
	if v, err := strconv.ParseFloat(get("flex-grow"), 32); err == nil {
		s.FlexGrow = float32(v)
	}
	if v, err := strconv.ParseFloat(get("flex-shrink"), 32); err == nil {
		s.FlexShrink = float32(v)
	}
	s.FlexBasis = parseLength(get("flex-basis"))
	s.GridTemplateColumns = parseTracks(get("grid-template-columns"))
	s.GridTemplateRows = parseTracks(get("grid-template-rows"))
	if v := get("grid-auto-columns"); v != "" {
		s.GridAutoColumns = parseAutoTracks(v)
	}
	if v := get("grid-auto-rows"); v != "" {
		s.GridAutoRows = parseAutoTracks(v)
	}
	switch get("grid-auto-flow") {
	case "column":
		s.GridAutoFlow = FlowColumn
	case "row dense", "dense":
		s.GridAutoFlow = FlowRowDense
	case "column dense":
		s.GridAutoFlow = FlowColumnDense
	}
	s.GridColumn = Placement{Start: parseLine(get("grid-column-start")), End: parseLine(get("grid-column-end"))}
	s.GridRow = Placement{Start: parseLine(get("grid-row-start")), End: parseLine(get("grid-row-end"))}
	return s
}

func isNaN(f float32) bool { return f != f }

func maxf(a, b float32) float32 {
	if a > b {
		return a
	}
	return b
}

func minf(a, b float32) float32 {
	if a < b {
		return a
	}
	return b
}

// words are a text's words in the Ahem font, split at zero-width spaces,
// in characters.
func words(text string) []int {
	var out []int
	for _, w := range strings.Split(text, "​") {
		out = append(out, len([]rune(w)))
	}
	return out
}

// ahem measures text in the Ahem font: 10×10 glyphs, breaking only at
// zero-width spaces.
func ahem(ws []int) MeasureFunc {
	minW, maxW := 0, 0
	for _, n := range ws {
		minW = max(minW, n)
		maxW += n
	}
	return func(c Constraints) Size {
		inline := c.Width
		if isNaN(inline) {
			switch {
			case c.AvailW == MinContentSpace:
				inline = float32(minW * 10)
			case c.AvailW == MaxContentSpace:
				inline = float32(maxW * 10)
			default:
				inline = minf(c.AvailW, float32(maxW*10))
			}
		}
		inline = maxf(inline, float32(minW*10))
		block := c.Height
		if isNaN(block) {
			perLine := int(math.Floor(float64(inline / 10)))
			lines, cur := 1, 0
			for _, n := range ws {
				if cur+n > perLine {
					if cur > 0 {
						lines++
					}
					cur = n
				} else {
					cur += n
				}
			}
			block = float32(lines * 10)
		}
		return Size{W: inline, H: block}
	}
}

func resolve0(l Length) float32 {
	switch l.U {
	case UnitPoint:
		return l.V
	case UnitPercent:
		return 0
	}
	return 0
}

func build(x *xNode) *Node {
	n := New()
	n.Style = styleOf(x)
	var kids []*Node
	for i := range x.Kids {
		kids = append(kids, build(&x.Kids[i]))
	}
	if len(kids) > 0 {
		n.SetChildren(kids)
	} else if t := strings.TrimSpace(x.Text); t != "" {
		n.SetMeasure(ahem(words(t)))
		st := &n.Style
		n.SetBaseline(func(Size) float32 { return resolve0(st.Padding.Top) + resolve0(st.Border.Top) + 8 })
	}
	return n
}

func abs(f float32) float32 {
	if f < 0 {
		return -f
	}
	return f
}

func matches(n *Node, x *xNode) bool {
	f := func(k string) float32 {
		v, _ := x.attr(k)
		r, _ := strconv.ParseFloat(v, 32)
		return float32(r)
	}
	want := [4]float32{f("x"), f("y"), f("width"), f("height")}
	got := [4]float32{n.X, n.Y, n.W, n.H}
	for i := range want {
		if abs(want[i]-got[i]) >= 0.1 {
			return false
		}
	}
	ok := true
	kids := n.Children()
	for i := range x.Kids {
		if i < len(kids) && !matches(kids[i], &x.Kids[i]) {
			ok = false
		}
	}
	return ok
}

func space(s string) float32 {
	switch s {
	case "", "max-content":
		return MaxContentSpace
	case "min-content":
		return MinContentSpace
	}
	v, _ := strconv.ParseFloat(strings.TrimSuffix(s, "px"), 32)
	return float32(v)
}

// Output.

type writer struct{ *bufio.Writer }

func num(v float32) string {
	switch {
	case math.IsInf(float64(v), 1):
		return "inf"
	case math.IsInf(float64(v), -1):
		return "-inf"
	case isNaN(v):
		return "nan"
	}
	return strconv.FormatFloat(float64(v), 'g', -1, 32)
}

func (w writer) f(v float32) { w.WriteString(" " + num(v)) }

func (w writer) i(v int) { w.WriteString(" " + strconv.Itoa(v)) }

// tokens collects a style's tokens.
type tokens []string

func (t *tokens) f(v float32) { *t = append(*t, num(v)) }
func (t *tokens) i(v int)     { *t = append(*t, strconv.Itoa(v)) }

func (t *tokens) length(l Length) {
	t.f(l.V)
	t.i(int(l.U))
}

func (t *tokens) rect(r Rect[Length]) {
	t.length(r.Top)
	t.length(r.Right)
	t.length(r.Bottom)
	t.length(r.Left)
}

func (t *tokens) trackSize(s TrackSize) {
	t.i(int(s.Min.Kind))
	t.f(s.Min.V)
	t.i(int(s.Max.Kind))
	t.f(s.Max.V)
}

func (t *tokens) line(l GridLine) {
	t.i(int(l.Kind))
	t.i(int(l.N))
}

// fixed is a style's fields of fixed size, in this order:
// display position box_sizing overflow_x overflow_y scrollbar_width;
// width height min_width min_height max_width max_height (each value unit)
// aspect_ratio; margin padding border inset (each top right bottom left);
// column_gap row_gap; align_items align_self justify_items justify_self
// align_content justify_content; direction flex_direction flex_wrap
// flex_grow flex_shrink flex_basis; grid_auto_flow grid_column (start end:
// kind n) grid_row.
func fixed(s *Style) tokens {
	var t tokens
	t.i(int(s.Display))
	t.i(int(s.Position))
	t.i(int(s.BoxSizing))
	t.i(int(s.OverflowX))
	t.i(int(s.OverflowY))
	t.f(s.ScrollbarWidth)
	for _, l := range []Length{s.Width, s.Height, s.MinWidth, s.MinHeight, s.MaxWidth, s.MaxHeight} {
		t.length(l)
	}
	t.f(s.AspectRatio)
	t.rect(s.Margin)
	t.rect(s.Padding)
	t.rect(s.Border)
	t.rect(s.Inset)
	t.length(s.ColumnGap)
	t.length(s.RowGap)
	for _, a := range []Align{s.AlignItems, s.AlignSelf, s.JustifyItems, s.JustifySelf} {
		t.i(int(a))
	}
	t.i(int(s.AlignContent))
	t.i(int(s.JustifyContent))
	t.i(int(s.Direction))
	t.i(int(s.FlexDirection))
	t.i(int(s.FlexWrap))
	t.f(s.FlexGrow)
	t.f(s.FlexShrink)
	t.length(s.FlexBasis)
	t.i(int(s.GridAutoFlow))
	t.line(s.GridColumn.Start)
	t.line(s.GridColumn.End)
	t.line(s.GridRow.Start)
	t.line(s.GridRow.End)
	return t
}

// defaults is the fixed fields of a node without attributes, which the D
// line holds.
var defaults = func() tokens { s := styleOf(&xNode{}); return fixed(&s) }()

// style writes a style as the fixed fields that differ from the D line's
// (a count, then index value pairs); then the template columns and rows (a
// count, then per track: count nsizes, then each size: min kind, min value,
// max kind, max value); the auto columns and rows (a count, then sizes);
// and the text's words (a count, then their lengths; 0: no text).
func (w writer) style(s *Style, text []int) {
	f := fixed(s)
	var diff []string
	for i := range f {
		if f[i] != defaults[i] {
			diff = append(diff, strconv.Itoa(i), f[i])
		}
	}
	w.i(len(diff) / 2)
	for _, d := range diff {
		w.WriteString(" " + d)
	}
	var t tokens
	for _, tpl := range [][]GridTrack{s.GridTemplateColumns, s.GridTemplateRows} {
		t.i(len(tpl))
		for _, tr := range tpl {
			t.i(tr.Count)
			t.i(len(tr.Sizes))
			for _, ts := range tr.Sizes {
				t.trackSize(ts)
			}
		}
	}
	for _, auto := range [][]TrackSize{s.GridAutoColumns, s.GridAutoRows} {
		t.i(len(auto))
		for _, ts := range auto {
			t.trackSize(ts)
		}
	}
	t.i(len(text))
	for _, n := range text {
		t.i(n)
	}
	w.WriteString(" " + strings.Join(t, " "))
}

func (w writer) input(x *xNode) {
	var text []int
	if len(x.Kids) == 0 {
		if t := strings.TrimSpace(x.Text); t != "" {
			text = words(t)
		}
	}
	s := styleOf(x)
	w.WriteString("N")
	w.i(len(x.Kids))
	w.style(&s, text)
	w.WriteString("\n")
	for i := range x.Kids {
		w.input(&x.Kids[i])
	}
}

func (w writer) expect(x *xNode) {
	f := func(k string) float32 {
		v, _ := x.attr(k)
		r, _ := strconv.ParseFloat(v, 32)
		return float32(r)
	}
	w.WriteString("E")
	w.i(len(x.Kids))
	w.f(f("x"))
	w.f(f("y"))
	w.f(f("width"))
	w.f(f("height"))
	w.WriteString("\n")
	for i := range x.Kids {
		w.expect(&x.Kids[i])
	}
}

func (w writer) got(n *Node) {
	w.WriteString("G")
	w.i(len(n.Children()))
	w.f(n.X)
	w.f(n.Y)
	w.f(n.W)
	w.f(n.H)
	w.WriteString("\n")
	for _, c := range n.Children() {
		w.got(c)
	}
}

func (w writer) test(name string, t *xTest) (pass bool) {
	if len(t.Input.Kids) != 1 || len(t.Expect.Kids) != 1 {
		fatal(fmt.Errorf("%s: want one root", name))
	}
	root := build(&t.Input.Kids[0])
	vw, vh := space(t.Viewport.Width), space(t.Viewport.Height)
	var p Pass
	root.Layout(Size{W: vw, H: vh}, &p)
	rounding := t.Rounding != "false"
	if rounding {
		Round(root, 1)
	}
	pass = matches(root, &t.Expect.Kids[0])
	w.WriteString("F " + name)
	w.i(map[bool]int{false: 0, true: 1}[rounding])
	w.f(vw)
	w.f(vh)
	w.i(map[bool]int{false: 0, true: 1}[pass])
	w.WriteString("\n")
	w.input(&t.Input.Kids[0])
	w.expect(&t.Expect.Kids[0])
	w.got(root)
	w.WriteString(".\n")
	return pass
}

func fatal(err error) {
	fmt.Fprintln(os.Stderr, "gen_c_fixtures:", err)
	os.Exit(1)
}

func main() {
	out, err := os.Create("core/test/testdata/layout_fixtures.txt")
	if err != nil {
		fatal(err)
	}
	w := writer{bufio.NewWriter(out)}
	w.WriteString("# Generated by go run layout/testdata/gen_c_fixtures.go. DO NOT EDIT.\n")
	w.WriteString("D " + strconv.Itoa(len(defaults)) + " " + strings.Join(defaults, " ") + "\n")
	files, _ := filepath.Glob("layout/testdata/css/*/*.xml")
	if len(files) == 0 {
		fatal(fmt.Errorf("no fixtures: run from the repository's root"))
	}
	pass, total := 0, 0
	for _, path := range files {
		data, err := os.ReadFile(path)
		if err != nil {
			fatal(err)
		}
		var t xTest
		if err := xml.Unmarshal(data, &t); err != nil {
			fatal(fmt.Errorf("%s: %v", path, err))
		}
		name := filepath.Base(filepath.Dir(path)) + "/" + strings.TrimSuffix(filepath.Base(path), ".xml")
		if w.test(name, &t) {
			pass++
		}
		total++
	}
	for _, file := range []string{"ltr_chrome", "rtl_chrome"} {
		data, err := os.ReadFile("layout/testdata/" + file + ".xml")
		if err != nil {
			fatal(err)
		}
		var f struct {
			Tests []xTest `xml:"test"`
		}
		if err := xml.Unmarshal(data, &f); err != nil {
			fatal(err)
		}
		for i := range f.Tests {
			if w.test(file+"/"+f.Tests[i].Name, &f.Tests[i]) {
				pass++
			}
			total++
		}
	}
	if err := w.Flush(); err != nil {
		fatal(err)
	}
	if err := out.Close(); err != nil {
		fatal(err)
	}
	fmt.Printf("%d fixtures, %d match Chrome in Go\n", total, pass)
}
