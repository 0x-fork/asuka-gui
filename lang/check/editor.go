package check

// What editors (lang/lsp) ask of a checked program beyond Info: the
// built-in methods a type has, what a struct's members used by their
// bare names in its methods are, the semantic colors.

// MethodsOf lists the built-in methods of a value of type t ([T].map,
// string.trim), each overload once, in the table's order.
func MethodsOf(t Type) []*Method {
	k := recvKind(t)
	if k == "" {
		return nil
	}
	var out []*Method
	for _, m := range Methods {
		if m.Recv == k {
			out = append(out, m)
		}
	}
	return out
}

// Instantiate is m's parameters and result for a receiver of type recv:
// T, K and V replaced with its types (U, which a call infers, stays).
func (m *Method) Instantiate(recv Type) ([]*Param, Type) {
	s := m.bind(recv)
	ps := make([]*Param, len(m.Params))
	for i, p := range m.Params {
		ps[i] = &Param{Name: p.Name, Type: s.Apply(p.Type), Default: p.Default}
	}
	return ps, s.Apply(m.Result)
}

// SelfMember reports what o is when it's a struct's or an enum's field or
// method used by its bare name in one of its methods: the field or the
// method, and the type they're of.
func SelfMember(o Object) (field *Field, method *Function, owner *TypeName, ok bool) {
	if m, is := o.(*selfMember); is {
		return m.field, m.method, m.owner, true
	}
	return nil, nil, nil, false
}

// SemanticColors are the names of the colors .name stands for where a
// Color is expected (.label, .accent, ...).
func SemanticColors() []string { return semanticColorNames() }

// IsLoadingType reports whether tn is Loading<T>, the type load makes.
func IsLoadingType(tn *TypeName) bool { return tn == loadingT }

// Exported reports whether o, declared in package p, may be used from
// another package.
func Exported(p *Package, o Object) bool { return (*Checker)(nil).exported(p, o) }
