package check

import (
	"regexp"
	"strconv"
	"strings"

	"github.com/egoist/asuka/internal/shadercode"
	"github.com/egoist/asuka/lang/diag"
	"github.com/egoist/asuka/lang/syntax"
)

// Shaders: shader Name(params) { fn fragment(pos: vec2) -> vec4 { … } }.
//
// A shader declaration is a component: its parameters are its props
// (uniforms, typed: float, int, bool, Color), animated and children are
// props of its own, and its body is the Shader element with the program
// compiled from the declaration's functions. The functions are a subset
// of the language a GPU runs: let and var, assignment, if, while, for
// over a range, return, break and continue; numbers, color literals
// (a vec4), the vector and matrix types and GLSL's functions, swizzles
// (.rgb), indexing. Inside, size is the box's size in points, scale the
// pixels a point, time the seconds since it showed (animated).
//
// The program is compiled by translating it into Kage, the shading
// language internal/shader compiles for every GPU (Metal, AGSL, GLSL,
// HLSL), each statement on a line of its own: a mistake Kage finds is
// reported at the statement's line in the shader, its names the
// shader's. Every name of the shader's is renamed on the way (x_ a
// local, f_ a function, c_ a constant, U_ a uniform), so none is Go's
// keyword or one of Kage's builtins.

// shaderAliases are the names a shader's component uses for what it's
// made of: they can't be written, so nothing of an app's hides them.
const (
	shaderElement = "Shader·"
	shaderUniform = "Uniform·"
	shaderColor   = "colorUniform·"
	shaderProgram = "shader·"
)

// shaderMoved is what a call of the shader("""…""") of before says.
const shaderMoved = `shader("""…""") is a declaration now: shader Name(uniforms) { fn fragment(pos: vec2) -> vec4 { … } }, used as a view`

// shaderUniformName is a parameter's uniform's name in the program.
func shaderUniformName(param string) string { return "U_" + param }

// shaderComponent is the component d is: its parameters, its props
// animated and children, and the Shader element its program draws.
func (c *Checker) shaderComponent(d *syntax.ShaderDecl) *syntax.ComponentDecl {
	at := d.Name.Span
	id := func(name string) *syntax.Ident { return &syntax.Ident{Span: at, Name: name} }
	str := func(s string) *syntax.StringLit { return &syntax.StringLit{Span: at, Segments: []string{s}} }
	call := func(fun string, args ...*syntax.Arg) *syntax.CallExpr {
		return &syntax.CallExpr{Span: at, Fun: id(fun), Args: &syntax.ArgList{Span: at, List: args}}
	}
	arg := func(label string, v syntax.Expr) *syntax.Arg {
		a := &syntax.Arg{Span: at, Value: v}
		if label != "" {
			a.Label = id(label)
		}
		return a
	}
	block := func(items ...syntax.Item) *syntax.Block {
		return &syntax.Block{Span: at, Lbrace: at.Start, Items: items, Rbrace: at.End}
	}
	var uniforms []syntax.Expr
	if d.Params != nil {
		for _, p := range d.Params.List {
			name := shaderUniformName(p.Name.Name)
			kind := ""
			if nt, ok := p.Type.(*syntax.NamedType); ok && len(nt.Path) == 1 && nt.Args == nil {
				kind = nt.Path[0].Name
			}
			switch kind {
			case "Color":
				uniforms = append(uniforms, call(shaderColor, arg("", str(name)), arg("", id(p.Name.Name))))
			case "float":
				uniforms = append(uniforms, call(shaderUniform, arg("name", str(name)), arg("value", &syntax.ArrayExpr{Span: at, Elems: []syntax.Expr{id(p.Name.Name)}})))
			case "int":
				uniforms = append(uniforms, call(shaderUniform, arg("name", str(name)), arg("value", &syntax.ArrayExpr{Span: at, Elems: []syntax.Expr{call("float", arg("", id(p.Name.Name)))}})))
			case "bool":
				one := &syntax.BasicLit{Span: at, Kind: syntax.FLOAT, Value: "1.0"}
				zero := &syntax.BasicLit{Span: at, Kind: syntax.FLOAT, Value: "0.0"}
				v := &syntax.IfExpr{Span: at, Cond: id(p.Name.Name), Then: block(&syntax.ExprStmt{Span: at, X: one}), Else: block(&syntax.ExprStmt{Span: at, X: zero})}
				uniforms = append(uniforms, call(shaderUniform, arg("name", str(name)), arg("value", &syntax.ArrayExpr{Span: at, Elems: []syntax.Expr{v}})))
			default:
				c.errorf(p, diag.InvalidShader, "a shader's parameter is a uniform: a float, an int, a bool or a Color")
			}
		}
	}
	program := call(shaderProgram)
	if c.shaderDecls == nil {
		c.shaderDecls = map[*syntax.CallExpr]*syntax.ShaderDecl{}
	}
	c.shaderDecls[program] = d
	element := &syntax.CallExpr{
		Span: at,
		Fun:  id(shaderElement),
		Args: &syntax.ArgList{Span: at, List: []*syntax.Arg{arg("", program)}},
		Block: block(
			&syntax.PropSet{Span: at, Name: id("uniforms"), Value: &syntax.ArrayExpr{Span: at, Elems: uniforms}},
			&syntax.PropSet{Span: at, Name: id("animated"), Value: id("animated")},
			&syntax.ExprStmt{Span: at, X: id("children")},
		),
	}
	viewType := &syntax.OptionalType{Span: at, Elem: &syntax.NamedType{Span: at, Path: []*syntax.Ident{id("View")}}}
	body := block(
		&syntax.PropDecl{Span: at, Name: id("animated"), Default: &syntax.BoolLit{Span: at, Value: false}},
		&syntax.PropDecl{Span: at, Name: id("children"), Type: viewType, Default: &syntax.NoneLit{Span: at}},
		&syntax.ExprStmt{Span: at, X: element},
	)
	return &syntax.ComponentDecl{Span: d.Span, Trivia: d.Trivia, Pub: d.Pub, Name: d.Name, Params: d.Params, Body: body}
}

