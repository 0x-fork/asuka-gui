package check

import (
	"strings"

	"github.com/egoist/asuka/lang/syntax"
)

// Routes (lang/LOWERING.md's "Routes"): a Router's Routes map addresses
// to screens. A Route's block holds its child Routes, its screen's
// builder (p => View, of its Params), or routed screens named alone: a
// screen declared with at "pattern" is a Route of its pattern whose
// builder reads its parameters from the address. navigate(X(…)) and a
// Button's href: X(…) are X's address, from the one Router that routes
// it.

// RouteBuilder is a lambda the runtime builds views with, passing
// accessors: a Route's screen, p => View, built for each screen of the
// route with its Params; a List's header, key => View (an element key's
// Builder). Its parameters are row items, as a for's.
type RouteBuilder struct {
	Lambda *syntax.LambdaExpr
	Item   syntax.Item   // the block's item the lambda is (a Route's)
	Param  *Var          // p, or nil when the lambda takes none
	Params []*Var        // every parameter's (nil: one it ignores)
	Items  []syntax.Item // its body's view items
}

// ScreenRoute is where a Router routes a routed screen: the pattern of
// its whole address (the Routes' paths around it joined).
type ScreenRoute struct {
	Router  *ElementCall
	Pattern string
	Known   bool // false: a path around it isn't a constant
}

// Href is a routed screen's address made from a call of it:
// navigate(Thread(id: 7)). Args are the call's, per screen parameter
// (nil: its default); Pattern is the screen's whole pattern, set once
// every Router is checked.
type Href struct {
	Screen  *TypeName
	Call    *syntax.CallExpr
	Args    []syntax.Expr
	Pattern string
}

// RoutePattern is a routed screen's pattern as declared (at "…"), and
// whether it's routed.
func RoutePattern(t *TypeName) (string, bool) {
	d, ok := t.Decl.(*syntax.ScreenDecl)
	if !ok || d.At == nil || len(d.At.Interps) > 0 {
		return "", false
	}
	s, err := Unescape(strings.Join(d.At.Segments, ""), d.At.Raw)
	if err != nil {
		return "", false
	}
	return s, true
}

// joinPattern is a route's pattern under its parent's: absolute when it
// starts with /.
func joinPattern(parent, p string) string {
	switch {
	case strings.HasPrefix(p, "/"):
		return p
	case p == "":
		if parent == "" {
			return "/"
		}
		return parent
	}
	return strings.TrimSuffix(parent, "/") + "/" + p
}

// PatternParams are a pattern's parameters, :name and *name, in order.
func PatternParams(p string) []string { return patternParams(p) }

func patternParams(p string) []string {
	var out []string
	for _, seg := range strings.Split(p, "/") {
		if strings.HasPrefix(seg, ":") || strings.HasPrefix(seg, "*") {
			out = append(out, seg[1:])
		}
	}
	return out
}

// routable reports whether a routed screen's parameter of type t can be
// read from an address: an int, a float, a bool, a string, or an enum
// without payloads (by its case's name).
func routable(t Type) bool {
	switch t {
	case Int, Float, Bool, String:
		return true
	}
	if n := AsNamed(t, EnumType); n != nil && len(n.Obj.TypeParams) == 0 {
		for _, k := range n.Obj.Cases {
			if len(k.Params) > 0 {
				return false
			}
		}
		return len(n.Obj.Conforms) == 0
	}
	return false
}

// screenAt checks a routed screen's pattern: its parameters are the
// screen's, and every parameter of the screen is one an address can say.
func (c *Checker) screenAt(t *TypeName) {
	d := t.Decl.(*syntax.ScreenDecl)
	if d.At == nil {
		return
	}
	p, ok := RoutePattern(t)
	if !ok {
		c.errorf(d.At, "a screen's route pattern is a string literal, without {…}: at \":id\"")
		return
	}
	comp := t.Comp
	for _, name := range patternParams(p) {
		if m := comp.Members[name]; m == nil || m.Kind != ParamMember {
			c.errorf(d.At, "the pattern's :%s isn't a parameter of %s: screen %s(%s: int) at …", name, t.name, t.name, name)
		}
	}
	for _, m := range comp.Params {
		if !routable(c.memberType(m)) {
			c.errorf(m.Decl, "%s is routed, and its parameter %s is a %s: an address says ints, floats, bools, strings and enums' cases", t.name, m.name, typeString(m.Type))
		}
	}
}

// isRouteElement reports a Router's or a Route's call.
func isRouteElement(ec *ElementCall) bool {
	el := ec.Comp.Comp.Element
	return el != nil && (el.Name == "Router" || el.Name == "Route")
}

