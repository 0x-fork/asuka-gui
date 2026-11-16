package nativegen

import (
	"bytes"
	"fmt"
	"go/format"
	"strings"
)

// DesktopDir is the folder of an Asuka native package's Go, for the
// desktop hosts (Windows, Linux): a Go package the CLI builds into the
// app's host.
const DesktopDir = "desktop"

// GoServerFile is the generated Go file of an Asuka package's desktop
// implementation.
func GoServerFile(p *Package) string { return p.Name + "_native.go" }

// GenerateGoServer writes the Go a package's desktop implementation is
// compiled with: its types and interfaces in Go, and the code that
// answers the core's calls (native.RegisterServer). The implementation
// writes new<Interface>() (<Interface>, error) for each interface, and
// for each native component a func(<View>Events) <View>Native it passes
// to implement<View> from an init function of a file built for the
// platform (the views: native.RegisterView).
func GenerateGoServer(p *Package) ([]byte, error) {
	g := &gsGen{p: p}
	g.printf("%s\n", header("//", p.Hash))
	g.printf("// What the package declares (%s), as its Go implements it for the\n// desktop hosts. The Go writes the functions that make each:\n//\n", p.Path)
	for _, it := range p.Interfaces {
		g.printf("//\tfunc new%s() (%s, error)\n", it.Name, it.Name)
	}
	for _, v := range p.Views {
		g.printf("//\tfunc init() { implement%s(func(events %sEvents) %sNative { … }) }\n", v.Name, v.Name, v.Name)
	}
	g.printf("\npackage %s\n\nimport (\n\t\"context\"\n\t\"time\"\n\n\t\"github.com/egoist/asuka/native\"\n)\n\n", p.Name)
	g.printf("var (\n\t_ = context.Background\n\t_ time.Duration\n)\n\n")
	for _, e := range p.Enums {
		g.doc(e.Doc, "")
		g.printf("type %s int\n\nconst (\n", e.Name)
		for _, v := range e.Values {
			g.printf("\t%s %s = %s\n", v.Name, e.Name, v.Value)
		}
		g.printf(")\n\n")
	}
	for _, s := range p.Structs {
		g.doc(s.Doc, "")
		g.printf("type %s struct {\n", s.Name)
		for _, f := range s.Fields {
			g.printf("\t%s %s\n", f.Name, g.goType(f.Type))
		}
		g.printf("}\n\n")
	}
	if len(p.Errors) > 0 {
		g.printf("// The package's declared errors.\nvar (\n")
		for _, e := range p.Errors {
			g.printf("\t%s = native.NewError(%q)\n", e.Name, e.Message)
		}
		g.printf(")\n\n")
	}
	for _, it := range p.Interfaces {
		g.doc(it.Doc, "")
		g.printf("type %s interface {\n", it.Name)
		for _, m := range it.Methods {
			g.doc(m.Doc, "\t")
			g.printf("\t%s\n", g.sig(m))
		}
		g.printf("}\n\n")
	}
	// Registration.
	g.printf("var server *native.Server\n\nfunc init() {\n\tserver = &native.Server{\n\tClass: %q,\n", p.Path)
	if len(p.Errors) > 0 {
		var names []string
		for _, e := range p.Errors {
			names = append(names, e.Name)
		}
		g.printf("\tErrors: []error{%s},\n", strings.Join(names, ", "))
	}
	g.printf("\tNew: func(iface uint32) (any, error) {\n\t\tswitch iface {\n")
	for _, it := range p.Interfaces {
		g.printf("\t\tcase %d:\n\t\t\treturn new%s()\n", it.Index, it.Name)
	}
	g.printf("\t\t}\n\t\treturn nil, native.ErrClosed\n\t},\n\tCall: serve,\n}\n\tnative.RegisterServer(server)\n}\n\n")
	g.printf("func serve(ctx context.Context, s *native.Session, obj any, method uint32, r *native.Reader, w *native.Writer) error {\n")
	if len(p.Interfaces) > 0 {
		g.printf("\tswitch o := obj.(type) {\n")
	} else {
		g.printf("\tswitch obj.(type) {\n")
	}
	for _, it := range p.Interfaces {
		g.printf("\tcase %s:\n\t\tswitch method {\n", it.Name)
		for _, m := range it.Methods {
			g.printf("\t\tcase %d:\n", m.Index)
			var args []string
			if m.Context {
				args = append(args, "ctx")
			}
			for i, prm := range m.Params {
				v := fmt.Sprintf("a%d", i)
				g.printf("\t\t\t%s := %s\n", v, g.dec(prm.Type, "r"))
				args = append(args, v)
			}
			g.printf("\t\t\tif err := r.Err(); err != nil {\n\t\t\t\treturn err\n\t\t\t}\n")
			if m.Result != nil {
				g.printf("\t\t\tv, err := o.%s(%s)\n\t\t\tif err != nil {\n\t\t\t\treturn err\n\t\t\t}\n", m.Name, strings.Join(args, ", "))
				g.printf("\t\t\t%s\n", g.enc(m.Result, "w", "v"))
				g.printf("\t\t\treturn nil\n")
			} else {
				g.printf("\t\t\treturn o.%s(%s)\n", m.Name, strings.Join(args, ", "))
			}
		}
		g.printf("\t\t}\n")
	}
	g.printf("\t}\n\treturn native.ErrClosed\n}\n\n")
	for _, v := range p.Views {
		g.view(v)
	}
	g.helpers()
	src, err := format.Source(g.b.Bytes())
	if err != nil {
		return nil, fmt.Errorf("nativegen: the desktop Go doesn't format: %v\n%s", err, g.b.Bytes())
	}
	return src, nil
}

