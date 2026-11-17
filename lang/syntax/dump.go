package syntax

import (
	"fmt"
	"io"
	"reflect"
	"strconv"
	"strings"
)

// Fdump writes n's tree to w, one node per line, indented by depth: the
// node's kind, its span as line:col-line:col, then its fields. It's for
// tests (the golden files of testdata) and for debugging; a tree made
// in code (file nil) prints offsets instead of lines.
//
// Fields that are empty (nil, false, NoPos, "") are left out, and so are
// the positions of brackets and operators; Pub, Native, Static, Async, Throws
// and Await print as flags.
func Fdump(w io.Writer, file *File, n Node) error {
	d := dumper{w: w, file: file}
	d.node(n, 0, "")
	return d.err
}

type dumper struct {
	w    io.Writer
	file *File
	err  error
}

func (d *dumper) printf(depth int, format string, args ...any) {
	if d.err != nil {
		return
	}
	_, d.err = fmt.Fprintf(d.w, "%s"+format+"\n", append([]any{strings.Repeat("  ", depth)}, args...)...)
}

func (d *dumper) pos(p Pos) string {
	if !p.IsValid() {
		return "-"
	}
	if d.file == nil {
		return strconv.Itoa(int(p))
	}
	ps := d.file.Position(p)
	return strconv.Itoa(ps.Line) + ":" + strconv.Itoa(ps.Column)
}

func (d *dumper) span(s Span) string { return d.pos(s.Start) + "-" + d.pos(s.End) }

var (
	posType      = reflect.TypeFor[Pos]()
	spanType     = reflect.TypeFor[Span]()
	triviaType   = reflect.TypeFor[Trivia]()
	tokenType    = reflect.TypeFor[Token]()
	nodeType     = reflect.TypeFor[Node]()
	commentType  = reflect.TypeFor[*Comment]()
	groupsType   = reflect.TypeFor[[]*CommentGroup]()
	fileType     = reflect.TypeFor[*File]()
	commentsType = reflect.TypeFor[[]*Comment]()
)

// flagPos are the positions printed, as flags, when they're valid.
var flagPos = map[string]bool{"Pub": true, "Native": true, "Extern": true, "Static": true, "Async": true, "Throws": true, "Await": true}

func (d *dumper) node(n Node, depth int, label string) {
	if label != "" {
		label += ": "
	}
	v := reflect.ValueOf(n)
	if n == nil || v.Kind() == reflect.Pointer && v.IsNil() {
		d.printf(depth, "%snil", label)
		return
	}
	if id, ok := n.(*Ident); ok {
		d.printf(depth, "%sIdent %q %s", label, id.Name, d.span(id.Span))
		return
	}
	e := v.Elem()
	t := e.Type()
	var head []string
	type child struct {
		name string
		v    reflect.Value
	}
	var children []child
	for i := range t.NumField() {
		f, fv := t.Field(i), e.Field(i)
		switch {
		case f.Type == spanType:
		case f.Type == fileType, f.Type == commentsType:
			// The file, and every comment (the nodes' Trivia show them).
		case f.Type == triviaType:
			tr := fv.Addr().Interface().(*Trivia)
			if tr.BlankBefore {
				head = append(head, "blank")
			}
			if s := groupsText(tr.Leading); s != "" {
				head = append(head, "comments="+strconv.Quote(s))
			}
			if tr.Trailing != nil {
				head = append(head, "trailing="+strconv.Quote(tr.Trailing.Text))
			}
		case f.Type == posType:
			if flagPos[f.Name] && fv.Interface().(Pos).IsValid() {
				head = append(head, strings.ToLower(f.Name))
			}
		case f.Type == tokenType:
			head = append(head, f.Name+"="+fv.Interface().(Token).String())
		case f.Type == commentType:
			if !fv.IsNil() {
				head = append(head, f.Name+"="+strconv.Quote(fv.Interface().(*Comment).Text))
			}
		case f.Type == groupsType:
			if s := groupsText(fv.Interface().([]*CommentGroup)); s != "" {
				head = append(head, f.Name+"="+strconv.Quote(s))
			}
		case f.Type.Kind() == reflect.Bool && f.Name == "Value":
			head = append(head, f.Name+"="+strconv.FormatBool(fv.Bool()))
		case f.Type.Kind() == reflect.Bool:
			if fv.Bool() {
				head = append(head, strings.ToLower(f.Name))
			}
		case f.Type.Kind() == reflect.String:
			head = append(head, f.Name+"="+strconv.Quote(fv.String()))
		case f.Type.Kind() == reflect.Slice && f.Type.Elem().Kind() == reflect.String:
			var q []string
			for j := range fv.Len() {
				q = append(q, strconv.Quote(fv.Index(j).String()))
			}
			head = append(head, f.Name+"=["+strings.Join(q, " ")+"]")
		default:
			if !fv.IsZero() {
				children = append(children, child{f.Name, fv})
			}
		}
	}
	d.printf(depth, "%s%s %s%s", label, t.Name(), d.span(n.Range()), prefixed(head))
	for _, c := range children {
		switch c.v.Kind() {
		case reflect.Slice:
			d.printf(depth+1, "%s: [%d]", c.name, c.v.Len())
			for j := range c.v.Len() {
				d.value(c.v.Index(j), depth+2, "")
			}
		default:
			d.value(c.v, depth+1, c.name)
		}
	}
}

func (d *dumper) value(v reflect.Value, depth int, label string) {
	if v.Kind() == reflect.Interface && v.IsNil() {
		d.node(nil, depth, label)
		return
	}
	if v.Type().Implements(nodeType) || v.Kind() == reflect.Interface {
		if n, ok := v.Interface().(Node); ok {
			d.node(n, depth, label)
			return
		}
	}
	d.printf(depth, "%s: %v", label, v.Interface())
}

func prefixed(s []string) string {
	if len(s) == 0 {
		return ""
	}
	return " " + strings.Join(s, " ")
}

func groupsText(gs []*CommentGroup) string {
	var b strings.Builder
	for i, g := range gs {
		if i > 0 {
			b.WriteString(" | ")
		}
		for j, c := range g.List {
			if j > 0 {
				b.WriteString(" / ")
			}
			b.WriteString(c.Text)
		}
	}
	return b.String()
}