// shaderProgramCall checks the program of a shader's component: the
// declaration's functions, translated and compiled for every GPU.
func (c *Checker) shaderProgramCall(e *syntax.CallExpr, call *Call) {
	call.Result = Bytes
	d := c.shaderDecls[e]
	if d == nil {
		c.errorf(e, diag.InvalidShader, "a shader is a declaration: shader Name(params) { fn fragment(pos: vec2) -> vec4 { … } }")
		return
	}
	t := &shaderTranslator{c: c, decl: d}
	src, ok := t.translate()
	if !ok {
		return
	}
	code, err := shadercode.Compile(src)
	if err != nil {
		t.report(err)
		return
	}
	c.info.Shaders[e] = code.Encode()
}

// shaderTranslator writes a shader's functions as Kage, a line each
// statement, remembering where each line came from.
type shaderTranslator struct {
	c      *Checker
	decl   *syntax.ShaderDecl
	b      strings.Builder
	lines  []syntax.Node // the node each line of the program came from
	indent int
	failed bool
	// What the shader's names are.
	uniforms map[string]bool
	bools    map[string]bool // the uniforms that are bools (ints, 0 or 1)
	consts   map[string]bool
	funcs    map[string]bool
	scopes   []map[string]bool
}

// shaderBuiltins are the functions and types a shader calls by their
// names: GLSL's, as Kage has them.
var shaderBuiltins = map[string]bool{}

func init() {
	for _, n := range strings.Fields(`bool int float vec2 vec3 vec4 ivec2 ivec3 ivec4 uint uvec2 uvec3 uvec4 mat2 mat3 mat4
		sin cos tan asin acos atan atan2 pow exp log exp2 log2 sqrt inversesqrt abs sign floor ceil fract mod min max
		clamp mix step smoothstep length distance dot cross normalize faceforward reflect refract transpose dfdx dfdy
		fwidth discard len`) {
		shaderBuiltins[n] = true
	}
}

// shaderTypes are the types a shader's values have.
var shaderTypes = map[string]bool{"bool": true, "int": true, "float": true, "vec2": true, "vec3": true, "vec4": true,
	"ivec2": true, "ivec3": true, "ivec4": true, "uint": true, "uvec2": true, "uvec3": true, "uvec4": true,
	"mat2": true, "mat3": true, "mat4": true}

