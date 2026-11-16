package check

import (
	"sort"
	"strings"

	"github.com/egoist/asuka/lang/syntax"
)

// The built-in types' methods: a table from a receiver and a name to a
// signature and the runtime builtin that implements it (core/src/rt's
// lib.c, by name; the lowering calls it with CALLB, the receiver first).
//
// Signatures are written in Asuka's type syntax over the receiver's
// type variables: T an array's element, an optional's or a task's
// value; K and V a map's key and value; U a type the call infers (map's
// result).

// A Method is a built-in type's method.
type Method struct {
	Recv    string // array, map, string, bytes, float, int, duration, optional, task, app, loading
	Name    string
	Sig     string // (name: Type, …) -> Result
	Builtin string // the runtime's builtin; "" for one the lowering does itself (Special)
	// Special names what the lowering does instead of a call: len (LEN),
	// enumerated, isNone, isSome, mapGet.
	Special string
	// Inout: changes its receiver (the runtime changes the slot it's
	// passed, copying first when it's shared).
	Inout  bool
	Throws bool
	// Fails: may raise a runtime error (as an index out of range does),
	// which needs no try.
	Fails bool
	Async bool
	// Order is the builtin's arguments after the receiver, by parameter
	// index, when it isn't the parameters' order.
	Order []int
	// Extra: arguments the lowering appends: "kindU", U's ak_kind;
	// "true" or "false", that bool (one builtin for two methods).
	Extra string
	// Needs: what the receiver's type variable must be: "Equatable",
	// "Comparable", "number", "string".
	Needs string
	Doc   string

	Params []*Param // parsed from Sig
	Result Type
}