// constString is x's value when it's a string literal without
// interpolations.
func constString(x syntax.Expr) (string, bool) {
	lit, ok := unparen(x).(*syntax.StringLit)
	if !ok || len(lit.Interps) > 0 {
		return "", false
	}
	s := strings.Join(lit.Segments, "")
	if lit.Multiline {
		s = Dedent(s)
	}
	s, err := Unescape(s, lit.Raw)
	return s, err == nil
}

// routeChildren checks a Router's or a Route's block: its Routes, a
// Route's screen builder, the routed screens named in it.
func (c *Checker) routeChildren(children []syntax.Item, ec *ElementCall) {
	savedPath, savedKnown, savedRouter := c.routePath, c.routeKnown, c.router
	defer func() { c.routePath, c.routeKnown, c.router = savedPath, savedKnown, savedRouter }()
	isRoute := ec.Comp.Comp.Element.Name == "Route"
	if !isRoute {
		c.router, c.routePath, c.routeKnown = ec, "", true
	} else {
		p := ""
		if len(ec.Args) > 0 && ec.Args[0] != nil {
			var ok bool
			if p, ok = constString(ec.Args[0]); !ok {
				c.routeKnown = false
			}
		}
		c.routePath = joinPattern(c.routePath, p)
	}
	savedView := c.view
	c.view = true
	defer func() { c.view = savedView }()
	for _, it := range children {
		if es, ok := it.(*syntax.ExprStmt); ok {
			if lam, ok := unparen(es.X).(*syntax.LambdaExpr); ok && isRoute {
				c.routeBuilder(lam, it, ec)
				continue
			}
			if id, ok := es.X.(*syntax.Ident); ok {
				if tn, ok := c.lookup(id.Name).(*TypeName); ok && tn.Comp != nil && tn.Comp.Screen {
					c.info.Uses[id] = tn
					c.routedChild(it, id, tn)
					continue
				}
			}
		}
		c.viewItem(it, ec)
	}
}

// routeBuilder checks a Route's p => View.
func (c *Checker) routeBuilder(lam *syntax.LambdaExpr, it syntax.Item, ec *ElementCall) {
	if c.info.RouteBuilders[ec] != nil {
		c.errorf(lam, "a Route has one screen: p => View")
	}
	rb := &RouteBuilder{Lambda: lam, Item: it}
	pt := c.prelude.Scope.LookupLocal("Params").(*TypeName)
	c.resolveType(pt)
	saved := c.ctx
	ctx := c.enter(CtxBuilder, lam, "route")
	if len(lam.Params) > 1 {
		c.errorf(lam, "a Route's screen takes its Params: p => View")
	}
	for i, p := range lam.Params {
		if p.Type != nil {
			if t := c.typeExpr(p.Type); !IsInvalid(t) && !Identical(t, pt.Type) {
				c.errorf(p.Type, "a Route's screen takes its Params, not a %s", typeString(t))
			}
		}
		if i > 0 {
			continue
		}
		v := c.declareVar(p.Name, pt.Type, false)
		v.Kind = RowItem
		ctx.Params = append(ctx.Params, v)
		rb.Param = v
		rb.Params = []*Var{v}
	}
	switch b := lam.Body.(type) {
	case *syntax.Block:
		rb.Items = b.Items
	case syntax.Expr:
		rb.Items = []syntax.Item{&syntax.ExprStmt{Span: b.Range(), X: b}}
	}
	c.viewItems(rb.Items, nil)
	c.leave(saved)
	c.info.RouteBuilders[ec] = rb
}

// routedChild checks a routed screen named in a Route's block: a Route
// of its pattern.
func (c *Checker) routedChild(it syntax.Item, id *syntax.Ident, tn *TypeName) {
	p, ok := RoutePattern(tn)
	if !ok {
		c.errorf(id, "%s isn't routed: declare it screen %s at \"pattern\", or build it in a Route: Route(\"…\") { _ => %s() }", tn.name, tn.name, tn.name)
		return
	}
	c.resolveType(tn)
	c.info.RoutedScreens[it] = tn
	if c.router != nil {
		c.info.ScreenRoutes[tn] = append(c.info.ScreenRoutes[tn], &ScreenRoute{Router: c.router, Pattern: joinPattern(c.routePath, p), Known: c.routeKnown})
	}
}

// routedScreen is the routed screen x calls (Thread(id: 7)), or nil.
func (c *Checker) routedScreen(x syntax.Expr) (*syntax.CallExpr, *TypeName) {
	call, ok := unparen(x).(*syntax.CallExpr)
	if !ok {
		return nil, nil
	}
	var tn *TypeName
	switch f := call.Fun.(type) {
	case *syntax.Ident:
		tn, _ = c.lookup(f.Name).(*TypeName)
	case *syntax.MemberExpr:
		if id, ok := f.X.(*syntax.Ident); ok {
			if pk, ok := c.lookup(id.Name).(*PkgName); ok && pk.Pkg != nil {
				tn, _ = pk.Pkg.Scope.LookupLocal(f.Name.Name).(*TypeName)
			}
		}
	}
	if tn == nil || tn.Comp == nil || !tn.Comp.Screen {
		return nil, nil
	}
	if _, ok := RoutePattern(tn); !ok {
		return nil, nil
	}
	return call, tn
}

