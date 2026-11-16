package nativegen

import (
	"bytes"
	"fmt"
	"os"
	"sort"
	"strconv"
	"strings"
)

// DesktopDir is the folder of an Asuka native package's C, for the
// desktop hosts (Windows, Linux), with the generated <pkg>.asuka.h and
// <pkg>.asuka.c beside the package's own files:
//
//	desktop/x.c          built for Windows and Linux (#ifdef _WIN32 where they differ)
//	desktop/x_windows.c  built for Windows only
//	desktop/x_linux.c    built for Linux only
//
// A platform the package has no C of its own for (no x.c, no x_<os>.c)
// gets none of it, the generated code included: making its objects
// throws unsupported there, as on a platform with no project.
const DesktopDir = "desktop"

// CHeader and CSource are the generated C files' names, in desktop/.
func CHeader(p *Package) string { return p.Name + ".asuka.h" }
func CSource(p *Package) string { return p.Name + ".asuka.c" }

// CSources are the C files of the package in dir to build for goos
// (windows, linux): its own for the platform and the generated one, or
// none when it has none of its own for it.
func CSources(dir, goos string) []string {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return nil
	}
	var own []string
	var gen string
	for _, e := range entries {
		n := e.Name()
		if !strings.HasSuffix(n, ".c") {
			continue
		}
		if strings.HasSuffix(n, ".asuka.c") {
			gen = n
			continue
		}
		base := strings.TrimSuffix(n, ".c")
		switch {
		case strings.HasSuffix(base, "_windows"):
			if goos != "windows" {
				continue
			}
		case strings.HasSuffix(base, "_linux"):
			if goos != "linux" {
				continue
			}
		case strings.HasSuffix(base, "_test"):
			continue
		}
		own = append(own, n)
	}
	if len(own) == 0 {
		return nil
	}
	if gen != "" {
		own = append(own, gen)
	}
	sort.Strings(own)
	return own
}

var cKeywords = set("auto break case char const continue default do double else enum extern float for goto if inline int long register restrict return short signed sizeof static struct switch typedef union unsigned void volatile while bool true false NULL self err call events errno main free malloc")

// cName is a name as a C identifier: a keyword gets a _.
func cName(n string) string {
	if cKeywords[n] {
		return n + "_"
	}
	return n
}

type cGen struct {
	p       *Package
	h, c    bytes.Buffer
	pre     string // the package's prefix: pantry_
	structs map[string]*StructDecl
	ifaces  map[string]*Interface
	need    map[string]*Type // composite types (lists, maps, optionals, structs), by mangled name
	order   []string
	fns     map[string]*FuncType
	fnOrder []string
}

func (g *cGen) hf(format string, args ...any) { fmt.Fprintf(&g.h, format, args...) }
func (g *cGen) cf(format string, args ...any) { fmt.Fprintf(&g.c, format, args...) }

// mangle names a type in identifiers: list_Item, map_string, opt_int,
// fn_string__bool.
func (g *cGen) mangle(t *Type) string {
	switch t.Kind {
	case Bool:
		return "bool"
	case Int:
		return "int"
	case Float64:
		return "float"
	case String:
		return "string"
	case Bytes:
		return "bytes"
	case Duration:
		return "Duration"
	case ViewRef:
		return "view"
	case Struct, Enum, Object:
		return t.Name
	case Slice:
		return "list_" + g.mangle(t.Elem)
	case Map:
		return "map_" + g.mangle(t.Elem)
	case Optional:
		return "opt_" + g.mangle(t.Elem)
	case Func:
		var ps []string
		for _, p := range t.Func.Params {
			ps = append(ps, g.mangle(p.Type))
		}
		s := "fn"
		if len(ps) > 0 {
			s += "_" + strings.Join(ps, "_")
		}
		if t.Func.Result != nil {
			s += "__" + g.mangle(t.Func.Result)
		}
		return s
	}
	return "x"
}

// ctype is t as a value of C.
func (g *cGen) ctype(t *Type) string {
	switch t.Kind {
	case Bool:
		return "bool"
	case Int, Duration:
		return "int64_t"
	case ViewRef:
		return "uint32_t"
	case Float64:
		return "double"
	case String:
		return "char *"
	case Bytes:
		return "asuka_bytes"
	case Struct, Enum:
		return g.pre + t.Name
	case Object:
		return g.pre + t.Name + " *"
	case Slice, Map, Func:
		return g.pre + g.mangle(t)
	case Optional:
		switch t.Elem.Kind {
		case String, Object:
			return g.ctype(t.Elem)
		}
		return star(g.ctype(t.Elem))
	}
	return "void *"
}

// ptype is t as a parameter: a struct by a const pointer, a string as a
// const char *.
func (g *cGen) ptype(t *Type) string {
	switch t.Kind {
	case String:
		return "const char *"
	case Struct:
		return "const " + g.pre + t.Name + " *"
	case Optional:
		switch t.Elem.Kind {
		case String:
			return "const char *"
		case Object:
			return g.ctype(t.Elem)
		}
		return "const " + star(g.ctype(t.Elem))
	}
	return g.ctype(t)
}