// view writes a native component's Go side: <View>Native, the
// platform's view and a setter per prop; <View>Events, which sends its
// events; and implement<View>, which registers the view (native.
// RegisterView) under the kind the C core names it by on every platform,
// the package's path # the view's name: what the desktop hosts look up
// (native.ViewFactory). Props and events cross by their exported names
// (Selected, Changed), each value encoded in the native codec, as
// core/src/ui/media.c sends and reads them.
func (g *gsGen) view(v *View) {
	n := v.Name
	g.printf("// %sNative is a %s implemented in Go (Windows, Linux): the platform's\n// view (native.View's Handle) and a setter per prop. It may also be a\n// native.Measurer, and have Destroy.\n", n, n)
	g.printf("type %sNative interface {\n\tHandle() uintptr\n", n)
	for _, f := range v.Props {
		g.doc(f.Doc, "\t")
		g.printf("\tSet%s(%s %s)\n", f.Name, goParam(lowerCamel(f.Name)), g.goType(f.Type))
	}
	g.printf("}\n\n")
	g.printf("// %sEvents sends a %s's events, and has its context: the host's\n// view it goes in.\n", n, n)
	g.printf("type %sEvents struct{ Context native.Context }\n\n", n)
	for _, e := range v.Events {
		var params []string
		for _, a := range e.Type.Func.Params {
			params = append(params, goParam(a.Name)+" "+g.goType(a.Type))
		}
		if e.Doc != "" {
			g.doc(e.Doc, "")
		} else {
			g.printf("// %s sends the %s event.\n", e.Name, e.Name)
		}
		g.printf("func (e %sEvents) %s(%s) {\n\tvar s *native.Session // a view's values hold no objects\n\t_ = s\n\tw := native.NewWriter()\n", n, e.Name, strings.Join(params, ", "))
		for _, a := range e.Type.Func.Params {
			g.printf("\t%s\n", g.enc(a.Type, "w", goParam(a.Name)))
		}
		g.printf("\te.Context.Emit(%q, map[string]any{\"v\": w.Take()})\n}\n\n", e.Name)
	}
	g.printf("// implement%s makes Go's implementation the %s on this platform: call\n// it from an init function of a file built for it.\n", n, n)
	g.printf("func implement%s(make func(events %sEvents) %sNative) {\n", n, n, n)
	g.printf("\tnative.RegisterView(%q, func(ctx native.Context) native.View {\n", g.p.Path+"#"+n)
	g.printf("\t\tv := make(%sEvents{ctx})\n\t\treturn native.GoView(v, func(name string, value []byte) {\n\t\t\tvar s *native.Session // a view's values hold no objects\n\t\t\t_ = s\n\t\t\tr := native.NewReader(value)\n\t\t\tswitch name {\n", n)
	for _, f := range v.Props {
		g.printf("\t\t\tcase %q:\n\t\t\t\tif a := %s; r.Err() == nil {\n\t\t\t\t\tv.Set%s(a)\n\t\t\t\t}\n", f.Name, g.dec(f.Type, "r"), f.Name)
	}
	g.printf("\t\t\t}\n\t\t})\n\t})\n}\n\n")
}