// Methods lists every built-in method, the language's reference for
// them (docs/language.md lists them too).
var Methods = []*Method{
	// Arrays.
	{Recv: "array", Name: "count", Sig: "() -> int", Special: "len", Doc: "the number of elements"},
	{Recv: "array", Name: "count", Sig: "(where: fn(T) -> bool) -> int", Builtin: "arr.count", Doc: "the number of elements the predicate is true of"},
	{Recv: "array", Name: "isEmpty", Sig: "() -> bool", Builtin: "arr.is_empty"},
	{Recv: "array", Name: "first", Sig: "() -> T?", Builtin: "arr.first"},
	{Recv: "array", Name: "last", Sig: "() -> T?", Builtin: "arr.last"},
	{Recv: "array", Name: "get", Sig: "(index: int) -> T?", Builtin: "arr.get", Doc: "the element at index, or none when it's out of range"},
	{Recv: "array", Name: "push", Sig: "(value: T)", Builtin: "arr.push", Inout: true},
	{Recv: "array", Name: "insert", Sig: "(value: T, at: int)", Builtin: "arr.insert", Inout: true, Order: []int{1, 0}},
	{Recv: "array", Name: "removeAt", Sig: "(index: int) -> T", Builtin: "arr.remove_at", Inout: true, Fails: true, Doc: "removes and returns the element at index; out of range is an error, as xs[i] is"},
	{Recv: "array", Name: "removeWhere", Sig: "(where: fn(T) -> bool) -> int", Builtin: "arr.remove_where", Inout: true, Doc: "removes the elements the predicate is true of; how many"},
	{Recv: "array", Name: "pop", Sig: "() -> T?", Builtin: "arr.pop", Inout: true},
	{Recv: "array", Name: "clear", Sig: "()", Builtin: "arr.clear", Inout: true},
	{Recv: "array", Name: "map", Sig: "(transform: fn(T) -> U) -> [U]", Builtin: "arr.map", Extra: "kindU"},
	{Recv: "array", Name: "filter", Sig: "(where: fn(T) -> bool) -> [T]", Builtin: "arr.filter"},
	{Recv: "array", Name: "any", Sig: "(where: fn(T) -> bool) -> bool", Builtin: "arr.any"},
	{Recv: "array", Name: "all", Sig: "(where: fn(T) -> bool) -> bool", Builtin: "arr.all"},
	{Recv: "array", Name: "index", Sig: "(where: fn(T) -> bool) -> int?", Builtin: "arr.index"},
	{Recv: "array", Name: "find", Sig: "(where: fn(T) -> bool) -> T?", Builtin: "arr.find"},
	{Recv: "array", Name: "contains", Sig: "(value: T) -> bool", Builtin: "arr.contains", Needs: "Equatable"},
	{Recv: "array", Name: "sum", Sig: "() -> T", Builtin: "arr.sum", Needs: "number"},
	{Recv: "array", Name: "reversed", Sig: "() -> [T]", Builtin: "arr.reversed"},
	{Recv: "array", Name: "slice", Sig: "(from: int, to: int) -> [T]", Builtin: "arr.slice", Doc: "the elements from index from up to to, clamped"},
	{Recv: "array", Name: "sorted", Sig: "() -> [T]", Builtin: "arr.sorted", Needs: "Comparable"},
	{Recv: "array", Name: "sorted", Sig: "(by: fn(T, T) -> bool) -> [T]", Builtin: "arr.sorted", Doc: "sorted by a less-than: (a, b) => a.age < b.age; stable"},
	{Recv: "array", Name: "sort", Sig: "()", Builtin: "arr.sort", Inout: true, Needs: "Comparable"},
	{Recv: "array", Name: "sort", Sig: "(by: fn(T, T) -> bool)", Builtin: "arr.sort", Inout: true},
	{Recv: "array", Name: "enumerated", Sig: "() -> [(int, T)]", Special: "enumerated", Doc: "the elements with their indexes: for (i, x) in xs.enumerated()"},
	{Recv: "array", Name: "joined", Sig: "(separator: string = \"\") -> string", Builtin: "str.join", Needs: "string"},

	// Maps.
	{Recv: "map", Name: "count", Sig: "() -> int", Special: "len"},
	{Recv: "map", Name: "isEmpty", Sig: "() -> bool", Builtin: "map.is_empty"},
	{Recv: "map", Name: "has", Sig: "(key: K) -> bool", Builtin: "map.has"},
	{Recv: "map", Name: "get", Sig: "(key: K) -> V?", Special: "mapGet"},
	{Recv: "map", Name: "remove", Sig: "(key: K) -> bool", Builtin: "map.delete", Inout: true, Doc: "removes the key; whether it was there"},
	{Recv: "map", Name: "keys", Sig: "() -> [K]", Builtin: "map.keys", Doc: "in insertion order"},
	{Recv: "map", Name: "values", Sig: "() -> [V]", Builtin: "map.values", Doc: "in insertion order"},

	// Strings.
	{Recv: "string", Name: "count", Sig: "() -> int", Builtin: "str.len", Doc: "the number of characters (code points)"},
	{Recv: "string", Name: "byteCount", Sig: "() -> int", Builtin: "str.bytes_len", Doc: "the number of bytes (UTF-8)"},
	{Recv: "string", Name: "isEmpty", Sig: "() -> bool", Builtin: "str.is_empty"},
	{Recv: "string", Name: "upper", Sig: "() -> string", Builtin: "str.upper"},
	{Recv: "string", Name: "lower", Sig: "() -> string", Builtin: "str.lower"},
	{Recv: "string", Name: "trim", Sig: "() -> string", Builtin: "str.trim", Doc: "without the white space around it"},
	{Recv: "string", Name: "trimStart", Sig: "() -> string", Builtin: "str.trim_start", Doc: "without the white space before it"},
	{Recv: "string", Name: "trimEnd", Sig: "() -> string", Builtin: "str.trim_end", Doc: "without the white space after it"},
	{Recv: "string", Name: "trimPrefix", Sig: "(prefix: string) -> string", Builtin: "str.trim_prefix", Doc: "without prefix when it starts with it"},
	{Recv: "string", Name: "trimSuffix", Sig: "(suffix: string) -> string", Builtin: "str.trim_suffix", Doc: "without suffix when it ends with it"},
	{Recv: "string", Name: "contains", Sig: "(text: string) -> bool", Builtin: "str.contains"},
	{Recv: "string", Name: "hasPrefix", Sig: "(prefix: string) -> bool", Builtin: "str.has_prefix"},
	{Recv: "string", Name: "hasSuffix", Sig: "(suffix: string) -> bool", Builtin: "str.has_suffix"},
	{Recv: "string", Name: "replace", Sig: "(old: string, new: string, count: int = -1) -> string", Builtin: "str.replace", Doc: "old replaced with new: the first count of them, every one when count is negative"},
	{Recv: "string", Name: "split", Sig: "(separator: string, count: int = -1) -> [string]", Builtin: "str.split", Doc: "the parts between separators: at most count, the last holding the rest, every one when count is negative"},
	{Recv: "string", Name: "fields", Sig: "() -> [string]", Builtin: "str.fields", Doc: "the words: the runs of text between white space"},
	{Recv: "string", Name: "lines", Sig: "() -> [string]", Builtin: "str.lines", Doc: "the lines, without their line breaks (\\n or \\r\\n)"},
	{Recv: "string", Name: "find", Sig: "(text: string) -> int?", Builtin: "str.find", Doc: "the byte offset of the first text, or none"},
	{Recv: "string", Name: "findLast", Sig: "(text: string) -> int?", Builtin: "str.find_last", Doc: "the byte offset of the last text, or none"},
	{Recv: "string", Name: "findAny", Sig: "(chars: string) -> int?", Builtin: "str.find_any", Doc: "the byte offset of the first character that's one of chars, or none"},
	{Recv: "string", Name: "slice", Sig: "(from: int, to: int) -> string", Builtin: "str.slice", Doc: "the characters from from up to to, clamped"},
	{Recv: "string", Name: "byteSlice", Sig: "(from: int, to: int) -> string", Builtin: "str.byte_slice", Doc: "the bytes from from up to to (the offsets find gives), clamped"},
	{Recv: "string", Name: "padStart", Sig: "(width: int, with: string = \" \") -> string", Builtin: "str.pad", Extra: "false", Doc: "with copies of with before it, up to width characters: \"7\".padStart(2, with: \"0\") is 07"},
	{Recv: "string", Name: "padEnd", Sig: "(width: int, with: string = \" \") -> string", Builtin: "str.pad", Extra: "true", Doc: "with copies of with after it, up to width characters"},
	{Recv: "string", Name: "toInt", Sig: "() -> int?", Builtin: "str.to_int"},
	{Recv: "string", Name: "toFloat", Sig: "() -> float?", Builtin: "str.to_float"},
	{Recv: "string", Name: "repeat", Sig: "(count: int) -> string", Builtin: "str.repeat"},
	{Recv: "string", Name: "chars", Sig: "() -> [string]", Builtin: "str.chars", Doc: "its characters, each a string"},
	{Recv: "string", Name: "bytes", Sig: "() -> bytes", Builtin: "str.to_bytes", Doc: "its UTF-8 bytes"},

	// Colors.
	{Recv: "color", Name: "opacity", Sig: "(alpha: float) -> Color", Builtin: "color.opacity", Doc: "the color drawn at alpha (0 to 1) times its own opacity; a semantic color stays one, so it still follows dark mode"},

	// Bytes.
	{Recv: "bytes", Name: "count", Sig: "() -> int", Builtin: "bytes.len", Doc: "the number of bytes"},
	{Recv: "bytes", Name: "isEmpty", Sig: "() -> bool", Builtin: "bytes.is_empty"},
	{Recv: "bytes", Name: "get", Sig: "(index: int) -> int?", Builtin: "bytes.get", Doc: "the byte at index (0 to 255), or none when it's out of range"},
	{Recv: "bytes", Name: "slice", Sig: "(from: int, to: int) -> bytes", Builtin: "bytes.slice", Doc: "the bytes from from up to to, clamped"},
	{Recv: "bytes", Name: "concat", Sig: "(other: bytes) -> bytes", Builtin: "bytes.concat", Doc: "these bytes, then other's"},
	{Recv: "bytes", Name: "string", Sig: "() -> string", Builtin: "bytes.str", Doc: "the bytes read as UTF-8 text; what isn't UTF-8 becomes U+FFFD"},

	// Numbers.
	{Recv: "float", Name: "fixed", Sig: "(digits: int) -> string", Builtin: "str.fixed", Doc: "with that many digits after the point: 3.14159.fixed(2) is 3.14"},
	{Recv: "float", Name: "rounded", Sig: "() -> float", Builtin: "math.round"},
	{Recv: "float", Name: "floor", Sig: "() -> float", Builtin: "math.floor"},
	{Recv: "float", Name: "ceil", Sig: "() -> float", Builtin: "math.ceil"},

	// Durations.
	{Recv: "duration", Name: "nanoseconds", Sig: "() -> int", Builtin: "dur.ns"},
	{Recv: "duration", Name: "milliseconds", Sig: "() -> int", Builtin: "dur.ms", Doc: "whole milliseconds, toward zero"},
	{Recv: "duration", Name: "seconds", Sig: "() -> float", Builtin: "dur.seconds"},
	{Recv: "duration", Name: "minutes", Sig: "() -> float", Builtin: "dur.minutes"},
	{Recv: "duration", Name: "hours", Sig: "() -> float", Builtin: "dur.hours"},
	{Recv: "duration", Name: "rounded", Sig: "(to: Duration) -> Duration", Builtin: "dur.rounded", Doc: "to the nearest multiple of to, halves away from zero: (90s).rounded(to: 1min) is 2min"},
	{Recv: "duration", Name: "truncated", Sig: "(to: Duration) -> Duration", Builtin: "dur.truncated", Doc: "to a multiple of to, toward zero"},

	// Optionals.
	{Recv: "optional", Name: "isNone", Sig: "() -> bool", Special: "isNone"},
	{Recv: "optional", Name: "isSome", Sig: "() -> bool", Special: "isSome"},

	// What mount returns, in tests.
	{Recv: "app", Name: "tap", Sig: "(text: string)", Builtin: "test.tap", Doc: "taps the control showing text, and waits for what it does"},
	{Recv: "app", Name: "type", Sig: "(text: string, into: string)", Builtin: "test.type", Doc: "types text into the field labelled or showing into"},
	{Recv: "app", Name: "expect", Sig: "(text: string)", Builtin: "test.expect", Doc: "fails the test unless text is shown"},
	{Recv: "app", Name: "expectNo", Sig: "(text: string)", Builtin: "test.expect_no", Doc: "fails the test if text is shown"},
	{Recv: "app", Name: "idle", Sig: "()", Builtin: "test.idle", Async: true, Doc: "waits until nothing is left to run"},

	// Loading<T>.
	{Recv: "loading", Name: "reload", Sig: "()", Builtin: "load.reload"},
}