func (t *shaderTranslator) errorf(n syntax.Node, code *diag.Code, format string, args ...any) {
	t.failed = true
	t.c.errorf(n, code, format, args...)
}

// line writes a line of the program, from n.
func (t *shaderTranslator) line(n syntax.Node, s string) {
	t.b.WriteString(strings.Repeat("\t", t.indent))
	t.b.WriteString(s)
	t.b.WriteByte('\n')
	t.lines = append(t.lines, n)
}

func (t *shaderTranslator) translate() (string, bool) {
	t.uniforms, t.consts, t.funcs, t.bools = map[string]bool{}, map[string]bool{}, map[string]bool{}, map[string]bool{}
	d := t.decl
	t.line(d, "package main")
	t.line(d, "var Time float")
	if d.Params != nil {
		for _, p := range d.Params.List {
			t.uniforms[p.Name.Name] = true
			t.bools[p.Name.Name] = typeName(p.Type) == "bool"
			// A bool's an int, 0 or 1: Android's AGSL has no bool
			// uniforms.
			typ := map[string]string{"Color": "vec4", "float": "float", "int": "int", "bool": "int"}[typeName(p.Type)]
			if typ != "" {
				t.line(p, "var "+shaderUniformName(p.Name.Name)+" "+typ)
			}
		}
	}
	var fragment *syntax.FuncDecl
	for _, m := range d.Members {
		switch m := m.(type) {
		case *syntax.FuncDecl:
			if m.Name.Name == "fragment" {
				fragment = m
			} else {
				t.funcs[m.Name.Name] = true
			}
		case *syntax.LetDecl:
			if bp, ok := m.Pattern.(*syntax.BindPat); ok {
				t.consts[bp.Name.Name] = true
			}
		}
	}
	if fragment == nil {
		t.errorf(d.Name, diag.InvalidShader, "a shader needs its fragment function: fn fragment(pos: vec2) -> vec4 { … }, the pixel's color from its position")
		return "", false
	}
	for _, m := range d.Members {
		switch m := m.(type) {
		case *syntax.LetDecl:
			bp, ok := m.Pattern.(*syntax.BindPat)
			if !ok || m.Value == nil || m.Keyword != syntax.LET {
				t.errorf(m, diag.InvalidShader, "a shader's constant is let name = value")
				continue
			}
			t.line(m, "const c_"+bp.Name.Name+" = "+t.expr(m.Value))
		case *syntax.FuncDecl:
			t.fn(m, m == fragment)
		}
	}
	return t.b.String(), !t.failed
}

func typeName(ty syntax.Type) string {
	if nt, ok := ty.(*syntax.NamedType); ok && len(nt.Path) == 1 && nt.Args == nil {
		return nt.Path[0].Name
	}
	return ""
}

// typ is a shader's type as Kage's.
func (t *shaderTranslator) typ(ty syntax.Type) string {
	if n := typeName(ty); shaderTypes[n] {
		return n
	}
	if ty != nil {
		t.errorf(ty, diag.InvalidShader, "a shader's values are numbers, vectors and matrices: bool, int, float, vec2 to vec4, ivec2 to ivec4, mat2 to mat4")
	}
	return "float"
}

func (t *shaderTranslator) fn(f *syntax.FuncDecl, fragment bool) {
	if f.Async.IsValid() || f.Throws.IsValid() || len(f.TypeParams) > 0 || f.Native.IsValid() || f.Extern.IsValid() {
		t.errorf(f.Name, diag.InvalidShader, "a shader's function is fn name(params) -> T { … }: not async, throwing, generic, native or extern")
	}
	t.scopes = []map[string]bool{{}}
	var params []string
	if f.Params != nil {
		for _, p := range f.Params.List {
			if p.Default != nil {
				t.errorf(p, diag.InvalidShader, "a shader's function's parameters have no defaults")
			}
			t.scopes[0][p.Name.Name] = true
			params = append(params, "x_"+p.Name.Name+" "+t.typ(p.Type))
		}
	}
	name := "f_" + f.Name.Name
	if fragment {
		if len(params) != 1 || typeName(f.Params.List[0].Type) != "vec2" || typeName(f.Result) != "vec4" {
			t.errorf(f.Name, diag.InvalidShader, "fragment takes the pixel's position and gives its color: fn fragment(pos: vec2) -> vec4, the color premultiplied")
			return
		}
		name = "Fragment"
		params = append([]string{"dst vec4"}, params...)
	}
	result := ""
	if f.Result != nil {
		result = " " + t.typ(f.Result)
	}
	t.line(f, "func "+name+"("+strings.Join(params, ", ")+")"+result+" {")
	if f.Value != nil {
		t.indent++
		t.line(f.Value, "return "+t.expr(f.Value))
		t.indent--
	} else if f.Body != nil {
		t.block(f.Body)
	}
	t.line(f, "}")
}