// star appends a pointer to a C type.
func star(t string) string {
	if strings.HasSuffix(t, "*") {
		return t + "*"
	}
	return t + " *"
}

// cdecl is a declaration of name of C type ct.
func cdecl(ct, name string) string {
	if strings.HasSuffix(ct, "*") {
		return ct + name
	}
	return ct + " " + name
}

// paramVal is a parameter's value as ctype has it (a struct's pointer
// dereferenced).
func paramVal(t *Type, name string) string {
	if t.Kind == Struct {
		return "(*" + name + ")"
	}
	return name
}

// argOf is a value of ctype passed as ptype.
func argOf(t *Type, v string) string {
	if t.Kind == Struct {
		return "&" + v
	}
	return v
}

func (g *cGen) want(t *Type) {
	switch t.Kind {
	case Optional:
		if t.Elem.Kind == Object {
			return // a handle, read and written in place
		}
	case Slice, Map, Struct:
	case Func:
		k := g.mangle(t)
		if _, ok := g.fns[k]; !ok {
			g.fns[k] = t.Func
			g.fnOrder = append(g.fnOrder, k)
			for _, p := range t.Func.Params {
				g.want(p.Type)
			}
			if t.Func.Result != nil {
				g.want(t.Func.Result)
			}
		}
		return
	default:
		return
	}
	k := g.mangle(t)
	if _, ok := g.need[k]; ok {
		return
	}
	g.need[k] = t
	if t.Kind == Struct {
		for _, f := range g.structs[t.Name].Fields {
			g.want(f.Type)
		}
	} else {
		g.want(t.Elem)
	}
	g.order = append(g.order, k)
}

// enc is the code that writes v (of ctype t) to the writer w (a pointer).
func (g *cGen) enc(t *Type, w, v string) string {
	switch t.Kind {
	case Bool:
		return fmt.Sprintf("asuka_writer_bool(%s, %s);", w, v)
	case Int, Duration, Enum:
		return fmt.Sprintf("asuka_writer_i64(%s, %s);", w, v)
	case Float64:
		return fmt.Sprintf("asuka_writer_f64(%s, %s);", w, v)
	case String:
		return fmt.Sprintf("asuka_writer_string(%s, %s);", w, v)
	case Bytes:
		return fmt.Sprintf("asuka_writer_bytes(%s, (%s).data, (%s).len);", w, v, v)
	case ViewRef:
		return fmt.Sprintf("asuka_writer_u64(%s, %s);", w, v)
	case Object:
		return fmt.Sprintf("asuka_writer_u64(%s, asuka_native_handle(&provider, %d, %s));", w, g.ifaces[t.Name].Index, v)
	case Func:
		return fmt.Sprintf("asuka_writer_u64(%s, (%s).id);", w, v)
	case Optional:
		if t.Elem.Kind == Object {
			return g.enc(t.Elem, w, v)
		}
		return fmt.Sprintf("w_%s(%s, %s);", g.mangle(t), w, v)
	}
	return fmt.Sprintf("w_%s(%s, &%s);", g.mangle(t), w, v)
}

// dec is the expression that reads a value of t from the reader r (a
// pointer).
func (g *cGen) dec(t *Type, r string) string {
	switch t.Kind {
	case Bool:
		return fmt.Sprintf("asuka_reader_bool(%s)", r)
	case Int, Duration, Enum:
		return fmt.Sprintf("asuka_reader_i64(%s)", r)
	case Float64:
		return fmt.Sprintf("asuka_reader_f64(%s)", r)
	case String:
		return fmt.Sprintf("asuka_reader_string(%s)", r)
	case Bytes:
		return fmt.Sprintf("asuka_reader_bytes(%s)", r)
	case ViewRef:
		return fmt.Sprintf("(uint32_t)asuka_reader_u64(%s)", r)
	case Object:
		return fmt.Sprintf("(%s)asuka_native_object(&provider, %d, asuka_reader_u64(%s))", g.ctype(t), g.ifaces[t.Name].Index, r)
	case Func:
		return fmt.Sprintf("(%s){asuka_reader_u64(%s)}", g.ctype(t), r)
	case Optional:
		if t.Elem.Kind == Object {
			return fmt.Sprintf("(asuka_reader_u8(%s) ? %s : NULL)", r, g.dec(t.Elem, r))
		}
	}
	return fmt.Sprintf("r_%s(%s)", g.mangle(t), r)
}

// free is the code that frees what v (of ctype t) holds; "" for nothing.
func (g *cGen) free(t *Type, v string) string {
	switch t.Kind {
	case String:
		return "free(" + v + ");"
	case Bytes:
		return "free((" + v + ").data);"
	case Struct, Slice, Map:
		return g.pre + g.mangle(t) + "_free(&" + v + ");"
	case Optional:
		switch t.Elem.Kind {
		case String:
			return "free(" + v + ");"
		case Object:
			return ""
		}
		inner := g.free(t.Elem, "(*"+v+")")
		if inner == "" {
			return "free(" + v + ");"
		}
		return "if (" + v + ") { " + inner + " free(" + v + "); }"
	}
	return ""
}

