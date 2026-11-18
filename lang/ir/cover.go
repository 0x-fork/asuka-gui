package ir

import (
	bc "github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/syntax"
)

// Coverage (asuka test --cover): with Options.Cover, each statement of
// the files it names, and each value that ends a block, an arm or a
// closure, counts the times it runs: CALLB cover.hit(i), counter i a
// site of Program.Cover. Without it nothing is emitted: a build that
// doesn't measure coverage pays nothing. Tests' and benches' own bodies
// aren't counted: they're what measures.

// Cover is what a build with coverage counts, and the branches made of
// its counters.
type Cover struct {
	Sites    []CoverSite
	Branches []CoverBranch
}

// CoverSite is a counter's statement: its file and where it starts.
type CoverSite struct {
	File      string
	Line, Col int
}

// CoverBranch is an if's or a match's arms, at its line: an if's then
// and else (one it doesn't write is the arm that does nothing), a
// match's arms in order.
type CoverBranch struct {
	File string
	Line int
	Arms []CoverArm
}

// CoverArm is how many times an arm ran: the sum of the Plus counters'
// counts less the Minus ones'. Known is false when no counter tells (an
// empty block, a view's): such an arm isn't reported.
type CoverArm struct {
	Plus, Minus []int
	Known       bool
}

// Count is how many times a ran, by counts (the counters' counts).
func (a CoverArm) Count(counts []uint64) uint64 {
	var n int64
	for _, i := range a.Plus {
		if i < len(counts) {
			n += int64(counts[i])
		}
	}
	for _, i := range a.Minus {
		if i < len(counts) {
			n -= int64(counts[i])
		}
	}
	if n < 0 {
		return 0
	}
	return uint64(n)
}

type coverer struct {
	files  map[string]bool
	tests  []syntax.Span // tests' and benches' declarations: not counted
	byNode map[syntax.Node]int
	nested map[syntax.Node]bool // else ifs: their arms are their outer if's
	out    *Cover
}

func newCoverer(l *Lowerer, files []string) *coverer {
	c := &coverer{files: map[string]bool{}, byNode: map[syntax.Node]int{}, out: &Cover{}}
	for _, f := range files {
		c.files[f] = true
	}
	for _, p := range l.prog.Packages {
		for _, t := range p.Tests {
			n := t.Node()
			c.tests = append(c.tests, syntax.Span{Start: n.Range().Start, End: n.Range().End})
		}
	}
	return c
}

// site is n's counter, made the first time; -1 when n isn't counted.
func (c *coverer) site(l *Lowerer, n syntax.Node) int {
	if i, ok := c.byNode[n]; ok {
		return i
	}
	start := n.Range().Start
	f := l.fset.File(start)
	if f == nil || !c.files[f.Name()] {
		return -1
	}
	for _, t := range c.tests {
		if start >= t.Start && start < t.End {
			return -1
		}
	}
	p := l.fset.Position(start)
	i := len(c.out.Sites)
	c.out.Sites = append(c.out.Sites, CoverSite{File: f.Name(), Line: p.Line, Col: p.Column})
	c.byNode[n] = i
	return i
}

// cover counts a run of n, a statement or a value that ends a block.
func (b *fb) cover(n syntax.Node) {
	c := b.l.cov
	if c == nil || n == nil {
		return
	}
	if i := c.site(b.l, n); i >= 0 {
		b.callBTo("cover.hit", bc.None, b.konst(int64(i)))
	}
}

// branches finds the instrumented files' ifs and matches, and says each
// arm's count by the counters (once everything is lowered: what wasn't,
// a branch @platform folded, has none and isn't reported).
func (c *coverer) branches(l *Lowerer) {
	c.dead(l)
	for _, p := range l.prog.Packages {
		for _, f := range p.Files {
			if f.File == nil || !c.files[f.File.Name()] {
				continue
			}
			syntax.Inspect(f, func(n syntax.Node) bool {
				switch n := n.(type) {
				case *syntax.IfStmt:
					if c.nested[n] {
						return true // an else if: its own arms are its outer's
					}
					c.ifBranch(l, n, c.count(n))
				case *syntax.IfExpr:
					if c.nested[n] {
						return true
					}
					c.ifBranch(l, n, CoverArm{})
				case *syntax.MatchStmt:
					c.matchBranch(l, n, n.Arms)
				case *syntax.MatchExpr:
					c.matchBranch(l, n, n.Arms)
				}
				return true
			})
		}
	}
}

// count is n's own counter's count, as an arm.
func (c *coverer) count(n syntax.Node) CoverArm {
	if i, ok := c.byNode[n]; ok {
		return CoverArm{Plus: []int{i}, Known: true}
	}
	return CoverArm{}
}