// goParam is a name as a Go parameter: one that's a Go keyword gets a _.
func goParam(n string) string {
	switch n {
	case "break", "case", "chan", "const", "continue", "default", "defer", "else", "fallthrough", "for", "func", "go", "goto", "if",
		"import", "interface", "map", "package", "range", "return", "select", "struct", "switch", "type", "var", "make", "s", "w", "e", "v", "r":
		return n + "_"
	}
	return n
}

type gsGen struct {
	p    *Package
	b    bytes.Buffer
	need map[string]*Type
	keys []string
}

func (g *gsGen) printf(format string, args ...any) { fmt.Fprintf(&g.b, format, args...) }

func (g *gsGen) doc(d, indent string) {
	if d == "" {
		return
	}
	for _, l := range strings.Split(d, "\n") {
		g.printf("%s// %s\n", indent, l)
	}
}

func (g *gsGen) goType(t *Type) string {
	switch t.Kind {
	case Int:
		return "int"
	case Float64:
		return "float64"
	case Bool:
		return "bool"
	case String:
		return "string"
	case Bytes:
		return "[]byte"
	case Duration:
		return "time.Duration"
	case Slice:
		return "[]" + g.goType(t.Elem)
	case Map:
		return "map[string]" + g.goType(t.Elem)
	case Optional:
		return "*" + g.goType(t.Elem)
	case Struct, Enum, Object:
		return t.Name
	case Func:
		var ps []string
		for _, p := range t.Func.Params {
			ps = append(ps, g.goType(p.Type))
		}
		r := ""
		if t.Func.Result != nil {
			r = " " + g.goType(t.Func.Result)
		}
		return "func(" + strings.Join(ps, ", ") + ")" + r
	}
	return "any"
}

func (g *gsGen) sig(m *Method) string {
	var ps []string
	if m.Context {
		ps = append(ps, "ctx context.Context")
	}
	for _, p := range m.Params {
		ps = append(ps, p.Name+" "+g.goType(p.Type))
	}
	res := "error"
	if m.Result != nil {
		res = "(" + g.goType(m.Result) + ", error)"
	}
	return m.Name + "(" + strings.Join(ps, ", ") + ") " + res
}

// mangled names a helper's type.
func (g *gsGen) mangled(t *Type) string {
	switch t.Kind {
	case Slice:
		return "Slice" + g.mangled(t.Elem)
	case Map:
		return "Map" + g.mangled(t.Elem)
	case Optional:
		return "Opt" + g.mangled(t.Elem)
	case Struct, Enum, Object:
		return t.Name
	case Func:
		return fmt.Sprintf("Func%x", len(g.goType(t))+strings.Count(g.goType(t), "string")*7)
	}
	return strings.Title(g.goType(t))
}

func (g *gsGen) want(t *Type) string {
	k := g.mangled(t)
	if g.need == nil {
		g.need = map[string]*Type{}
	}
	if _, ok := g.need[k]; !ok {
		g.need[k] = t
		g.keys = append(g.keys, k)
	}
	return k
}

func (g *gsGen) dec(t *Type, r string) string {
	switch t.Kind {
	case Int:
		return r + ".Int()"
	case Float64:
		return r + ".Float64()"
	case Bool:
		return r + ".Bool()"
	case String:
		return r + ".String()"
	case Bytes:
		return r + ".Data()"
	case Duration:
		return r + ".Duration()"
	case Enum:
		return t.Name + "(" + r + ".Int())"
	case Object:
		return fmt.Sprintf("objectOf%s(s, %s.Handle())", g.want(t), r)
	case Func:
		return fmt.Sprintf("callback%s(s, %s.Uint64())", g.want(t), r)
	}
	return fmt.Sprintf("read%s(s, %s)", g.want(t), r)
}

func (g *gsGen) enc(t *Type, w, v string) string {
	switch t.Kind {
	case Int:
		return w + ".Int(" + v + ")"
	case Float64:
		return w + ".Float64(" + v + ")"
	case Bool:
		return w + ".Bool(" + v + ")"
	case String:
		return w + ".String(" + v + ")"
	case Bytes:
		return w + ".Data(" + v + ")"
	case Duration:
		return w + ".Duration(" + v + ")"
	case Enum:
		return w + ".Int(int(" + v + "))"
	case Object:
		return fmt.Sprintf("%s.Uint64(s.Handle(server, %s))", w, v)
	}
	return fmt.Sprintf("write%s(s, %s, %s)", g.want(t), w, v)
}