func cDoc(doc, indent string) string {
	if doc == "" {
		return ""
	}
	var b strings.Builder
	for _, l := range strings.Split(doc, "\n") {
		b.WriteString(strings.TrimRight(indent+"// "+l, " ") + "\n")
	}
	return b.String()
}

func cString(s string) string {
	q := strconv.Quote(s)
	return q
}

// GenerateC writes the package's C side, for its desktop/ (Windows and
// Linux): the header its C includes, and the source that registers its
// provider.
func GenerateC(p *Package) (header, source []byte) {
	g := &cGen{p: p, pre: p.Name + "_", structs: map[string]*StructDecl{}, ifaces: map[string]*Interface{},
		need: map[string]*Type{}, fns: map[string]*FuncType{}}
	for _, s := range p.Structs {
		g.structs[s.Name] = s
	}
	for _, it := range p.Interfaces {
		g.ifaces[it.Name] = it
	}
	for _, s := range p.Structs {
		g.want(&Type{Kind: Struct, Name: s.Name})
	}
	for _, it := range p.Interfaces {
		for _, m := range it.Methods {
			for _, a := range m.Params {
				g.want(a.Type)
			}
			if m.Result != nil {
				g.want(m.Result)
			}
		}
	}
	for _, v := range p.Views {
		for _, f := range v.Props {
			g.want(f.Type)
		}
		for _, e := range v.Events {
			for _, a := range e.Type.Func.Params {
				g.want(a.Type)
			}
		}
	}
	g.header()
	g.source()
	return g.h.Bytes(), g.c.Bytes()
}

func (g *cGen) header() {
	p := g.p
	guard := strings.ToUpper(p.Name) + "_ASUKA_H"
	g.hf("%s\n", header("//", p.Hash))
	g.hf("// What the package declares (%s), as its C implements it on\n// Windows and Linux (asuka_desktop.h). The C writes the functions\n// that make each:\n//\n", p.Path)
	for _, it := range p.Interfaces {
		g.hf("//\t%s%s *%smake_%s(asuka_error *err);\n", g.pre, it.Name, g.pre, it.Name)
	}
	for _, v := range p.Views {
		g.hf("//\t%s%s *%smake_%s(%s%s_events events);\n", g.pre, v.Name, g.pre, v.Name, g.pre, v.Name)
	}
	g.hf("//\n// A method fails by setting err (asuka_error_declared with one of the\n")
	g.hf("// package's errors below, asuka_error_failed, asuka_error_unsupported)\n// and returning a zero value. An async method answers its call once,\n")
	g.hf("// from any thread, with its _done or asuka_call_fail. A NULL in a\n// vtable answers unsupported (a NULL release or destroy frees the object).\n\n")
	g.hf("#ifndef %s\n#define %s\n\n#include \"asuka_desktop.h\"\n\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n", guard, guard)
	if len(p.Errors) > 0 {
		en := p.ErrorName
		if en == "" {
			en = upperCamel(p.Name) + "Error"
		}
		g.hf("// The package's declared errors (asuka_error_declared).\nenum {\n")
		for _, e := range p.Errors {
			g.hf("\t%s%s_%s = %d, // %s\n", g.pre, en, lowerCamel(errorCase(e.Name)), e.Index, e.Message)
		}
		g.hf("};\n\n")
	}
	for _, e := range p.Enums {
		g.hf("%stypedef int64_t %s%s;\nenum {\n", cDoc(e.Doc, ""), g.pre, e.Name)
		for _, v := range e.Values {
			g.hf("\t%s%s_%s = %s,\n", g.pre, e.Name, lowerCamel(enumCase(e.Name, v.Name)), v.Value)
		}
		g.hf("};\n\n")
	}
	for _, s := range p.Structs {
		g.hf("typedef struct %s%s %s%s;\n", g.pre, s.Name, g.pre, s.Name)
	}
	for _, it := range p.Interfaces {
		g.hf("typedef struct %s%s %s%s;\n", g.pre, it.Name, g.pre, it.Name)
	}
	for _, v := range p.Views {
		g.hf("typedef struct %s%s %s%s;\n", g.pre, v.Name, g.pre, v.Name)
	}
	g.hf("\n")
	// Lists and maps, inner ones first (g.order is), then the structs in
	// the order their by-value fields need.
	for _, k := range g.order {
		t := g.need[k]
		switch t.Kind {
		case Slice:
			g.hf("typedef struct {\n\t%s;\n\tsize_t len;\n} %s%s;\n", cdecl(star(g.ctype(t.Elem)), "data"), g.pre, k)
			g.hf("void %s%s_free(%s%s *v);\n\n", g.pre, k, g.pre, k)
		case Map:
			g.hf("typedef struct {\n\tchar **keys;\n\t%s;\n\tsize_t len;\n} %s%s;\n", cdecl(star(g.ctype(t.Elem)), "values"), g.pre, k)
			g.hf("void %s%s_free(%s%s *v);\n\n", g.pre, k, g.pre, k)
		}
	}
	for _, s := range g.structOrder() {
		g.hf("%sstruct %s%s {\n", cDoc(s.Doc, ""), g.pre, s.Name)
		for _, f := range s.Fields {
			g.hf("%s\t%s;\n", cDoc(f.Doc, "\t"), cdecl(g.ctype(f.Type), cName(lowerCamel(f.Name))))
		}
		if len(s.Fields) == 0 {
			g.hf("\tchar unused_;\n")
		}
		g.hf("};\n\n// %s%s_free frees what v holds.\nvoid %s%s_free(%s%s *v);\n\n", g.pre, s.Name, g.pre, s.Name, g.pre, s.Name)
	}
	for _, k := range g.fnOrder {
		ft := g.fns[k]
		kind := "a notification: any thread, posted to the main thread"
		if ft.Result != nil {
			kind = "a question: its answer the caller's (main thread at once; another waits for it)"
		}
		g.hf("// %s%s is a callback, %s.\ntypedef struct {\n\tuint64_t id;\n} %s%s;\n", g.pre, k, kind, g.pre, k)
		res := "void"
		if ft.Result != nil {
			res = g.ctype(ft.Result)
		}
		var ps []string
		ps = append(ps, g.pre+k+" f")
		for i, a := range ft.Params {
			ps = append(ps, cdecl(g.ptype(a.Type), fmt.Sprintf("p%d", i)))
		}
		g.hf("%s;\n\n", cdecl(res, g.pre+k+"_call("+strings.Join(ps, ", ")+")"))
	}
	for _, it := range p.Interfaces {
		g.iface(it)
	}
	for _, v := range p.Views {
		g.viewDecl(v)
	}
	g.hf("#ifdef __cplusplus\n}\n#endif\n\n#endif\n")
}

