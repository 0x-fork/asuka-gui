package check

import (
	"github.com/egoist/asuka/lang/diag"
	"github.com/egoist/asuka/lang/syntax"
)

// Seq<T>, a lazy sequence (core/src/rt/seq.c): xs.lazy() and the other
// collections', ranges', strings' characters', and the generators
// Seq(first:next:), Seq(state:next:), Seq(repeating:), Seq(from:by:).
// Its adapters (map, filter, take, ...) compute nothing until something
// reads it: for, or a consumer (toArray, reduce, first, ...). An
// adapter's closure runs later, when the Seq is read, so it can't throw
// (as Swift's lazy map's); a consumer's runs at once, and the consumer
// throws what it throws (Method.Rethrows).

// Seq is Seq<T>.
type Seq struct{ Elem Type }

func (t *Seq) String() string { return "Seq<" + t.Elem.String() + ">" }

// seqMethods are Seq's methods, and those that make one or order by a
// key, appended to Methods. Extra "seqProto": the lowering passes an
// empty [X] of the result Seq<X>'s elements (their kind and shape);
// "proto": an empty value of the result ([T], Set<T>, [K: V]), which the
// consumer fills.
var seqMethods = []*Method{
	// Making one.
	{Recv: "array", Name: "lazy", Sig: "() -> Seq<T>", Builtin: "seq.of_array", Extra: "seqProto", Doc: "the elements as a lazy sequence: its map, filter, take and the rest compute nothing until it's read, one element at a time, with no array in between"},
	{Recv: "map", Name: "lazy", Sig: "() -> Seq<(K, V)>", Builtin: "seq.of_map", Extra: "seqProto", Doc: "the (key, value) pairs as a lazy sequence, in insertion order"},
	{Recv: "set", Name: "lazy", Sig: "() -> Seq<T>", Builtin: "seq.of_set", Extra: "seqProto", Doc: "the elements as a lazy sequence, in insertion order"},
	{Recv: "string", Name: "lazy", Sig: "() -> Seq<string>", Builtin: "seq.of_string", Extra: "seqProto", Doc: "the characters as a lazy sequence, each a string"},

	// An array is a double-ended queue too: its start changes in O(1),
	// amortized, as its end does.
	{Recv: "array", Name: "pushFirst", Sig: "(value: T)", Builtin: "arr.push_first", Inout: true, Doc: "adds value at the start, in O(1) (amortized), as push adds at the end"},
	{Recv: "array", Name: "popFirst", Sig: "() -> T?", Builtin: "arr.pop_first", Inout: true, Doc: "removes and returns the first element, in O(1) (amortized), as pop the last; none when it's empty"},

	// Ordering by a key: the key computed once per element, compared as
	// sorted() compares (a type's compare too).
	{Recv: "array", Name: "sorted", Sig: "(key: fn(T) throws -> U, descending: bool = false) -> [T]", Builtin: "arr.sorted_key", Extra: "kindU", Needs: "ComparableU", Rethrows: true, Doc: "sorted by what key gives each element, a Comparable (people.sorted(key: p => p.age)); descending: greatest first; stable"},
	{Recv: "array", Name: "sort", Sig: "(key: fn(T) throws -> U, descending: bool = false)", Builtin: "arr.sort_key", Inout: true, Extra: "kindU", Needs: "ComparableU", Rethrows: true, Doc: "sorts by what key gives each element, as sorted(key:)"},
	{Recv: "array", Name: "min", Sig: "(key: fn(T) throws -> U) -> T?", Builtin: "arr.min_key", Extra: "kindU", Needs: "ComparableU", Rethrows: true, Doc: "the element whose key is least (the first of equal ones): people.min(key: p => p.age)"},
	{Recv: "array", Name: "max", Sig: "(key: fn(T) throws -> U) -> T?", Builtin: "arr.max_key", Extra: "kindU", Needs: "ComparableU", Rethrows: true, Doc: "the element whose key is greatest (the first of equal ones)"},

	// Adapters: a new Seq, computed as it's read.
	{Recv: "seq", Name: "map", Sig: "(transform: fn(T) -> U) -> Seq<U>", Builtin: "seq.map", Extra: "seqProto", Doc: "what transform makes of each element, as it's read"},
	{Recv: "seq", Name: "filter", Sig: "(where: fn(T) -> bool) -> Seq<T>", Builtin: "seq.filter", Doc: "the elements the predicate is true of"},
	{Recv: "seq", Name: "compactMap", Sig: "(transform: fn(T) -> U?) -> Seq<U>", Builtin: "seq.compact_map", Extra: "seqProto", Doc: "what transform gives, without its nones"},
	{Recv: "seq", Name: "flatMap", Sig: "(transform: fn(T) -> Seq<U>) -> Seq<U>", Builtin: "seq.flat_map", Extra: "seqProto", Doc: "the elements of the sequences transform gives, one after another"},
	{Recv: "seq", Name: "enumerated", Sig: "() -> Seq<(int, T)>", Builtin: "seq.enumerated", Extra: "seqProto", Doc: "the elements with their places, from 0"},
	{Recv: "seq", Name: "zip", Sig: "(other: Seq<U>) -> Seq<(T, U)>", Builtin: "seq.zip", Extra: "seqProto", Doc: "pairs of this sequence's and other's elements, as long as the shorter"},
	{Recv: "seq", Name: "take", Sig: "(count: int) -> Seq<T>", Builtin: "seq.take", Doc: "the first count elements: what comes after isn't computed"},
	{Recv: "seq", Name: "skip", Sig: "(count: int) -> Seq<T>", Builtin: "seq.skip", Doc: "the elements after the first count"},
	{Recv: "seq", Name: "takeWhile", Sig: "(where: fn(T) -> bool) -> Seq<T>", Builtin: "seq.take_while", Doc: "the elements up to the first the predicate isn't true of"},
	{Recv: "seq", Name: "skipWhile", Sig: "(where: fn(T) -> bool) -> Seq<T>", Builtin: "seq.skip_while", Doc: "the elements from the first the predicate isn't true of"},
	{Recv: "seq", Name: "chunked", Sig: "(size: int) -> Seq<[T]>", Builtin: "seq.chunked", Extra: "seqProto", Fails: true, Doc: "the elements in arrays of size, the last holding what's left; a size under 1 is an error"},
	{Recv: "seq", Name: "windows", Sig: "(size: int) -> Seq<[T]>", Builtin: "seq.windows", Extra: "seqProto", Fails: true, Doc: "each size elements in a row, a window sliding by one ([1, 2, 3] by 2: [1, 2], [2, 3]); none when there are fewer; a size under 1 is an error"},
	{Recv: "seq", Name: "onEach", Sig: "(action: fn(T)) -> Seq<T>", Builtin: "seq.on_each", Doc: "the same elements, action called with each as it's read"},
	{Recv: "seq", Name: "unique", Sig: "() -> Seq<T>", Builtin: "seq.unique", Needs: "Hashable", Doc: "each element once, where it first comes"},
	{Recv: "seq", Name: "concat", Sig: "(other: Seq<T>) -> Seq<T>", Builtin: "seq.concat", Doc: "these elements, then other's"},

	// Consumers: they read it now.
	{Recv: "seq", Name: "toArray", Sig: "() -> [T]", Builtin: "seq.to_array", Extra: "proto", Fails: true, Doc: "the elements, in an array"},
	{Recv: "seq", Name: "toSet", Sig: "() -> Set<T>", Builtin: "seq.to_set", Extra: "proto", Needs: "Hashable", Fails: true, Doc: "the distinct elements, in a set"},
	{Recv: "seq", Name: "toMap", Sig: "() -> [K: V]", Builtin: "seq.to_map", Extra: "proto", Needs: "pairs", Fails: true, Doc: "a map of (key, value) pairs; a key that comes again takes the later value"},
	{Recv: "seq", Name: "count", Sig: "() -> int", Builtin: "seq.len", Fails: true, Doc: "the number of elements (reading them all)"},
	{Recv: "seq", Name: "count", Sig: "(where: fn(T) throws -> bool) -> int", Builtin: "seq.count_where", Rethrows: true, Fails: true, Doc: "the number of elements the predicate is true of"},
	{Recv: "seq", Name: "first", Sig: "() -> T?", Builtin: "seq.first", Fails: true, Doc: "the first element, none when there's none: only it is computed"},
	{Recv: "seq", Name: "last", Sig: "() -> T?", Builtin: "seq.last", Fails: true},
	{Recv: "seq", Name: "find", Sig: "(where: fn(T) throws -> bool) -> T?", Builtin: "seq.find", Rethrows: true, Fails: true, Doc: "the first element the predicate is true of: what comes after isn't computed"},
	{Recv: "seq", Name: "any", Sig: "(where: fn(T) throws -> bool) -> bool", Builtin: "seq.any", Rethrows: true, Fails: true, Doc: "whether the predicate is true of an element: it stops at the first"},
	{Recv: "seq", Name: "all", Sig: "(where: fn(T) throws -> bool) -> bool", Builtin: "seq.all", Rethrows: true, Fails: true, Doc: "whether the predicate is true of every element: it stops at the first it isn't"},
	{Recv: "seq", Name: "contains", Sig: "(value: T) -> bool", Builtin: "seq.contains", Needs: "Equatable", Fails: true},
	{Recv: "seq", Name: "reduce", Sig: "(initial: U, combine: fn(U, T) throws -> U) -> U", Builtin: "seq.reduce", Extra: "kindU", Rethrows: true, Fails: true, Doc: "combine of initial and the first element, then of that and the next, and so on"},
	{Recv: "seq", Name: "sum", Sig: "() -> T", Builtin: "seq.sum", Needs: "number", Fails: true},
	{Recv: "seq", Name: "min", Sig: "() -> T?", Builtin: "seq.min", Needs: "Comparable", Fails: true, Doc: "the least element (the first of equal ones), none when it's empty"},
	{Recv: "seq", Name: "min", Sig: "(by: fn(T, T) throws -> bool) -> T?", Builtin: "seq.min", Rethrows: true, Fails: true, Doc: "the least element by a less-than"},
	{Recv: "seq", Name: "min", Sig: "(key: fn(T) throws -> U) -> T?", Builtin: "seq.min_key", Extra: "kindU", Needs: "ComparableU", Rethrows: true, Fails: true, Doc: "the element whose key is least (the first of equal ones)"},
	{Recv: "seq", Name: "max", Sig: "() -> T?", Builtin: "seq.max", Needs: "Comparable", Fails: true, Doc: "the greatest element (the first of equal ones), none when it's empty"},
	{Recv: "seq", Name: "max", Sig: "(by: fn(T, T) throws -> bool) -> T?", Builtin: "seq.max", Rethrows: true, Fails: true, Doc: "the greatest element by a less-than"},
	{Recv: "seq", Name: "max", Sig: "(key: fn(T) throws -> U) -> T?", Builtin: "seq.max_key", Extra: "kindU", Needs: "ComparableU", Rethrows: true, Fails: true, Doc: "the element whose key is greatest (the first of equal ones)"},
	{Recv: "seq", Name: "joined", Sig: "(separator: string = \"\") -> string", Builtin: "seq.joined", Needs: "string", Fails: true},
}

