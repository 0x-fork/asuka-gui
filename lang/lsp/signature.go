package lsp

import (
	"strings"

	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/syntax"
)

func (s *server) signatureHelp(p *TextDocumentPositionParams) (*SignatureHelp, error) {
	fc, err := s.fileAt(p.TextDocument.URI)
	if err != nil {
		return nil, nil
	}
	pos := fc.pos(p.Position)
	path := pathTo(fc.file, pos)
	for i := len(path) - 1; i >= 0; i-- {
		var args *syntax.ArgList
		var sig, doc string
		switch n := path[i].(type) {
		case *syntax.CallExpr:
			if n.Args == nil || !fc.insideParens(n.Args, pos) {
				continue
			}
			args = n.Args
			sig, doc = fc.callSignature(n)
		case *syntax.EmitStmt:
			// emit archive(id: …): the event's parameters.
			if n.Args == nil || !fc.insideParens(n.Args, pos) {
				continue
			}
			args = n.Args
			if m := fc.info.Emits[n]; m != nil {
				sig, doc = "event "+eventSig(m), docOf(m.Decl)
			}
		default:
			continue
		}
		if sig == "" {
			return nil, nil
		}
		info, names := splitSignature(sig, s.utf8)
		if doc != "" {
			info.Documentation = &MarkupContent{Kind: "markdown", Value: doc}
		}
		active := fc.activeArg(args, pos, names)
		return &SignatureHelp{Signatures: []SignatureInformation{info}, ActiveParameter: active}, nil
	}
	return nil, nil
}

// insideParens reports whether pos is between an argument list's
// parentheses.
func (fc *fileCtx) insideParens(args *syntax.ArgList, pos syntax.Pos) bool {
	if pos <= args.Start {
		return false
	}
	if pos < args.End {
		return true
	}
	// At its end: inside only when it isn't closed (still being typed).
	end := fc.offset(args.End)
	return pos == args.End && (end <= 0 || end > len(fc.m.src) || fc.m.src[end-1] != ')')
}

// activeArg is the parameter the argument at pos goes to: an argument
// goes on up to the comma after it, so f(1 |) is still the first and
// f(x: |), with its value not typed yet, x.
func (fc *fileCtx) activeArg(args *syntax.ArgList, pos syntax.Pos, names []string) int {
	for j, a := range args.List {
		if pos > a.End && fc.commaBetween(a.End, pos) {
			continue
		}
		if a.Label != nil {
			for k, n := range names {
				if n == a.Label.Name {
					return k
				}
			}
		}
		return j
	}
	return len(args.List)
}

// commaBetween reports whether the text from a to b has a comma.
func (fc *fileCtx) commaBetween(a, b syntax.Pos) bool {
	i, j := fc.offset(a), fc.offset(b)
	if i < 0 || j > len(fc.m.src) || i > j {
		return true
	}
	return strings.IndexByte(string(fc.m.src[i:j]), ',') >= 0
}