// structOrder is the package's structs, each after those it holds by value.
func (g *cGen) structOrder() []*StructDecl {
	var out []*StructDecl
	done := map[string]bool{}
	var visit func(s *StructDecl)
	visit = func(s *StructDecl) {
		if done[s.Name] {
			return
		}
		done[s.Name] = true
		for _, f := range s.Fields {
			if f.Type.Kind == Struct {
				visit(g.structs[f.Type.Name])
			}
		}
		out = append(out, s)
	}
	for _, s := range g.p.Structs {
		visit(s)
	}
	return out
}

func (g *cGen) methodSig(it *Interface, m *Method, name string) string {
	ps := []string{g.pre + it.Name + " *self"}
	for _, a := range m.Params {
		ps = append(ps, cdecl(g.ptype(a.Type), cName(a.Name)))
	}
	if m.Context {
		ps = append(ps, "asuka_call *call")
		return "void (*" + name + ")(" + strings.Join(ps, ", ") + ")"
	}
	ps = append(ps, "asuka_error *err")
	res := "void"
	if m.Result != nil {
		res = g.ctype(m.Result)
	}
	return cdecl(res, "(*"+name+")("+strings.Join(ps, ", ")+")")
}

func (g *cGen) iface(it *Interface) {
	n := g.pre + it.Name
	g.hf("%stypedef struct {\n\t// release frees the object: the core let it go.\n\tvoid (*release)(%s *self);\n", cDoc(it.Doc, ""), n)
	for _, m := range it.Methods {
		doc := m.Doc
		if m.Context {
			if doc != "" {
				doc += "\n"
			}
			doc += fmt.Sprintf("Async: answer with %s_%s_done or asuka_call_fail.", n, lowerCamel(m.Name))
		}
		g.hf("%s\t%s;\n", cDoc(doc, "\t"), g.methodSig(it, m, cName(lowerCamel(m.Name))))
	}
	g.hf("} %s_vtable;\n\nstruct %s {\n\tconst %s_vtable *vt;\n};\n\n", n, n, n)
	for _, m := range it.Methods {
		if !m.Context {
			continue
		}
		g.hf("void %s;\n", g.doneSig(it, m))
	}
	g.hf("%s *%smake_%s(asuka_error *err);\n\n", n, g.pre, it.Name)
}

func (g *cGen) doneSig(it *Interface, m *Method) string {
	s := fmt.Sprintf("%s%s_%s_done(asuka_call *call", g.pre, it.Name, lowerCamel(m.Name))
	if m.Result != nil {
		s += ", " + cdecl(g.ctype(m.Result), "value")
	}
	return s + ")"
}

func (g *cGen) eventSig(v *View, e *Field) string {
	ps := []string{g.pre + v.Name + "_events events"}
	for _, a := range e.Type.Func.Params {
		ps = append(ps, cdecl(g.ptype(a.Type), cName(a.Name)))
	}
	return fmt.Sprintf("void %s%s_%s(%s)", g.pre, v.Name, lowerCamel(e.Name), strings.Join(ps, ", "))
}