func (g *gsGen) helpers() {
	for i := 0; i < len(g.keys); i++ {
		k := g.keys[i]
		t := g.need[k]
		gt := g.goType(t)
		switch t.Kind {
		case Slice:
			g.printf("func read%s(s *native.Session, r *native.Reader) %s {\n\tn := r.Len()\n\tv := make(%s, 0, n)\n\tfor i := 0; i < n && r.Err() == nil; i++ {\n\t\tv = append(v, %s)\n\t}\n\treturn v\n}\n\n", k, gt, gt, g.dec(t.Elem, "r"))
			g.printf("func write%s(s *native.Session, w *native.Writer, v %s) {\n\tw.Len(len(v))\n\tfor _, e := range v {\n\t\t%s\n\t}\n}\n\n", k, gt, g.enc(t.Elem, "w", "e"))
		case Map:
			g.printf("func read%s(s *native.Session, r *native.Reader) %s {\n\tn := r.Len()\n\tv := make(%s, n)\n\tfor i := 0; i < n && r.Err() == nil; i++ {\n\t\tk := r.String()\n\t\tv[k] = %s\n\t}\n\treturn v\n}\n\n", k, gt, gt, g.dec(t.Elem, "r"))
			g.printf("func write%s(s *native.Session, w *native.Writer, v %s) {\n\tw.Len(len(v))\n\tfor k, e := range v {\n\t\tw.String(k)\n\t\t%s\n\t}\n}\n\n", k, gt, g.enc(t.Elem, "w", "e"))
		case Optional:
			g.printf("func read%s(s *native.Session, r *native.Reader) %s {\n\tif !r.Present() {\n\t\treturn nil\n\t}\n\tv := %s\n\treturn &v\n}\n\n", k, gt, g.dec(t.Elem, "r"))
			g.printf("func write%s(s *native.Session, w *native.Writer, v %s) {\n\tw.Present(v != nil)\n\tif v != nil {\n\t\t%s\n\t}\n}\n\n", k, gt, g.enc(t.Elem, "w", "(*v)"))
		case Struct:
			var sd *StructDecl
			for _, s := range g.p.Structs {
				if s.Name == t.Name {
					sd = s
				}
			}
			g.printf("func read%s(s *native.Session, r *native.Reader) %s {\n\tvar v %s\n", k, gt, gt)
			for _, f := range sd.Fields {
				g.printf("\tv.%s = %s\n", f.Name, g.dec(f.Type, "r"))
			}
			g.printf("\treturn v\n}\n\n")
			g.printf("func write%s(s *native.Session, w *native.Writer, v %s) {\n", k, gt)
			for _, f := range sd.Fields {
				g.printf("\t%s\n", g.enc(f.Type, "w", "v."+f.Name))
			}
			g.printf("}\n\n")
		case Object:
			g.printf("func objectOf%s(s *native.Session, h uint64) %s {\n\tv, _ := s.Object(h).(%s)\n\treturn v\n}\n\n", k, gt, gt)
		case Func:
			// A callback into the core: its arguments go, its result comes back.
			var ps, enc []string
			for i, p := range t.Func.Params {
				ps = append(ps, fmt.Sprintf("p%d %s", i, g.goType(p.Type)))
				enc = append(enc, "\t\t"+g.enc(p.Type, "w", fmt.Sprintf("p%d", i)))
			}
			g.printf("func callback%s(s *native.Session, id uint64) %s {\n\tif id == 0 {\n\t\treturn nil\n\t}\n", k, gt)
			if t.Func.Result == nil {
				g.printf("\treturn func(%s) {\n\t\tw := native.NewWriter()\n%s\n\t\ts.Invoke(id, w.Take())\n\t}\n}\n\n", strings.Join(ps, ", "), strings.Join(enc, "\n"))
			} else {
				g.printf("\treturn func(%s) %s {\n\t\tw := native.NewWriter()\n%s\n\t\tr := native.NewReader(s.Invoke(id, w.Take()))\n\t\treturn %s\n\t}\n}\n\n", strings.Join(ps, ", "), g.goType(t.Func.Result), strings.Join(enc, "\n"), g.dec(t.Func.Result, "r"))
			}
		}
	}
}