// The receivers' type variables.
var (
	tvT = &TypeParam{Name: "T", id: -10}
	tvK = &TypeParam{Name: "K", id: -11}
	tvV = &TypeParam{Name: "V", id: -12}
	tvU = &TypeParam{Name: "U", id: -13}
)

var methodIndex = func() map[string][]*Method {
	idx := map[string][]*Method{}
	for _, m := range Methods {
		m.parse()
		idx[m.Recv+"."+m.Name] = append(idx[m.Recv+"."+m.Name], m)
	}
	return idx
}()

func recvKind(t Type) string {
	switch x := t.(type) {
	case *Array:
		return "array"
	case *Map:
		return "map"
	case *Optional:
		return "optional"
	case *Task:
		return "task"
	case *Stream:
		return "stream"
	case *Named:
		if x.Obj == loadingT {
			return "loading"
		}
	case *Basic:
		switch x {
		case String:
			return "string"
		case Float:
			return "float"
		case Int:
			return "int"
		case Duration:
			return "duration"
		case TestApp:
			return "app"
		case Bytes:
			return "bytes"
		case Color:
			return "color"
		}
	}
	return ""
}

// lookupMethod finds a built-in method of t named name (any arity).
func lookupMethod(t Type, name string) *Method {
	ms := methodIndex[recvKind(t)+"."+name]
	if len(ms) == 0 {
		return nil
	}
	return ms[0]
}