func (g *cGen) viewDecl(v *View) {
	n := g.pre + v.Name
	g.hf("%s// The view's widget (an HWND, a GtkWidget *) is the host's once made: it\n// destroys it as the view goes, after destroy.\n", cDoc(v.Doc, ""))
	g.hf("typedef struct {\n\t// destroy frees the view's object, as the view goes.\n\tvoid (*destroy)(%s *self);\n", n)
	for _, f := range v.Props {
		g.hf("%s\tvoid (*set_%s)(%s *self, %s);\n", cDoc(f.Doc, "\t"), lowerCamel(f.Name), n, cdecl(g.ptype(f.Type), "value"))
	}
	g.hf("\t// measure sizes the view (width and height known sizes or NaN);\n\t// NULL or false: the host measures the widget as its own.\n")
	g.hf("\tbool (*measure)(%s *self, float width, float height, float *out_width, float *out_height);\n} %s_vtable;\n\n", n, n)
	g.hf("struct %s {\n\tconst %s_vtable *vt;\n\tvoid *widget;\n};\n\n", n, n)
	g.hf("// %s_events raises a %s's events (any thread): view is its node, parent\n// the host's widget it's made in.\ntypedef struct {\n\tuint32_t view;\n\tvoid *parent;\n} %s_events;\n\n", n, v.Name, n)
	for _, e := range v.Events {
		g.hf("%s%s;\n", cDoc(e.Doc, ""), g.eventSig(v, e))
	}
	g.hf("%s *%smake_%s(%s_events events);\n\n", n, g.pre, v.Name, n)
}

func (g *cGen) source() {
	p := g.p
	g.cf("%s\n", header("//", p.Hash))
	g.cf("// The provider of %s: what makes, calls and shows what the package\n// declares, over the wire (core/src/native/native.h).\n\n", p.Path)
	g.cf("#include \"%s\"\n\n#include <stdlib.h>\n#include <string.h>\n\n", CHeader(p))
	g.cf("// The codec's helpers: those the package's types need.\n#if defined(__GNUC__) || defined(__clang__)\n#define HELPER static __attribute__((unused))\n#else\n#define HELPER static\n#endif\n\n")
	g.cf("static const asuka_native_provider provider;\n\n")
	// Prototypes of the codec's helpers.
	for _, k := range g.order {
		t := g.need[k]
		ct := g.ctype(t)
		if t.Kind == Optional {
			g.cf("HELPER void w_%s(asuka_writer *w, %s);\n", k, cdecl(g.ptype(t), "v"))
		} else {
			g.cf("HELPER void w_%s(asuka_writer *w, const %s *v);\n", k, ct)
		}
		g.cf("HELPER %s;\n", cdecl(ct, "r_"+k+"(asuka_reader *r)"))
	}
	if len(g.order) > 0 {
		g.cf("\n")
	}
	for _, k := range g.order {
		g.helper(k, g.need[k])
	}
	for _, k := range g.fnOrder {
		g.callback(k, g.fns[k])
	}
	g.calls()
	for _, it := range p.Interfaces {
		for _, m := range it.Methods {
			if m.Context {
				g.done(it, m)
			}
		}
	}
	g.views()
	g.cf("static const asuka_native_provider provider = {\n\t.path = %s,\n\t.make = make,\n\t.call = call,\n\t.start = start,\n\t.release = release,\n", cString(p.Path))
	if len(p.Views) > 0 {
		g.cf("\t.views = &views,\n")
	}
	g.cf("};\n\nASUKA_NATIVE_CONSTRUCTOR(register_%s) { asuka_native_register(&provider); }\n", p.Name)
}