// seqBind binds what a Seq's methods name: T, and K and V when its
// elements are pairs (toMap).
func seqBind(s Subst, x *Seq) {
	s[tvT] = x.Elem
	if t, ok := x.Elem.(*Tuple); ok && len(t.Elems) == 2 {
		s[tvK], s[tvV] = t.Elems[0], t.Elems[1]
	}
}

// seqRequires is requires' for the needs Seq's methods and the key
// orderings add.
func (m *Method) seqRequires(s Subst) string {
	switch m.Needs {
	case "ComparableU":
		if u := s[tvU]; u != nil && !IsInvalid(u) {
			if why := whyNotComparable(u); why != "" {
				return "(key:) orders by what key gives, and " + typeString(u) + " isn't Comparable: " + why
			}
		}
	case "pairs":
		t := s[tvT]
		if tup, ok := t.(*Tuple); !ok || len(tup.Elems) != 2 {
			return "makes a map of (key, value) pairs, and these are " + typeString(t) + "s: map(x => (key, value)) first"
		}
		if why := whyNotHashable(s[tvK]); why != "" {
			return "makes a map's keys, and " + typeString(s[tvK]) + " isn't Hashable: " + why
		}
	}
	return ""
}

// overloadByLabel picks among a built-in method's overloads of one
// arity by what the arguments say: a label one of them has and the
// others don't (sorted(by:), sorted(key:)), or a closure's parameter
// count against the function a parameter takes (sorted((a, b) => …):
// by's). nil when that doesn't tell.
func (c *Checker) overloadByLabel(e *syntax.CallExpr, ms []*Method) *Method {
	if e.Args == nil {
		return nil
	}
	for i, a := range e.Args.List {
		var hit []*Method
		for _, m := range ms {
			ps, _ := c.methodSig(m)
			switch {
			case a.Label != nil:
				for _, p := range ps {
					if p.Name == a.Label.Name {
						hit = append(hit, m)
						break
					}
				}
			case i < len(ps):
				lit, ok := unparen(a.Value).(*syntax.LambdaExpr)
				ft, isFn := ps[i].Type.(*FuncType)
				if ok && isFn && len(lit.Params) == len(ft.Params) {
					hit = append(hit, m)
				}
			}
		}
		if len(hit) == 1 {
			return hit[0]
		}
	}
	return nil
}