// first is how many times a block was entered: its first statement's.
func (c *coverer) first(b *syntax.Block) CoverArm {
	if b == nil || len(b.Items) == 0 {
		return CoverArm{}
	}
	return c.count(b.Items[0])
}

func (c *coverer) body(n syntax.Node) CoverArm {
	switch n := n.(type) {
	case *syntax.Block:
		return c.first(n)
	case nil:
		return CoverArm{}
	}
	return c.count(n)
}

func minus(a, b CoverArm) CoverArm {
	if !a.Known || !b.Known {
		return CoverArm{}
	}
	return CoverArm{Plus: append(append([]int{}, a.Plus...), b.Minus...), Minus: append(append([]int{}, a.Minus...), b.Plus...), Known: true}
}

func plus(a, b CoverArm) CoverArm {
	if !a.Known || !b.Known {
		return CoverArm{}
	}
	return CoverArm{Plus: append(append([]int{}, a.Plus...), b.Plus...), Minus: append(append([]int{}, a.Minus...), b.Minus...), Known: true}
}

// ifBranch reports an if and its else ifs as one branch: an arm for each
// block, and the else (written or not). evaluated is how many times its
// condition was tested, when known (an if statement's own counter).
func (c *coverer) ifBranch(l *Lowerer, n syntax.Node, evaluated CoverArm) {
	top := n
	var arms []CoverArm
	for n != nil {
		var then *syntax.Block
		var els syntax.Node
		switch x := n.(type) {
		case *syntax.IfStmt:
			then, els = x.Then, x.Else
		case *syntax.IfExpr:
			then, els = x.Then, x.Else
		}
		t := c.first(then)
		arms = append(arms, t)
		switch x := els.(type) {
		case *syntax.IfStmt, *syntax.IfExpr:
			c.nestedIf(x)
			evaluated = minus(evaluated, t)
			n = x
			continue
		case *syntax.Block:
			arms = append(arms, c.first(x))
		default:
			arms = append(arms, minus(evaluated, t))
		}
		n = nil
	}
	c.addAt(l, top, arms)
}

func (c *coverer) nestedIf(n syntax.Node) {
	if c.nested == nil {
		c.nested = map[syntax.Node]bool{}
	}
	c.nested[n] = true
}

func (c *coverer) matchBranch(l *Lowerer, n syntax.Node, arms []*syntax.MatchArm) {
	var out []CoverArm
	for _, a := range arms {
		out = append(out, c.body(a.Body))
	}
	c.addAt(l, n, out)
}

func (c *coverer) addAt(l *Lowerer, n syntax.Node, arms []CoverArm) {
	known := 0
	for _, a := range arms {
		if a.Known {
			known++
		}
	}
	if known == 0 || len(arms) < 2 {
		return
	}
	start := n.Range().Start
	f := l.fset.File(start)
	if f == nil {
		return
	}
	c.out.Branches = append(c.out.Branches, CoverBranch{File: f.Name(), Line: l.fset.Position(start).Line, Arms: arms})
}

// dead counts the statements of the functions nothing calls, which
// weren't lowered (a fn is lowered as it's reached): they never run, so
// their counters, which nothing counts, say 0.
func (c *coverer) dead(l *Lowerer) {
	for _, p := range l.prog.Packages {
		for _, f := range p.Files {
			if f.File == nil || !c.files[f.File.Name()] {
				continue
			}
			syntax.Inspect(f, func(n syntax.Node) bool {
				d, ok := n.(*syntax.FuncDecl)
				if !ok || d.Body == nil && d.Value == nil || returnsView(d) || c.reached(d) {
					return true
				}
				if d.Value != nil {
					c.site(l, d.Value)
				}
				syntax.Inspect(d, func(n syntax.Node) bool {
					switch n := n.(type) {
					case *syntax.Block:
						for _, it := range n.Items {
							c.site(l, it)
						}
					case *syntax.LambdaExpr:
						if e, ok := n.Body.(syntax.Expr); ok {
							c.site(l, e)
						}
					case *syntax.MatchArm:
						if e, ok := n.Body.(syntax.Expr); ok {
							c.site(l, e)
						}
					}
					return true
				})
				return false
			})
		}
	}
}

// reached reports whether any of d's statements has a counter: d was
// lowered.
func (c *coverer) reached(d *syntax.FuncDecl) bool {
	for n := range c.byNode {
		if r := n.Range(); r.Start >= d.Range().Start && r.Start < d.Range().End {
			return true
		}
	}
	return false
}

// returnsView: a fn that builds views, whose body is views, not
// statements.
func returnsView(d *syntax.FuncDecl) bool {
	t, ok := d.Result.(*syntax.NamedType)
	return ok && len(t.Path) == 1 && t.Path[0].Name == "View"
}