// block writes a block's statements, a scope of their own.
func (t *shaderTranslator) block(b *syntax.Block) {
	t.indent++
	t.scopes = append(t.scopes, map[string]bool{})
	for _, it := range b.Items {
		t.stmt(it)
	}
	t.scopes = t.scopes[:len(t.scopes)-1]
	t.indent--
}

func (t *shaderTranslator) declare(name string) { t.scopes[len(t.scopes)-1][name] = true }

func (t *shaderTranslator) local(name string) bool {
	for i := len(t.scopes) - 1; i >= 0; i-- {
		if t.scopes[i][name] {
			return true
		}
	}
	return false
}

var assignOps = map[syntax.Token]string{syntax.ASSIGN: "=", syntax.ADD_ASSIGN: "+=", syntax.SUB_ASSIGN: "-=", syntax.MUL_ASSIGN: "*=", syntax.QUO_ASSIGN: "/=", syntax.REM_ASSIGN: "%="}

func (t *shaderTranslator) stmt(it syntax.Item) {
	switch s := it.(type) {
	case *syntax.LetDecl:
		bp, ok := s.Pattern.(*syntax.BindPat)
		if !ok || s.Value == nil || s.Else != nil {
			t.errorf(s, diag.InvalidShader, "a shader's let and var name one value: let x = …")
			return
		}
		v := t.expr(s.Value)
		t.declare(bp.Name.Name)
		if s.Type != nil {
			t.line(s, "var x_"+bp.Name.Name+" "+t.typ(s.Type)+" = "+v)
		} else {
			t.line(s, "x_"+bp.Name.Name+" := "+v)
		}
	case *syntax.AssignStmt:
		op, ok := assignOps[s.Op]
		if !ok {
			t.errorf(s, diag.InvalidShader, "a shader can't assign with %s", s.Op)
			return
		}
		t.line(s, t.expr(s.Target)+" "+op+" "+t.expr(s.Value))
	case *syntax.IfStmt:
		t.ifStmt(s, "if ")
	case *syntax.WhileStmt:
		cond, ok := s.Cond.(syntax.Expr)
		if !ok {
			t.errorf(s, diag.InvalidShader, "a shader's while takes a condition, not a let")
			return
		}
		t.line(s, "for "+t.expr(cond)+" {")
		t.block(s.Body)
		t.line(s, "}")
	case *syntax.ForStmt:
		bp, ok := s.Pattern.(*syntax.BindPat)
		r, isRange := s.Seq.(*syntax.BinaryExpr)
		if !ok || !isRange || (r.Op != syntax.RANGE && r.Op != syntax.RANGELT) || s.Await.IsValid() || s.Key != nil {
			t.errorf(s, diag.InvalidShader, "a shader's for counts over a range: for i in 0..<n { … }")
			return
		}
		from, to := t.expr(r.X), t.expr(r.Y)
		cmp := "<"
		if r.Op == syntax.RANGE {
			cmp = "<="
		}
		t.scopes = append(t.scopes, map[string]bool{bp.Name.Name: true})
		v := "x_" + bp.Name.Name
		t.line(s, "for "+v+" := "+from+"; "+v+" "+cmp+" "+to+"; "+v+"++ {")
		t.block(s.Body)
		t.scopes = t.scopes[:len(t.scopes)-1]
		t.line(s, "}")
	case *syntax.ReturnStmt:
		if s.Value == nil {
			t.line(s, "return")
		} else {
			t.line(s, "return "+t.expr(s.Value))
		}
	case *syntax.BranchStmt:
		if s.Tok == syntax.BREAK {
			t.line(s, "break")
		} else {
			t.line(s, "continue")
		}
	case *syntax.ExprStmt:
		if _, ok := s.X.(*syntax.CallExpr); !ok {
			t.errorf(s, diag.InvalidShader, "a statement of a shader's is a call: its value isn't used otherwise")
			return
		}
		t.line(s, t.expr(s.X))
	default:
		t.errorf(it, diag.InvalidShader, "a shader's functions have let and var, assignments, if, while, for over a range, return, break and continue")
	}
}

