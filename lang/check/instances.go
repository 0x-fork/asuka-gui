package check

import (
	"fmt"

	"github.com/egoist/asuka/lang/diag"
	"github.com/egoist/asuka/lang/syntax"
)

// A generic is compiled for each list of type arguments it's used with
// (LOWERING.md), so one that uses itself with a bigger one, directly or
// through others, would need one after another without end:
//
//	fn depth<T>(x: T, n: int) -> int = … depth([x], n - 1) …
//
// is depth<int>, then depth<[int]>, then depth<[[int]]>, …. As Go does,
// the checker finds such cycles among the type parameters and says so;
// the compiler would otherwise go on making instances until it runs out.

// An instance is where a generic's type parameters get their arguments:
// a call of a generic fn, a method of a generic type, a generic store
// made.
type instance struct {
	at     syntax.Node // or, for a type's declaration, pos
	pos    syntax.Pos
	name   string
	params []*TypeParam
	args   []Type
	own    []*TypeParam // the type parameters in scope where it is
}

// noteInstance records an instance, its arguments for params.
func (c *Checker) noteInstance(at syntax.Node, name string, params []*TypeParam, args []Type) {
	if len(params) == 0 {
		return
	}
	own := c.enclosingParams()
	if f := c.enclosingFunc(); f != nil && f.Recv != nil {
		own = append(own, f.Recv.TypeParams...)
	}
	if len(own) == 0 {
		return // nothing here is generic: its arguments are fixed
	}
	c.instances = append(c.instances, instance{at: at, name: name, params: params, args: args, own: own})
}

// instanceCycles reports the instances that make a type parameter's
// argument bigger on a way back to it.
func (c *Checker) instanceCycles() {
	type edge struct {
		to    *TypeParam
		grows bool
		inst  *instance
	}
	edges := map[*TypeParam][]edge{}
	all := append(c.typeInstances(), c.instances...)
	for i := range all {
		in := &all[i]
		for j, q := range in.params {
			if j >= len(in.args) {
				break
			}
			a := in.args[j]
			for _, p := range in.own {
				if occurs(p, a) {
					edges[p] = append(edges[p], edge{to: q, grows: a != Type(p), inst: in})
				}
			}
		}
	}
	reaches := func(from, to *TypeParam) bool {
		seen := map[*TypeParam]bool{}
		stack := []*TypeParam{from}
		for len(stack) > 0 {
			p := stack[len(stack)-1]
			stack = stack[:len(stack)-1]
			if p == to {
				return true
			}
			if seen[p] {
				continue
			}
			seen[p] = true
			for _, e := range edges[p] {
				stack = append(stack, e.to)
			}
		}
		return false
	}
	for p, es := range edges {
		for _, e := range es {
			if e.grows && reaches(e.to, p) {
				arg := ""
				for j, q := range e.inst.params {
					if q == e.to && j < len(e.inst.args) {
						arg = typeString(e.inst.args[j])
					}
				}
				msg := fmt.Sprintf("%s's %s is %s here, made of %s, which comes back to here: a generic is compiled for each type it's used with, and this would need them without end (%s, %s, …)",
					e.inst.name, e.to.Name, arg, p.Name, p.Name, arg)
				if e.inst.at != nil {
					c.errorf(e.inst.at, diag.RecursiveType, "%s; pass a value of the same type", msg)
				} else {
					c.errorPos(e.inst.pos, diag.RecursiveType, "%s; a type that holds itself takes the same arguments (Tree<T> in Tree<T>)", msg)
				}
			}
		}
	}
}

// typeInstances are the generic types that a generic struct's fields,
// an enum's payloads and an interface's methods and fields name: G<[T]>
// in struct G<T> would need G<[[T]]>, and so on.
func (c *Checker) typeInstances() []instance {
	var out []instance
	for _, p := range c.pkgs {
		for _, t := range p.Types {
			if len(t.TypeParams) == 0 {
				continue
			}
			add := func(pos syntax.Pos, ft Type) {
				namedIn(ft, func(n *Named) {
					if len(n.Args) > 0 && len(n.Obj.TypeParams) == len(n.Args) {
						out = append(out, instance{pos: pos, name: n.Obj.name, params: n.Obj.TypeParams, args: n.Args, own: t.TypeParams})
					}
				})
			}
			for _, f := range t.Fields {
				add(f.Pos, f.Type)
			}
			for _, k := range t.Cases {
				for _, pt := range k.Params {
					add(k.Pos, pt)
				}
			}
			if t.Iface != nil {
				for _, f := range t.Iface.Fields {
					add(f.Pos, f.Type)
				}
				for _, m := range t.Iface.Methods {
					for _, pa := range m.Sig.Params {
						add(t.pos, pa.Type)
					}
					add(t.pos, m.Sig.Result)
				}
			}
		}
	}
	return out
}

// namedIn calls f with each declared type t names (itself included).
func namedIn(t Type, f func(*Named)) {
	switch x := t.(type) {
	case *Array:
		namedIn(x.Elem, f)
	case *Optional:
		namedIn(x.Elem, f)
	case *Map:
		namedIn(x.Key, f)
		namedIn(x.Elem, f)
	case *Set:
		namedIn(x.Elem, f)
	case *Tuple:
		for _, e := range x.Elems {
			namedIn(e, f)
		}
	case *FuncType:
		for _, e := range x.Params {
			namedIn(e, f)
		}
		namedIn(x.Result, f)
	case *Task:
		namedIn(x.Elem, f)
	case *Stream:
		namedIn(x.Elem, f)
	case *Named:
		f(x)
		for _, a := range x.Args {
			namedIn(a, f)
		}
	}
}

// occurs reports whether p is in t.
func occurs(p *TypeParam, t Type) bool {
	switch x := t.(type) {
	case *TypeParam:
		return x == p
	case *Array:
		return occurs(p, x.Elem)
	case *Optional:
		return occurs(p, x.Elem)
	case *Map:
		return occurs(p, x.Key) || occurs(p, x.Elem)
	case *Set:
		return occurs(p, x.Elem)
	case *Tuple:
		for _, e := range x.Elems {
			if occurs(p, e) {
				return true
			}
		}
	case *FuncType:
		for _, e := range x.Params {
			if occurs(p, e) {
				return true
			}
		}
		return occurs(p, x.Result)
	case *Task:
		return occurs(p, x.Elem)
	case *Stream:
		return occurs(p, x.Elem)
	case *Named:
		for _, a := range x.Args {
			if occurs(p, a) {
				return true
			}
		}
	}
	return false
}