func (g *cGen) helper(k string, t *Type) {
	ct := g.ctype(t)
	pub := g.pre + k
	switch t.Kind {
	case Struct:
		s := g.structs[t.Name]
		g.cf("HELPER void w_%s(asuka_writer *w, const %s *v) {\n", k, ct)
		for _, f := range s.Fields {
			g.cf("\t%s\n", g.enc(f.Type, "w", "v->"+cName(lowerCamel(f.Name))))
		}
		if len(s.Fields) == 0 {
			g.cf("\t(void)w;\n\t(void)v;\n")
		}
		g.cf("}\n\nHELPER %s r_%s(asuka_reader *r) {\n\t%s v = {0};\n", ct, k, ct)
		for _, f := range s.Fields {
			g.cf("\tv.%s = %s;\n", cName(lowerCamel(f.Name)), g.dec(f.Type, "r"))
		}
		if len(s.Fields) == 0 {
			g.cf("\t(void)r;\n")
		}
		g.cf("\treturn v;\n}\n\nvoid %s_free(%s *v) {\n", pub, ct)
		any := false
		for _, f := range s.Fields {
			if fr := g.free(f.Type, "v->"+cName(lowerCamel(f.Name))); fr != "" {
				g.cf("\t%s\n", fr)
				any = true
			}
		}
		if !any {
			g.cf("\t(void)v;\n")
		}
		g.cf("}\n\n")
	case Slice:
		g.cf("HELPER void w_%s(asuka_writer *w, const %s *v) {\n\tasuka_writer_u32(w, (uint32_t)v->len);\n\tfor (size_t i = 0; i < v->len; i++) %s\n}\n\n", k, ct, g.enc(t.Elem, "w", "v->data[i]"))
		g.cf("HELPER %s r_%s(asuka_reader *r) {\n\t%s v = {0};\n\tuint32_t n = asuka_reader_count(r);\n\tv.data = calloc(n ? n : 1, sizeof *v.data);\n", ct, k, ct)
		g.cf("\tfor (uint32_t i = 0; i < n && !r->failed; i++) v.data[v.len++] = %s;\n\treturn v;\n}\n\n", g.dec(t.Elem, "r"))
		g.cf("void %s_free(%s *v) {\n", pub, ct)
		if fr := g.free(t.Elem, "v->data[i]"); fr != "" {
			g.cf("\tfor (size_t i = 0; i < v->len; i++) %s\n", fr)
		}
		g.cf("\tfree(v->data);\n\tv->data = NULL;\n\tv->len = 0;\n}\n\n")
	case Map:
		g.cf("HELPER void w_%s(asuka_writer *w, const %s *v) {\n\tasuka_writer_u32(w, (uint32_t)v->len);\n\tfor (size_t i = 0; i < v->len; i++) {\n\t\tasuka_writer_string(w, v->keys[i]);\n\t\t%s\n\t}\n}\n\n", k, ct, g.enc(t.Elem, "w", "v->values[i]"))
		g.cf("HELPER %s r_%s(asuka_reader *r) {\n\t%s v = {0};\n\tuint32_t n = asuka_reader_count(r);\n\tv.keys = calloc(n ? n : 1, sizeof *v.keys);\n\tv.values = calloc(n ? n : 1, sizeof *v.values);\n", ct, k, ct)
		g.cf("\tfor (uint32_t i = 0; i < n && !r->failed; i++) {\n\t\tv.keys[i] = asuka_reader_string(r);\n\t\tv.values[i] = %s;\n\t\tv.len++;\n\t}\n\treturn v;\n}\n\n", g.dec(t.Elem, "r"))
		g.cf("void %s_free(%s *v) {\n\tfor (size_t i = 0; i < v->len; i++) {\n\t\tfree(v->keys[i]);\n", pub, ct)
		if fr := g.free(t.Elem, "v->values[i]"); fr != "" {
			g.cf("\t\t%s\n", fr)
		}
		g.cf("\t}\n\tfree(v->keys);\n\tfree(v->values);\n\tv->keys = NULL;\n\tv->values = NULL;\n\tv->len = 0;\n}\n\n")
	case Optional:
		g.cf("HELPER void w_%s(asuka_writer *w, %s) {\n\tasuka_writer_bool(w, v != NULL);\n", k, cdecl(g.ptype(t), "v"))
		if t.Elem.Kind == String {
			g.cf("\tif (v) %s\n}\n\n", g.enc(t.Elem, "w", "v"))
			g.cf("HELPER char *r_%s(asuka_reader *r) { return asuka_reader_u8(r) ? asuka_reader_string(r) : NULL; }\n\n", k)
			return
		}
		g.cf("\tif (v) %s\n}\n\n", g.enc(t.Elem, "w", "(*v)"))
		g.cf("HELPER %s {\n\tif (!asuka_reader_u8(r)) return NULL;\n\t%s = malloc(sizeof *v);\n\t*v = %s;\n\treturn v;\n}\n\n", cdecl(ct, "r_"+k+"(asuka_reader *r)"), cdecl(ct, "v"), g.dec(t.Elem, "r"))
	}
}

func (g *cGen) callback(k string, ft *FuncType) {
	res := "void"
	if ft.Result != nil {
		res = g.ctype(ft.Result)
	}
	ps := []string{g.pre + k + " f"}
	for i, a := range ft.Params {
		ps = append(ps, cdecl(g.ptype(a.Type), fmt.Sprintf("p%d", i)))
	}
	g.cf("%s {\n\tasuka_writer w = {0};\n", cdecl(res, g.pre+k+"_call("+strings.Join(ps, ", ")+")"))
	for i, a := range ft.Params {
		g.cf("\t%s\n", g.enc(a.Type, "&w", paramVal(a.Type, fmt.Sprintf("p%d", i))))
	}
	if ft.Result == nil {
		g.cf("\tasuka_native_notify(f.id, &w);\n\tasuka_writer_free(&w);\n}\n\n")
		return
	}
	g.cf("\tasuka_writer a;\n\tasuka_native_ask(f.id, &w, &a);\n\tasuka_writer_free(&w);\n\tasuka_reader r = {a.data, a.len, 0, false};\n")
	g.cf("\t%s = %s;\n\tasuka_writer_free(&a);\n\treturn v;\n}\n\n", cdecl(g.ctype(ft.Result), "v"), g.dec(ft.Result, "&r"))
}

// args decodes a method's arguments into locals a0…, returning the
// call's arguments and what frees them.
func (g *cGen) args(m *Method, indent, prefix string) (call []string, frees []string) {
	for i, a := range m.Params {
		v := fmt.Sprintf("%sa%d", prefix, i)
		if prefix == "" {
			g.cf("%s%s = %s;\n", indent, cdecl(g.ctype(a.Type), v), g.dec(a.Type, "r"))
		} else {
			g.cf("%s%s = %s;\n", indent, v, g.dec(a.Type, "r"))
		}
		call = append(call, argOf(a.Type, v))
		if fr := g.free(a.Type, v); fr != "" {
			frees = append(frees, fr)
		}
	}
	return call, frees
}