// lookupMethodArity finds the overload taking n arguments.
func lookupMethodArity(t Type, name string, n int) *Method {
	for _, m := range methodIndex[recvKind(t)+"."+name] {
		req := 0
		for _, p := range m.Params {
			if p.Default == nil {
				req++
			}
		}
		if n >= req && n <= len(m.Params) {
			return m
		}
	}
	return nil
}

// methodNames lists t's built-in methods (for suggestions).
func methodNames(t Type) []string {
	var out []string
	prefix := recvKind(t) + "."
	for k := range methodIndex {
		if strings.HasPrefix(k, prefix) {
			out = append(out, strings.TrimPrefix(k, prefix))
		}
	}
	sort.Strings(out)
	return out
}

func (m *Method) params() []*Param { return m.Params }

// bind maps the receiver's type variables to its types.
func (m *Method) bind(recv Type) Subst {
	s := Subst{}
	switch x := recv.(type) {
	case *Array:
		s[tvT] = x.Elem
	case *Map:
		s[tvK], s[tvV] = x.Key, x.Elem
	case *Optional:
		s[tvT] = x.Elem
	case *Task:
		s[tvT] = x.Elem
	case *Stream:
		s[tvT] = x.Elem
	case *Named:
		if len(x.Args) > 0 {
			s[tvT] = x.Args[0]
		}
	}
	return s
}