func (t *shaderTranslator) ifStmt(s *syntax.IfStmt, prefix string) {
	cond, ok := s.Cond.(syntax.Expr)
	if !ok {
		t.errorf(s, diag.InvalidShader, "a shader's if takes a condition, not a let")
		return
	}
	t.line(s, prefix+t.expr(cond)+" {")
	t.block(s.Then)
	switch e := s.Else.(type) {
	case nil:
		t.line(s, "}")
	case *syntax.IfStmt:
		t.ifStmtElse(e)
	case *syntax.Block:
		t.line(s, "} else {")
		t.block(e)
		t.line(s, "}")
	}
}

// ifStmtElse writes } else if …: the closing brace of the if before on
// the same line, as Go wants it.
func (t *shaderTranslator) ifStmtElse(s *syntax.IfStmt) { t.ifStmt(s, "} else if ") }

var binaryOps = map[syntax.Token]bool{}

func init() {
	for _, op := range []syntax.Token{syntax.ADD, syntax.SUB, syntax.MUL, syntax.QUO, syntax.REM, syntax.EQL, syntax.NEQ, syntax.LSS,
		syntax.LEQ, syntax.GTR, syntax.GEQ, syntax.LAND, syntax.LOR, syntax.AMP, syntax.PIPE, syntax.CARET, syntax.SHL, syntax.SHR} {
		binaryOps[op] = true
	}
}

func (t *shaderTranslator) expr(e syntax.Expr) string {
	switch x := e.(type) {
	case *syntax.Ident:
		return t.name(x)
	case *syntax.BasicLit:
		switch x.Kind {
		case syntax.INT, syntax.FLOAT:
			return x.Value
		case syntax.COLOR:
			return t.color(x)
		}
		t.errorf(x, diag.InvalidShader, "a shader's literals are numbers and colors")
		return "0"
	case *syntax.BoolLit:
		return strconv.FormatBool(x.Value)
	case *syntax.ParenExpr:
		return "(" + t.expr(x.X) + ")"
	case *syntax.UnaryExpr:
		switch x.Op {
		case syntax.SUB:
			return "-" + t.expr(x.X)
		case syntax.NOT:
			return "!" + t.expr(x.X)
		}
		t.errorf(x, diag.InvalidShader, "a shader can't use %s", x.Op)
		return "0"
	case *syntax.BinaryExpr:
		if !binaryOps[x.Op] {
			t.errorf(x, diag.InvalidShader, "a shader can't use %s here", x.Op)
			return "0"
		}
		return t.expr(x.X) + " " + x.Op.String() + " " + t.expr(x.Y)
	case *syntax.MemberExpr:
		if x.Optional {
			t.errorf(x, diag.InvalidShader, "a shader has no optionals")
		}
		return t.expr(x.X) + "." + x.Name.Name
	case *syntax.IndexExpr:
		return t.expr(x.X) + "[" + t.expr(x.Index) + "]"
	case *syntax.CallExpr:
		return t.call(x)
	case *syntax.IfExpr:
		t.errorf(x, diag.InvalidShader, "a shader's if is a statement, not a value: var x = …; if c { x = … } else { x = … } (or mix, step)")
		return "0"
	}
	t.errorf(e, diag.InvalidShader, "a shader's expressions are numbers, vectors and matrices: arithmetic, comparisons, calls, swizzles (v.rgb) and indexing")
	return "0"
}