func (g *cGen) calls() {
	p := g.p
	g.cf("static void *make(uint32_t iface, asuka_error *err) {\n\tswitch (iface) {\n")
	for _, it := range p.Interfaces {
		g.cf("\tcase %d:\n\t\treturn %smake_%s(err);\n", it.Index, g.pre, it.Name)
	}
	g.cf("\t}\n\tasuka_error_unsupported(err, \"%s: no interface %%u\", (unsigned)iface);\n\treturn NULL;\n}\n\n", p.Path)
	g.cf("static void release(void *obj, uint32_t iface) {\n\tswitch (iface) {\n")
	for _, it := range p.Interfaces {
		g.cf("\tcase %d: {\n\t\t%s%s *o = obj;\n\t\tif (o->vt->release) o->vt->release(o);\n\t\telse free(o);\n\t\treturn;\n\t}\n", it.Index, g.pre, it.Name)
	}
	g.cf("\t}\n\t(void)obj;\n}\n\n")
	// Synchronous calls.
	g.cf("static void call(void *obj, uint32_t iface, uint32_t method, asuka_reader *r, asuka_writer *w, asuka_error *err) {\n\tswitch (iface) {\n")
	for _, it := range p.Interfaces {
		sync := false
		for _, m := range it.Methods {
			sync = sync || !m.Context
		}
		if !sync {
			continue
		}
		g.cf("\tcase %d: {\n\t\t%s%s *o = obj;\n\t\tswitch (method) {\n", it.Index, g.pre, it.Name)
		for _, m := range it.Methods {
			if m.Context {
				continue
			}
			mn := cName(lowerCamel(m.Name))
			g.cf("\t\tcase %d: { // %s\n", m.Index, lowerCamel(m.Name))
			args, frees := g.args(m, "\t\t\t", "")
			g.cf("\t\t\tif (r->failed) asuka_error_failed(err, \"asuka\", 0, \"%s.%s: a malformed call\");\n", it.Name, lowerCamel(m.Name))
			g.cf("\t\t\telse if (!o->vt->%s) asuka_error_unsupported(err, \"%s.%s isn't implemented here\");\n", mn, it.Name, lowerCamel(m.Name))
			callArgs := strings.Join(append(append([]string{"o"}, args...), "err"), ", ")
			if m.Result != nil {
				g.cf("\t\t\telse {\n\t\t\t\t%s = o->vt->%s(%s);\n\t\t\t\tif (!err->status) %s\n", cdecl(g.ctype(m.Result), "v"), mn, callArgs, g.enc(m.Result, "w", "v"))
				if fr := g.free(m.Result, "v"); fr != "" {
					g.cf("\t\t\t\t%s\n", fr)
				}
				g.cf("\t\t\t}\n")
			} else {
				g.cf("\t\t\telse o->vt->%s(%s);\n", mn, callArgs)
			}
			for _, fr := range frees {
				g.cf("\t\t\t%s\n", fr)
			}
			g.cf("\t\t\treturn;\n\t\t}\n")
		}
		g.cf("\t\t}\n\t\tbreak;\n\t}\n")
	}
	g.cf("\t}\n\t(void)obj;\n\t(void)r;\n\t(void)w;\n\tasuka_error_unsupported(err, \"%s: no method %%u.%%u\", (unsigned)iface, (unsigned)method);\n}\n\n", p.Path)
	// Async calls: their arguments live until they answer.
	for _, it := range p.Interfaces {
		for _, m := range it.Methods {
			if !m.Context || len(m.Params) == 0 {
				continue
			}
			n := fmt.Sprintf("args_%s_%s", it.Name, lowerCamel(m.Name))
			g.cf("typedef struct {\n")
			for i, a := range m.Params {
				g.cf("\t%s;\n", cdecl(g.ctype(a.Type), fmt.Sprintf("a%d", i)))
			}
			g.cf("} %s;\n\nstatic void free_%s(void *p) {\n\t%s *a = p;\n", n, n, n)
			for i, a := range m.Params {
				if fr := g.free(a.Type, fmt.Sprintf("a->a%d", i)); fr != "" {
					g.cf("\t%s\n", fr)
				}
			}
			g.cf("\tfree(a);\n}\n\n")
		}
	}
	g.cf("static void start(void *obj, uint32_t iface, uint32_t method, asuka_reader *r, asuka_call *call) {\n\tasuka_error err = {0};\n\tswitch (iface) {\n")
	for _, it := range p.Interfaces {
		async := false
		for _, m := range it.Methods {
			async = async || m.Context
		}
		if !async {
			continue
		}
		g.cf("\tcase %d: {\n\t\t%s%s *o = obj;\n\t\tswitch (method) {\n", it.Index, g.pre, it.Name)
		for _, m := range it.Methods {
			if !m.Context {
				continue
			}
			mn := cName(lowerCamel(m.Name))
			g.cf("\t\tcase %d: { // %s\n", m.Index, lowerCamel(m.Name))
			var callArgs []string
			if len(m.Params) > 0 {
				n := fmt.Sprintf("args_%s_%s", it.Name, lowerCamel(m.Name))
				g.cf("\t\t\t%s *a = calloc(1, sizeof *a);\n", n)
				args, _ := g.args(m, "\t\t\t", "a->")
				callArgs = args
				g.cf("\t\t\tasuka_call_set_args(call, a, free_%s);\n", n)
			}
			g.cf("\t\t\tif (r->failed) asuka_error_failed(&err, \"asuka\", 0, \"%s.%s: a malformed call\");\n", it.Name, lowerCamel(m.Name))
			g.cf("\t\t\telse if (!o->vt->%s) asuka_error_unsupported(&err, \"%s.%s isn't implemented here\");\n", mn, it.Name, lowerCamel(m.Name))
			g.cf("\t\t\telse {\n\t\t\t\to->vt->%s(%s);\n\t\t\t\treturn;\n\t\t\t}\n\t\t\tasuka_call_fail(call, &err);\n\t\t\treturn;\n\t\t}\n", mn, strings.Join(append(append([]string{"o"}, callArgs...), "call"), ", "))
		}
		g.cf("\t\t}\n\t\tbreak;\n\t}\n")
	}
	g.cf("\t}\n\t(void)obj;\n\t(void)r;\n\tasuka_error_unsupported(&err, \"%s: no method %%u.%%u\", (unsigned)iface, (unsigned)method);\n\tasuka_call_fail(call, &err);\n}\n\n", p.Path)
}