// seqCall checks Seq(…): a generator.
//
//	Seq(first: x, next: f)       x, f(x), f(f(x)), … until f gives none
//	Seq(state: s, next: f)       f(s) gives (element, next state), or none for the end
//	Seq(repeating: x)            x without end; count: n, n times
//	Seq(from: n)                 n, n + 1, … without end; by: the step
func (c *Checker) seqCall(e *syntax.CallExpr, call *Call, want Type) {
	var first string
	if e.Args != nil && len(e.Args.List) > 0 && e.Args.List[0].Label != nil {
		first = e.Args.List[0].Label.Name
	}
	var elem Type
	if len(e.TypeArgs) == 1 {
		elem = c.typeExpr(e.TypeArgs[0])
	} else if st, ok := Unopt(want).(*Seq); ok {
		elem = st.Elem
	}
	tvS := tvU
	var params []*Param
	switch first {
	case "first":
		params = []*Param{{Name: "first", Type: tvT}, {Name: "next", Type: &FuncType{Params: []Type{tvT}, Result: &Optional{tvT}}}}
	case "state":
		params = []*Param{{Name: "state", Type: tvS}, {Name: "next", Type: &FuncType{Params: []Type{tvS}, Result: &Optional{&Tuple{[]Type{tvT, tvS}}}}}}
	case "repeating":
		params = []*Param{{Name: "repeating", Type: tvT}, {Name: "count", Type: Int, Default: minusOne}}
	case "from":
		params = []*Param{{Name: "from", Type: Int}, {Name: "by", Type: Int, Default: oneLit}}
		elem = Int
	default:
		c.errorf(e, diag.ArgumentLabel, "Seq's arguments are first and next, state and next, repeating (and count), or from (and by): a collection's sequence is xs.lazy()")
		c.checkArgsLoosely(e)
		call.Result = &Seq{Invalid}
		return
	}
	args := c.matchArgs(e, params, "Seq")
	if first == "state" {
		c.seqState(e, call, args, params, elem)
		return
	}
	s := Subst{}
	if elem != nil {
		s[tvT] = elem
	}
	free := map[*TypeParam]bool{tvT: true, tvS: true}
	result := Type(&Seq{tvT})
	c.inferArgs(args, params, s, free, nil, result, "Seq")
	c.typeDefaults(args, params)
	if mentionsFree(result, free, s) {
		c.errorf(e, diag.CannotInferType, "can't tell what the sequence gives: Seq<T>(…), or say what's expected")
		s[tvT] = Invalid
	}
	ps := make([]*Param, len(params))
	for i, p := range params {
		ps[i] = &Param{Name: p.Name, Type: s.Apply(p.Type), Default: p.Default}
	}
	call.Args = args
	call.Params = ps
	call.Result = s.Apply(result)
	call.Bound = map[string]Type{"T": s[tvT]}
}