// name is a name in a shader: a local's, a parameter's (a uniform), a
// constant's, a function's, or one the shader has of its own.
func (t *shaderTranslator) name(x *syntax.Ident) string {
	n := x.Name
	switch {
	case t.local(n):
		return "x_" + n
	case t.uniforms[n] && t.bools[n]:
		return "(" + shaderUniformName(n) + " != 0)"
	case t.uniforms[n]:
		return shaderUniformName(n)
	case t.consts[n]:
		return "c_" + n
	case n == "size":
		return "rectSize()"
	case n == "scale":
		return "pixelScale()"
	case n == "time":
		return "Time"
	case t.funcs[n] || shaderBuiltins[n]:
		t.errorf(x, diag.NotAValue, "%s is a function: call it", n)
		return "0"
	}
	t.errorf(x, diag.UndefinedName, "unknown name %s in a shader (its own: size, scale, time; its parameters, constants and locals)", n)
	return "0"
}

func (t *shaderTranslator) call(x *syntax.CallExpr) string {
	f, ok := x.Fun.(*syntax.Ident)
	if !ok || x.Block != nil || len(x.Handlers) > 0 || len(x.TypeArgs) > 0 || x.Args == nil {
		t.errorf(x, diag.InvalidShader, "a shader calls a function by its name: f(a, b)")
		return "0"
	}
	var args []string
	for _, a := range x.Args.List {
		if a.Label != nil {
			t.errorf(a, diag.InvalidShader, "a shader's arguments are in order, without labels")
		}
		args = append(args, t.expr(a.Value))
	}
	var name string
	switch {
	case t.local(f.Name) || t.uniforms[f.Name] || t.consts[f.Name]:
		t.errorf(f, diag.NotCallable, "%s isn't a function", f.Name)
		return "0"
	case t.funcs[f.Name]:
		name = "f_" + f.Name
	case f.Name == "fragment":
		t.errorf(f, diag.InvalidShader, "fragment is the shader's entry: the GPU calls it")
		return "0"
	case shaderBuiltins[f.Name]:
		name = f.Name
	default:
		t.errorf(f, diag.UndefinedName, "unknown function %s in a shader (GLSL's: sin, mix, smoothstep, …; the vector and matrix types; the shader's own)", f.Name)
		return "0"
	}
	return name + "(" + strings.Join(args, ", ") + ")"
}

// color is a color literal (#rrggbb, #rrggbbaa, #rgb) as a vec4, its
// channels 0 to 1, not premultiplied.
func (t *shaderTranslator) color(x *syntax.BasicLit) string {
	h := strings.TrimPrefix(x.Value, "#")
	if len(h) == 3 || len(h) == 4 {
		var b strings.Builder
		for _, r := range h {
			b.WriteRune(r)
			b.WriteRune(r)
		}
		h = b.String()
	}
	if len(h) == 6 {
		h += "ff"
	}
	if len(h) != 8 {
		t.errorf(x, diag.InvalidLiteral, "a color literal is #rgb, #rrggbb or #rrggbbaa")
		return "vec4(0)"
	}
	var ch []string
	for i := 0; i < 8; i += 2 {
		v, err := strconv.ParseUint(h[i:i+2], 16, 8)
		if err != nil {
			t.errorf(x, diag.InvalidLiteral, "a color literal's digits are hexadecimal")
			return "vec4(0)"
		}
		ch = append(ch, strconv.FormatFloat(float64(v)/255, 'g', 6, 64))
	}
	return "vec4(" + strings.Join(ch, ", ") + ")"
}

var kageError = regexp.MustCompile(`^(\d+):(\d+): (.*)$`)
var kageNames = regexp.MustCompile(`\b[xfcU]_([A-Za-z_][A-Za-z0-9_]*)\b`)

// report says what Kage found wrong, at the shader's statement it came
// from, in the shader's names.
func (t *shaderTranslator) report(err error) {
	for _, l := range strings.Split(err.Error(), "\n") {
		m := kageError.FindStringSubmatch(strings.TrimSpace(l))
		at := syntax.Node(t.decl.Name)
		msg := strings.TrimSpace(l)
		if m != nil {
			if n, _ := strconv.Atoi(m[1]); n >= 1 && n <= len(t.lines) {
				at = t.lines[n-1]
			}
			msg = m[3]
		}
		msg = strings.TrimPrefix(kageNames.ReplaceAllString(msg, "$1"), "shader: ")
		msg = strings.NewReplacer("Fragment", "fragment", "rectSize()", "size", "pixelScale()", "scale", "Time", "time").Replace(msg)
		if msg != "" {
			t.c.errorf(at, diag.InvalidShader, "the shader: %s", msg)
		}
	}
}