// callSignature is the signature of what a call calls, and its doc.
func (fc *fileCtx) callSignature(call *syntax.CallExpr) (string, string) {
	if c := fc.info.Calls[call]; c != nil {
		switch c.Kind {
		case check.CallFunc, check.CallMethod:
			if c.Func != nil {
				return funcSig(c.Func), docOf(c.Func.Decl)
			}
		case check.CallBuiltinMethod:
			if c.Method != nil {
				return methodSig(c.Method, c.RecvType), methodDoc(c.Method)
			}
		case check.CallBuiltin:
			if c.Builtin != nil {
				return builtinSig(c.Builtin)
			}
		case check.CallComponent, check.CallStore:
			if c.Type != nil {
				return withParens(compHeader(c.Type.Obj)), docOf(c.Type.Obj.Decl)
			}
		case check.CallStruct:
			if c.Type != nil {
				return structCtor(c.Type.Obj), docOf(c.Type.Obj.Decl)
			}
		case check.CallCase:
			if c.Case != nil {
				return "case " + c.Case.Owner.Name() + "." + caseString(c.Case), ""
			}
		case check.CallIface, check.CallNative:
			if r := fc.imethRef(memberOwner(c.RecvType, calleeName(call)), calleeName(call)); r != nil && r.imeth != nil {
				return imethSig(r.imeth), fc.declDoc(r.decl)
			}
		case check.CallClosure:
			if ft, ok := c.RecvType.(*check.FuncType); ok {
				var ps []string
				for _, t := range ft.Params {
					ps = append(ps, typeStr(t))
				}
				return calleeName(call) + "(" + strings.Join(ps, ", ") + ")" + resultString(ft.Result, ft.Async, ft.Throws), ""
			}
		}
	}
	// The checker didn't get to the call: what its callee names.
	var id *syntax.Ident
	switch f := call.Fun.(type) {
	case *syntax.Ident:
		id = f
	case *syntax.MemberExpr:
		id = f.Name
	case *syntax.ImplicitMember:
		id = f.Name
	}
	if id == nil {
		return "", ""
	}
	path := pathTo(fc.file, id.Start)
	if at(path, 1) != syntax.Node(id) {
		return "", ""
	}
	r := fc.resolve(path)
	if r == nil {
		return "", ""
	}
	switch o := r.obj.(type) {
	case *check.TypeName:
		switch {
		case o.Comp != nil:
			return withParens(compHeader(o)), docOf(o.Decl)
		case o.Kind == check.StructType:
			return structCtor(o), docOf(o.Decl)
		}
		return "", ""
	case *check.Builtin:
		return builtinSig(o)
	}
	code, doc := fc.describe(r)
	if !strings.Contains(code, "(") {
		return "", ""
	}
	return code, doc
}

// withParens is a header that takes no parameters with an empty list
// (View()), for the call's signature; one with them as it is.
func withParens(header string) string {
	if strings.Contains(header, "(") {
		return header
	}
	return header + "()"
}

func calleeName(call *syntax.CallExpr) string {
	switch f := call.Fun.(type) {
	case *syntax.Ident:
		return f.Name
	case *syntax.MemberExpr:
		return f.Name.Name
	case *syntax.ImplicitMember:
		return f.Name.Name
	}
	return "fn"
}

// structCtor is a struct's making: Todo(id: int, title: string).
func structCtor(tn *check.TypeName) string {
	var ps []string
	for _, f := range tn.Fields {
		s := f.Name + ": " + typeStr(f.Type)
		if f.Default != nil {
			s += " = " + exprString(f.Default)
		}
		ps = append(ps, s)
	}
	return tn.Name() + "(" + strings.Join(ps, ", ") + ")"
}

// splitSignature finds a signature's parameters: the label's ranges of
// what's between its first parentheses, split at their top-level commas.
// A header without parentheses ends with "()" from the caller
// (withParens).
func splitSignature(sig string, utf8 bool) (SignatureInformation, []string) {
	info := SignatureInformation{Label: sig, Parameters: []ParameterInformation{}}
	open := strings.IndexByte(sig, '(')
	if open < 0 {
		return info, nil
	}
	depth := 0
	start := open + 1
	var names []string
	addParam := func(a, b int) {
		p := strings.TrimSpace(sig[a:b])
		if p == "" {
			return
		}
		lead := len(sig[a:b]) - len(strings.TrimLeft(sig[a:b], " "))
		a += lead
		b = a + len(p)
		info.Parameters = append(info.Parameters, ParameterInformation{Label: [2]int{units(sig[:a], utf8), units(sig[:b], utf8)}})
		name := p
		if i := strings.IndexAny(p, ":="); i >= 0 {
			name = strings.TrimSpace(p[:i])
		}
		names = append(names, name)
	}
	for i := open; i < len(sig); i++ {
		switch sig[i] {
		case '(', '[', '<', '{':
			depth++
		case ')', ']', '>', '}':
			if sig[i] == '>' && i > 0 && sig[i-1] == '-' {
				continue // ->
			}
			depth--
			if depth == 0 {
				addParam(start, i)
				return info, names
			}
		case ',':
			if depth == 1 {
				addParam(start, i)
				start = i + 1
			}
		}
	}
	return info, names
}