func (g *cGen) done(it *Interface, m *Method) {
	g.cf("void %s {\n\tasuka_writer w = asuka_call_begin();\n", g.doneSig(it, m))
	if m.Result != nil {
		g.cf("\t%s\n", g.enc(m.Result, "&w", "value"))
		if fr := g.free(m.Result, "value"); fr != "" {
			g.cf("\t%s\n", fr)
		}
	}
	g.cf("\tasuka_call_done(call, &w);\n}\n\n")
}

func (g *cGen) views() {
	p := g.p
	if len(p.Views) == 0 {
		return
	}
	for _, v := range p.Views {
		for _, e := range v.Events {
			g.cf("%s {\n\tasuka_writer asuka_w = {0};\n", g.eventSig(v, e))
			for _, a := range e.Type.Func.Params {
				g.cf("\t%s\n", g.enc(a.Type, "&asuka_w", paramVal(a.Type, cName(a.Name))))
			}
			g.cf("\tasuka_native_emit(events.view, %s, asuka_w.data, asuka_w.len);\n\tasuka_writer_free(&asuka_w);\n}\n\n", cString(e.Name))
		}
	}
	g.cf("static void *view_make(const char *name, uint32_t view, void *parent, void **widget, uint32_t *index) {\n")
	for i, v := range p.Views {
		g.cf("\tif (strcmp(name, %s) == 0) {\n\t\t%s%s *v = %smake_%s((%s%s_events){view, parent});\n", cString(v.Name), g.pre, v.Name, g.pre, v.Name, g.pre, v.Name)
		g.cf("\t\tif (!v) return NULL;\n\t\t*widget = v->widget;\n\t\t*index = %d;\n\t\treturn v;\n\t}\n", i)
	}
	g.cf("\treturn NULL;\n}\n\n")
	g.cf("static void view_set(void *obj, uint32_t index, const char *prop, asuka_reader *r) {\n\tswitch (index) {\n")
	for i, v := range p.Views {
		g.cf("\tcase %d: {\n\t\t%s%s *v = obj;\n", i, g.pre, v.Name)
		for _, f := range v.Props {
			set := "set_" + lowerCamel(f.Name)
			g.cf("\t\tif (strcmp(prop, %s) == 0) {\n\t\t\t%s = %s;\n", cString(f.Name), cdecl(g.ctype(f.Type), "a"), g.dec(f.Type, "r"))
			g.cf("\t\t\tif (!r->failed && v->vt->%s) v->vt->%s(v, %s);\n", set, set, argOf(f.Type, "a"))
			if fr := g.free(f.Type, "a"); fr != "" {
				g.cf("\t\t\t%s\n", fr)
			}
			g.cf("\t\t\treturn;\n\t\t}\n")
		}
		g.cf("\t\t(void)v;\n\t\treturn;\n\t}\n")
	}
	g.cf("\t}\n\t(void)prop;\n\t(void)r;\n}\n\n")
	g.cf("static bool view_measure(void *obj, uint32_t index, float width, float height, float *out_width, float *out_height) {\n\tswitch (index) {\n")
	for i, v := range p.Views {
		g.cf("\tcase %d: {\n\t\t%s%s *v = obj;\n\t\treturn v->vt->measure && v->vt->measure(v, width, height, out_width, out_height);\n\t}\n", i, g.pre, v.Name)
	}
	g.cf("\t}\n\treturn false;\n}\n\n")
	g.cf("static void view_destroy(void *obj, uint32_t index) {\n\tswitch (index) {\n")
	for i, v := range p.Views {
		g.cf("\tcase %d: {\n\t\t%s%s *v = obj;\n\t\tif (v->vt->destroy) v->vt->destroy(v);\n\t\telse free(v);\n\t\treturn;\n\t}\n", i, g.pre, v.Name)
	}
	g.cf("\t}\n}\n\nstatic const asuka_native_views views = {view_make, view_set, view_measure, view_destroy};\n\n")
}