// hrefArg checks an href: a string, or a routed screen's call, its
// address.
func (c *Checker) hrefArg(x syntax.Expr, what string) {
	call, tn := c.routedScreen(x)
	if call == nil {
		c.assign(x, String, what)
		return
	}
	c.resolveType(tn)
	if call.Block != nil {
		c.errorf(call.Block, "an href is a screen's address: %s(…) takes no block here", tn.name)
	}
	var params []*Param
	for _, m := range tn.Comp.Params {
		params = append(params, &Param{Name: m.name, Type: c.memberType(m), Default: m.Default})
	}
	var list []*syntax.Arg
	if call.Args != nil {
		list = call.Args.List
	}
	args := c.matchArgList(call, list, params, tn.name)
	for i, a := range args {
		if a != nil {
			c.assign(a, params[i].Type, "argument "+params[i].Name+" of "+tn.name)
		}
	}
	h := &Href{Screen: tn, Call: call, Args: args}
	c.info.Hrefs[call] = h
	c.hrefs = append(c.hrefs, h)
	c.record(call, String)
	if x != syntax.Expr(call) {
		c.record(x, String)
	}
}

// routeHrefs gives each href its screen's whole pattern, once every
// Router is checked: a screen that one Router routes.
func (c *Checker) routeHrefs() {
	for _, h := range c.hrefs {
		rs := c.info.ScreenRoutes[h.Screen]
		switch {
		case len(rs) == 0:
			c.errorf(h.Call, "no Router routes %s: name it in a Route's block, Route(\"…\") { %s }", h.Screen.name, h.Screen.name)
			continue
		case len(rs) > 1:
			c.errorf(h.Call, "%d Routes route %s: its address is ambiguous; navigate to an href", len(rs), h.Screen.name)
			continue
		case !rs[0].Known:
			c.errorf(h.Call, "a path of a Route around %s isn't a constant: navigate to an href", h.Screen.name)
			continue
		}
		h.Pattern = rs[0].Pattern
		for i, m := range h.Screen.Comp.Params {
			in := false
			for _, n := range patternParams(h.Pattern) {
				in = in || n == m.name
			}
			if !in && i < len(h.Args) && h.Args[i] == nil && m.Default == nil {
				c.errorf(h.Call, "%s needs %s for its address", h.Screen.name, m.name)
			}
			if in && i < len(h.Args) && h.Args[i] == nil {
				c.errorf(h.Call, "%s needs %s: its address's path has :%s", h.Screen.name, m.name, m.name)
			}
		}
	}
}

// builderLambda checks a lambda passed to an element's Builder key: its
// parameters, of the key's fn type, read as row items (accessors the
// runtime passes); its body, views.
func (c *Checker) builderLambda(lam *syntax.LambdaExpr, ft *FuncType, what string) {
	rb := &RouteBuilder{Lambda: lam}
	saved := c.ctx
	ctx := c.enter(CtxBuilder, lam, what)
	if len(lam.Params) != len(ft.Params) {
		c.errorf(lam, "%s takes %s", what, plural(len(ft.Params), "parameter", "parameters"))
	}
	for i, p := range lam.Params {
		if i >= len(ft.Params) {
			break
		}
		if p.Type != nil {
			if t := c.typeExpr(p.Type); !IsInvalid(t) && !Identical(t, ft.Params[i]) {
				c.errorf(p.Type, "%s's parameter is a %s, not a %s", what, typeString(ft.Params[i]), typeString(t))
			}
		}
		v := c.declareVar(p.Name, ft.Params[i], false)
		v.Kind = RowItem
		ctx.Params = append(ctx.Params, v)
		rb.Params = append(rb.Params, v)
	}
	for len(rb.Params) < len(ft.Params) {
		rb.Params = append(rb.Params, nil)
	}
	switch b := lam.Body.(type) {
	case *syntax.Block:
		rb.Items = b.Items
	case syntax.Expr:
		rb.Items = []syntax.Item{&syntax.ExprStmt{Span: b.Range(), X: b}}
	}
	savedView := c.view
	c.view = true
	c.viewItems(rb.Items, nil)
	c.view = savedView
	c.leave(saved)
	c.info.BuilderLambdas[lam] = rb
	c.record(lam, ft)
}