// seqState checks Seq(state: s, next: f): the state's type first, then
// f's, whose result says the elements' when nothing else does. A next
// that never gives none (an endless sequence) gives the pair itself, an
// (element, state) where (element, state)? is taken: a tuple is the
// same value either way.
func (c *Checker) seqState(e *syntax.CallExpr, call *Call, args []syntax.Expr, params []*Param, elem Type) {
	st := Type(Invalid)
	if args[0] != nil {
		st = c.expr(args[0], nil)
		if st == NoneType {
			c.errorf(args[0], diag.CannotInferType, "can't tell the state's type from none: say it, (none as T?)")
			st = Invalid
		}
	}
	next := &FuncType{Params: []Type{st}}
	if elem != nil {
		next.Result = &Optional{&Tuple{[]Type{elem, st}}}
		if args[1] != nil {
			c.assign(args[1], next, "next of Seq")
		}
	} else if args[1] != nil {
		saved := c.free
		c.free = map[*TypeParam]bool{tvT: true}
		got := c.expr(args[1], &FuncType{Params: []Type{st}, Result: tvT})
		c.free = saved
		elem = Invalid
		if ft, ok := got.(*FuncType); ok && !IsInvalid(ft.Result) {
			tup, ok := Unopt(ft.Result).(*Tuple)
			if !ok || len(tup.Elems) != 2 || !Identical(tup.Elems[1], st) && !IsInvalid(st) {
				c.errorf(args[1], diag.TypeMismatch, "next gives %s: it gives the next element and the next state, (element, %s), or none at the end", aType(ft.Result), typeString(st))
			} else {
				elem = tup.Elems[0]
			}
		}
		next.Result = &Optional{&Tuple{[]Type{elem, st}}}
	}
	call.Args = args
	call.Params = []*Param{{Name: "state", Type: st}, {Name: "next", Type: next}}
	call.Result = &Seq{elem}
	call.Bound = map[string]Type{"T": elem, "S": st}
}

var (
	minusOne = &syntax.UnaryExpr{Op: syntax.SUB, X: &syntax.BasicLit{Kind: syntax.INT, Value: "1"}}
	oneLit   = &syntax.BasicLit{Kind: syntax.INT, Value: "1"}
)