// requires reports what the receiver lacks for m, "" when nothing.
func (m *Method) requires(s Subst) string {
	t := s[tvT]
	switch m.Needs {
	case "Equatable":
		if !IsEquatable(t) {
			return "compares elements, and " + typeString(t) + "s can't be compared"
		}
	case "Comparable":
		if t != Int && t != Float && t != String && t != Duration {
			return "() orders ints, floats and strings; order " + typeString(t) + "s with a less-than: " + m.Name + "((a, b) => …)"
		}
	case "number":
		if t != Int && t != Float {
			return "adds numbers, and these are " + typeString(t) + "s"
		}
	case "string":
		if t != String {
			return "joins strings, and these are " + typeString(t) + "s"
		}
	}
	return ""
}

// parse reads m.Sig.
func (m *Method) parse() {
	p := &sigParser{s: m.Sig}
	m.Params = p.params()
	m.Result = Void
	p.space()
	if strings.HasPrefix(p.s[p.i:], "->") {
		p.i += 2
		m.Result = p.typ()
	}
	if p.err {
		panic("check: bad method signature: " + m.Recv + "." + m.Name + " " + m.Sig)
	}
}

// sigParser reads the method table's signatures.
type sigParser struct {
	s   string
	i   int
	err bool
}

func (p *sigParser) space() {
	for p.i < len(p.s) && p.s[p.i] == ' ' {
		p.i++
	}
}

func (p *sigParser) eat(tok string) bool {
	p.space()
	if strings.HasPrefix(p.s[p.i:], tok) {
		p.i += len(tok)
		return true
	}
	return false
}

func (p *sigParser) ident() string {
	p.space()
	j := p.i
	for p.i < len(p.s) && (p.s[p.i] >= 'a' && p.s[p.i] <= 'z' || p.s[p.i] >= 'A' && p.s[p.i] <= 'Z') {
		p.i++
	}
	if j == p.i {
		p.err = true
	}
	return p.s[j:p.i]
}

func (p *sigParser) params() []*Param {
	var out []*Param
	if !p.eat("(") {
		p.err = true
		return nil
	}
	for !p.eat(")") {
		if len(out) > 0 && !p.eat(",") {
			p.err = true
			return out
		}
		name := p.ident()
		if !p.eat(":") {
			p.err = true
			return out
		}
		pa := &Param{Name: name, Type: p.typ()}
		if p.eat("=") {
			p.space()
			j := strings.IndexAny(p.s[p.i:], ",)")
			if j < 0 {
				p.err = true
				return out
			}
			lit := strings.TrimSpace(p.s[p.i : p.i+j])
			p.i += j
			pa.Default = defaultLit(lit)
		}
		out = append(out, pa)
	}
	return out
}

func (p *sigParser) typ() Type {
	var t Type
	switch {
	case p.eat("["):
		k := p.typ()
		if p.eat(":") {
			v := p.typ()
			t = &Map{k, v}
		} else {
			t = &Array{k}
		}
		if !p.eat("]") {
			p.err = true
		}
	case p.eat("("):
		var es []Type
		for !p.eat(")") {
			if len(es) > 0 && !p.eat(",") {
				p.err = true
				return Invalid
			}
			es = append(es, p.typ())
		}
		t = &Tuple{es}
	case p.eat("fn"):
		var ps []Type
		if !p.eat("(") {
			p.err = true
			return Invalid
		}
		for !p.eat(")") {
			if len(ps) > 0 && !p.eat(",") {
				p.err = true
				return Invalid
			}
			ps = append(ps, p.typ())
		}
		r := Type(Void)
		if p.eat("->") {
			r = p.typ()
		}
		t = &FuncType{Params: ps, Result: r}
	default:
		switch n := p.ident(); n {
		case "int":
			t = Int
		case "float":
			t = Float
		case "bool":
			t = Bool
		case "string":
			t = String
		case "bytes":
			t = Bytes
		case "Color":
			t = Color
		case "Duration":
			t = Duration
		case "T":
			t = tvT
		case "K":
			t = tvK
		case "V":
			t = tvV
		case "U":
			t = tvU
		default:
			p.err = true
			t = Invalid
		}
	}
	if p.eat("?") {
		t = MakeOptional(t)
	}
	return t
}

// defaultLit is a default written in a signature: "" or a number.
func defaultLit(lit string) syntax.Expr {
	if strings.HasPrefix(lit, `"`) {
		return &syntax.StringLit{Segments: []string{strings.Trim(lit, `"`)}}
	}
	return &syntax.BasicLit{Kind: syntax.INT, Value: lit}
}
