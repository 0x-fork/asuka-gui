package check

import (
	"github.com/egoist/asuka/lang/prelude"
	"github.com/egoist/asuka/lang/syntax"
)

// componentMembers declares a component's, a screen's, a store's or an
// element's header parameters and members.
func (c *Checker) componentMembers(t *TypeName, params *syntax.ParamList, body *syntax.Block) {
	comp := t.Comp
	comp.Members = map[string]*Member{}
	kind := CtxComponent
	if comp.Store {
		kind = CtxStore
	}
	ctx := &FuncCtx{Kind: kind, Node: t.Decl, Name: t.name, Comp: t, Result: View, Async: false}
	if comp.Store {
		ctx.Result = t.Type
	}
	ctx.scope = NewScope(c.scope)
	comp.Ctx = ctx
	c.info.Ctxs[t.Decl] = ctx
	add := func(id *syntax.Ident, m *Member) {
		m.object = object{id.Name, id.Start}
		m.Owner = t
		c.info.Defs[id] = m
		if old := comp.Members[id.Name]; old != nil {
			c.errorf(id, "%s is declared twice in %s", id.Name, t.name)
			return
		}
		comp.Members[id.Name] = m
		comp.Order = append(comp.Order, m)
		ctx.scope.Insert(m)
	}
	savedScope := c.scope
	c.scope = ctx.scope
	defer func() { c.scope = savedScope }()
	if params != nil {
		for _, p := range params.List {
			if p.Type == nil {
				c.errorf(p.Name, "parameter %s needs a type: %s: Type", p.Name.Name, p.Name.Name)
			}
			m := &Member{Kind: ParamMember, Type: c.typeExpr(p.Type), Default: p.Default, Decl: p, Pub: true}
			add(p.Name, m)
			comp.Params = append(comp.Params, m)
		}
	}
	if body == nil {
		return
	}
	for _, it := range body.Items {
		switch d := it.(type) {
		case *syntax.PropDecl:
			if comp.Store {
				c.errorf(d, "a store has no props: give it parameters, store %s(name: T)", t.name)
				continue
			}
			m := &Member{Kind: PropMember, Default: d.Default, Decl: d, Pub: true}
			if d.Type != nil {
				m.Type = c.typeExpr(d.Type)
			} else if d.Default == nil {
				c.errorf(d.Name, "prop %s needs a type or a default", d.Name.Name)
				m.Type = Invalid
			}
			add(d.Name, m)
			comp.Props = append(comp.Props, m)
		case *syntax.EventDecl:
			if comp.Store {
				c.errorf(d, "a store has no events")
				continue
			}
			m := &Member{Kind: EventMember, Type: Void, Decl: d, Pub: true}
			if d.Params != nil {
				for _, p := range d.Params.List {
					m.Params = append(m.Params, &Param{Name: p.Name.Name, Type: c.typeExpr(p.Type)})
				}
			}
			add(d.Name, m)
			comp.Events = append(comp.Events, m)
		case *syntax.StateDecl:
			m := &Member{Kind: StateMember, Default: d.Value, Decl: d, Pub: d.Pub.IsValid()}
			if d.Type != nil {
				m.Type = c.typeExpr(d.Type)
			}
			add(d.Name, m)
		case *syntax.LetDecl:
			bp, ok := d.Pattern.(*syntax.BindPat)
			if !ok {
				c.errorf(d.Pattern, "a component's let names one value: let name = …")
				continue
			}
			if d.Keyword == syntax.VAR {
				c.errorf(d, "a component's body is declarations: a value that changes is state (state %s = …)", bp.Name.Name)
			}
			m := &Member{Kind: LetMember, Default: d.Value, Decl: d, Pub: d.Pub.IsValid()}
			if d.Type != nil {
				m.Type = c.typeExpr(d.Type)
			}
			add(bp.Name, m)
		case *syntax.UseDecl:
			m := &Member{Kind: UseMember, Type: c.typeExpr(d.Type), Decl: d}
			if n, ok := m.Type.(*Named); !ok || n.Obj.Kind != StoreType {
				if !IsInvalid(m.Type) {
					c.errorf(d.Type, "use takes a store's type, provided by a component above: %s isn't one", typeString(m.Type))
				}
			}
			add(d.Name, m)
		case *syntax.FuncDecl:
			f := &Function{object: object{d.Name.Name, d.Name.Start}, Pkg: t.Pkg, Decl: d, Recv: t}
			m := &Member{Kind: FnMember, Decl: d, Fn: f, Pub: d.Pub.IsValid()}
			add(d.Name, m)
			if t.Methods == nil {
				t.Methods = map[string]*Function{}
			}
			t.Methods[f.name] = f
			c.funcSig(f)
			m.Type = f.Sig.Type()
		case *syntax.ProvideDecl:
			comp.Provides = append(comp.Provides, d)
		}
	}
	// A native component's (an element's) View prop: its children.
}

// memberType is a member's type, inferring it from its value when it has
// no type written.
func (c *Checker) memberType(m *Member) Type {
	if m.Type != nil {
		return m.Type
	}
	if c.resolving[m] {
		c.errorf(m.Decl, "%s's value is defined in terms of itself", m.name)
		m.Type = Invalid
		return m.Type
	}
	c.resolving[m] = true
	defer delete(c.resolving, m)
	c.memberValue(m)
	if m.Type == nil {
		m.Type = Invalid
	}
	return m.Type
}

// memberValue checks a member's value (a state's initial value, a let's
// binding, a prop's default) and infers its type.
func (c *Checker) memberValue(m *Member) {
	if m.Default == nil || c.info.Types[m.Default] != nil && m.Type != nil {
		return
	}
	comp := m.Owner.Comp
	savedScope, savedCtx, savedPkg := c.scope, c.ctx, c.pkg
	savedTry, savedAwait, savedHandled, savedView, savedLoad := c.inTry, c.inAwait, c.handled, c.view, c.loadOK
	c.inTry, c.inAwait, c.handled, c.view = 0, 0, 0, false
	c.pkg = m.Owner.Pkg
	c.scope = comp.Ctx.scope
	c.ctx = comp.Ctx
	kind := CtxComponent
	switch m.Kind {
	case LetMember:
		kind = CtxBinding
		c.loadOK = true
	case PropMember, ParamMember:
		kind = CtxBinding
	}
	if kind == CtxBinding {
		c.enter(kind, m.Default, m.name)
	}
	var t Type
	if m.Type != nil {
		c.assign(m.Default, m.Type, "the value of "+m.name)
		t = m.Type
	} else {
		t = c.valueType(c.expr(m.Default, nil), m.Default)
	}
	if kind == CtxBinding {
		c.leave(comp.Ctx)
	}
	c.scope, c.ctx, c.pkg = savedScope, savedCtx, savedPkg
	c.inTry, c.inAwait, c.handled, c.view, c.loadOK = savedTry, savedAwait, savedHandled, savedView, savedLoad
	if m.Type == nil {
		m.Type = t
	}
}

// componentBody checks a component's, a screen's or a store's body:
// its members' values, then its effects, tasks and views.
func (c *Checker) componentBody(t *TypeName) {
	comp := t.Comp
	if comp.Screen && prelude.FindElement("Screen") == nil {
		c.errorf(t.Decl, "screen %s needs the Screen element, which the runtime doesn't have yet: make it a component", t.name)
	}
	for _, m := range comp.Order {
		switch m.Kind {
		case StateMember, LetMember, PropMember, ParamMember:
			if m.Default != nil {
				c.memberValue(m)
			}
		}
	}
	savedScope, savedCtx := c.scope, c.ctx
	c.scope, c.ctx = comp.Ctx.scope, comp.Ctx
	defer func() { c.scope, c.ctx = savedScope, savedCtx }()
	for _, it := range comp.Body.Items {
		switch d := it.(type) {
		case *syntax.ProvideDecl:
			pt := c.expr(d.Value, nil)
			if n, ok := pt.(*Named); !ok || n.Obj.Kind != StoreType {
				if !IsInvalid(pt) {
					c.errorf(d.Value, "provide takes a store, for use in the components below: this is a %s", typeString(pt))
				}
			}
		case *syntax.TaskDecl:
			c.taskDecl(d)
		case *syntax.EffectDecl:
			saved := c.ctx
			ctx := c.enter(CtxEffect, d, t.name+".effect")
			ctx.Throws = true
			c.block(d.Body)
			c.leave(saved)
		}
	}
	if comp.Store {
		for _, it := range comp.Body.Items {
			switch it.(type) {
			case *syntax.PropDecl, *syntax.EventDecl, *syntax.StateDecl, *syntax.LetDecl, *syntax.UseDecl,
				*syntax.FuncDecl, *syntax.ProvideDecl, *syntax.TaskDecl, *syntax.EffectDecl, *syntax.BadItem:
			default:
				c.errorf(it, "a store has no views or statements: it's state, lets, fns, tasks and effects")
			}
		}
		return
	}
	var views []syntax.Item
	for _, it := range comp.Body.Items {
		switch it.(type) {
		case *syntax.PropDecl, *syntax.EventDecl, *syntax.StateDecl, *syntax.LetDecl, *syntax.UseDecl,
			*syntax.FuncDecl, *syntax.ProvideDecl, *syntax.TaskDecl, *syntax.EffectDecl, *syntax.BadItem:
		default:
			views = append(views, it)
		}
	}
	c.viewBlock(views, nil, t.name)
}

func (c *Checker) taskDecl(d *syntax.TaskDecl) {
	saved := c.ctx
	if len(d.Deps) > 0 {
		c.enter(CtxBinding, d, "task deps")
		for _, x := range d.Deps {
			c.expr(x, nil)
		}
		c.leave(saved)
	}
	ctx := c.enter(CtxTask, d.Body, "task")
	ctx.Async, ctx.Throws = true, true
	c.block(d.Body)
	c.leave(saved)
}

// viewBlock checks the view items of a component's body or app: what it
// shows.
func (c *Checker) viewBlock(items []syntax.Item, el *ElementCall, what string) {
	savedView := c.view
	c.view = true
	c.viewItems(items, el)
	c.view = savedView
}

// viewItems checks items in a view: elements, control flow, View values.
// el is the element whose block they're in, nil in a body.
func (c *Checker) viewItems(items []syntax.Item, el *ElementCall) {
	for _, it := range items {
		c.viewItem(it, el)
	}
}

func (c *Checker) viewItem(it syntax.Item, el *ElementCall) {
	switch s := it.(type) {
	case *syntax.IfStmt:
		saved := c.ctx
		c.openScope()
		if l, ok := s.Cond.(*syntax.LetCond); ok {
			c.enter(CtxBinding, s.Cond, "condition")
			lt := c.expr(l.Value, nil)
			c.leave(saved)
			c.info.Types[s.Cond] = lt
			c.enter(CtxBuilder, s.Then, "then")
			c.letCondPattern(l, lt, ViewMemo)
		} else {
			c.enter(CtxBinding, s.Cond, "condition")
			c.assign(s.Cond, Bool, "the condition")
			c.leave(saved)
			c.enter(CtxBuilder, s.Then, "then")
		}
		c.viewItems(s.Then.Items, nil)
		c.leave(saved)
		c.closeScope()
		switch e := s.Else.(type) {
		case *syntax.IfStmt:
			c.enter(CtxBuilder, e, "else")
			c.viewItem(e, nil)
			c.leave(saved)
		case *syntax.Block:
			c.enter(CtxBuilder, e, "else")
			c.viewItems(e.Items, nil)
			c.leave(saved)
		}
	case *syntax.ForStmt:
		c.forStmt(s, true)
		if el != nil && el.Comp.Comp.Element != nil && el.Comp.Comp.Element.LazyFor {
			c.info.Fors[s].Lazy = true
		}
	case *syntax.MatchStmt:
		saved := c.ctx
		c.enter(CtxBinding, s.Subject, "subject")
		t := c.expr(s.Subject, nil)
		c.leave(saved)
		mi := &MatchInfo{Subject: t, View: true}
		c.info.Matches[s] = mi
		for _, a := range s.Arms {
			c.enter(CtxBuilder, a, "arm")
			c.pattern(a.Pattern, t, ViewMemo)
			if a.Guard != nil {
				c.errorf(a.Guard, "a match in a view takes no guards: put an if in the arm")
			}
			switch b := a.Body.(type) {
			case *syntax.Block:
				c.viewItems(b.Items, nil)
			case syntax.Item:
				c.viewItem(b, nil)
			}
			c.leave(saved)
		}
		// A view may show nothing for the cases it doesn't match.
		mi.Exhaustive, _ = c.exhaustive(t, s.Arms)
	case *syntax.ExprStmt:
		t := c.expr(s.X, View)
		if !IsInvalid(t) && t != View && !(IsOptional(t) && Unopt(t) == View) {
			c.errorf(s, "a %s isn't a view: a body shows elements and components (Text(\"{x}\") shows a value)", typeString(t))
		}
	case *syntax.PropSet, *syntax.OnHandler, *syntax.BindItem, *syntax.StyleUse, *syntax.SlotItem:
		c.errorf(it, "%s goes in an element's block, not here", itemName(it))
	case *syntax.LetDecl, *syntax.AssignStmt, *syntax.WhileStmt, *syntax.DoStmt, *syntax.ReturnStmt,
		*syntax.ThrowStmt, *syntax.BranchStmt, *syntax.DeferStmt, *syntax.EmitStmt:
		c.errorf(it, "%s in a view: a component's body is declarations; put statements in a handler (on press { … }), a fn, a task or an effect", stmtName(it))
	case *syntax.StateDecl, *syntax.PropDecl, *syntax.EventDecl, *syntax.UseDecl, *syntax.ProvideDecl,
		*syntax.TaskDecl, *syntax.EffectDecl, *syntax.FuncDecl:
		c.errorf(it, "%s belongs in a component's body, not in an element's block", itemName(it))
	case *syntax.StyleDecl:
		c.errorf(it, "a style is declared at the top of a file")
	case *syntax.BadItem:
	default:
		c.errorf(it, "this isn't a view")
	}
}

func stmtName(it syntax.Item) string {
	switch it := it.(type) {
	case *syntax.LetDecl:
		if it.Keyword == syntax.VAR {
			return "var"
		}
		return "a let"
	case *syntax.AssignStmt:
		return "an assignment"
	case *syntax.WhileStmt:
		return "while"
	case *syntax.DoStmt:
		return "do"
	case *syntax.ReturnStmt:
		return "return"
	case *syntax.ThrowStmt:
		return "throw"
	case *syntax.BranchStmt:
		return it.Tok.String()
	case *syntax.DeferStmt:
		return "defer"
	case *syntax.EmitStmt:
		return "emit"
	}
	return "a statement"
}

// ---------------------------------------------------------------------
// Components used

// styleMember finds a prop of the Style pseudo-component.
func (c *Checker) styleMember(name string) *Member {
	o, ok := c.prelude.Scope.LookupLocal("Style").(*TypeName)
	if !ok || o.Comp == nil {
		return nil
	}
	if m := o.Comp.Members[name]; m != nil && m.Kind == PropMember {
		return m
	}
	return nil
}

// componentCall checks a component's, a screen's or an element's use:
// header arguments in the parentheses, the rest in the block.
func (c *Checker) componentCall(e *syntax.CallExpr, tn *TypeName) Type {
	comp := tn.Comp
	if comp.Style {
		c.errorf(e.Fun, "Style isn't an element: its props are every element's")
	}
	ec := &ElementCall{Comp: tn, Type: tn.Type.(*Named)}
	c.info.Elements[e] = ec
	c.info.Calls[e] = &Call{Kind: CallComponent, Type: ec.Type, Result: View}
	if c.ctx != nil {
		c.ctx.readsState = true
	}
	// Header arguments.
	var params []*Param
	for _, m := range comp.Params {
		params = append(params, &Param{Name: m.name, Type: m.Type, Default: m.Default})
	}
	var list []*syntax.Arg
	if e.Args != nil {
		for _, a := range e.Args.List {
			if a.Label != nil {
				if m := comp.Members[a.Label.Name]; m != nil && m.Kind != ParamMember || (comp.Native || c.TakesStyle(tn)) && c.styleMember(a.Label.Name) != nil {
					c.errorf(a.Label, "%s is a prop of %s: set it in the block, %s(…) { %s: … }", a.Label.Name, tn.name, tn.name, a.Label.Name)
					c.expr(a.Value, nil)
					continue
				}
			}
			list = append(list, a)
		}
	}
	ec.Args = c.matchArgList(e, list, params, tn.name)
	for i, a := range ec.Args {
		if a != nil {
			c.propValue(a, comp.Params[i], "argument "+params[i].Name+" of "+tn.name)
		}
	}
	if e.Block != nil {
		c.elementBlock(e.Block.Items, ec)
	}
	for _, h := range e.Handlers {
		c.onHandler(h, ec)
	}
	// Required props.
	for _, m := range comp.Props {
		if m.Default != nil || IsOptional(m.Type) || comp.Native {
			continue
		}
		set := false
		for _, p := range ec.Props {
			if p.Member == m {
				set = true
			}
		}
		for _, s := range ec.Slots {
			if s.Prop == m {
				set = true
			}
		}
		if m.name == "children" && len(ec.Children) > 0 {
			set = true
		}
		if !set {
			c.errorf(e, "%s needs %s: set it in the block, %s { %s: … }", tn.name, m.name, tn.name, m.name)
		}
	}
	return View
}

// propValue checks a value passed to a component: a binding (or a
// builder, for a View).
func (c *Checker) propValue(x syntax.Expr, m *Member, what string) {
	saved := c.ctx
	t := c.memberType(m)
	if Identical(Unopt(t), View) {
		c.enter(CtxBuilder, x, "slot")
		savedView := c.view
		c.view = true
		c.assign(x, t, what)
		c.view = savedView
		c.leave(saved)
		return
	}
	c.enter(CtxBinding, x, "binding")
	c.assign(x, t, what)
	c.leave(saved)
}

// elementBlock checks an element's or a component's block.
func (c *Checker) elementBlock(items []syntax.Item, ec *ElementCall) {
	comp := ec.Comp.Comp
	var children []syntax.Item
	for _, it := range items {
		switch s := it.(type) {
		case *syntax.PropSet:
			c.propSet(s.Name, s.Value, ec, false)
		case *syntax.OnHandler:
			c.onHandler(s, ec)
		case *syntax.BindItem:
			c.bindItem(s, ec)
		case *syntax.StyleUse:
			c.styleUse(s, ec)
		case *syntax.SlotItem:
			c.slotItem(s, ec)
		case *syntax.IfStmt, *syntax.ForStmt, *syntax.MatchStmt, *syntax.ExprStmt:
			children = append(children, it)
		default:
			c.viewItem(it, ec)
		}
	}
	ec.Children = children
	if len(children) == 0 {
		return
	}
	cm := comp.Members["children"]
	if cm == nil || cm.Kind != PropMember {
		c.errorf(children[0], "%s takes no children%s", ec.Comp.name, childHint(ec.Comp))
		return
	}
	if comp.Native {
		// An element's children are built in place.
		savedView := c.view
		c.view = true
		c.viewItems(children, ec)
		c.view = savedView
		return
	}
	// A component's: one builder of them all, its children prop.
	saved := c.ctx
	c.enter(CtxBuilder, children[0], "children")
	savedView := c.view
	c.view = true
	c.viewItems(children, ec)
	c.view = savedView
	c.leave(saved)
}

func childHint(t *TypeName) string {
	if t.Comp.Native {
		return ""
	}
	return ": declare prop children: View for them"
}

// propSet checks name: value in a block.
func (c *Checker) propSet(name *syntax.Ident, value syntax.Expr, ec *ElementCall, style bool) {
	comp := ec.Comp.Comp
	m := comp.Members[name.Name]
	if m == nil && (comp.Native && !(comp.Element != nil && comp.Element.NoStyle) || c.TakesStyle(ec.Comp)) {
		m = c.styleMember(name.Name)
	}
	if m == nil {
		c.errorf(name, "%s has no prop %s%s", ec.Comp.name, name.Name, c.propSuggest(comp, name.Name))
		c.expr(value, nil)
		return
	}
	switch m.Kind {
	case ParamMember:
		c.errorf(name, "%s is a parameter of %s: pass it in the parentheses, %s(%s: …)", name.Name, ec.Comp.name, ec.Comp.name, name.Name)
	case EventMember:
		c.errorf(name, "%s is an event: handle it with on %s { … }", name.Name, name.Name)
	case PropMember:
		if name.Name == "children" {
			c.errorf(name, "children are the block's views: put them in the block")
		}
	default:
		c.errorf(name, "%s isn't a prop of %s", name.Name, ec.Comp.name)
	}
	if m.Kind != PropMember {
		c.expr(value, nil)
		return
	}
	c.propValue(value, m, name.Name)
	ec.Props = append(ec.Props, &PropArg{Member: m, Value: value, Style: style})
}

func (c *Checker) propSuggest(comp *Component, name string) string {
	best, bestD := "", 3
	try := func(n string) {
		if d := editDistance(name, n); d < bestD && d < len(name) {
			best, bestD = n, d
		}
	}
	for n := range comp.Members {
		try(n)
	}
	if comp.Native || comp.styled > 0 {
		for _, k := range prelude.Style {
			try(k.Name)
		}
	}
	if best != "" {
		return " (did you mean " + best + "?)"
	}
	return ""
}

func (c *Checker) onHandler(h *syntax.OnHandler, ec *ElementCall) {
	comp := ec.Comp.Comp
	m := comp.Members[h.Name.Name]
	saved := c.ctx
	ctx := c.enter(CtxHandler, h, ec.Comp.name+".on "+h.Name.Name)
	ctx.Async, ctx.Throws = true, true
	savedView, savedLoops, savedTry, savedAwait, savedHandled := c.view, c.loops, c.inTry, c.inAwait, c.handled
	c.view, c.loops, c.inTry, c.inAwait, c.handled = false, 0, 0, 0, 0
	if m == nil || m.Kind != EventMember {
		c.errorf(h.Name, "%s has no event %s%s", ec.Comp.name, h.Name.Name, c.eventSuggest(comp, h.Name.Name))
		for _, p := range h.Params {
			c.declareVar(p, Invalid, false)
		}
	} else {
		if len(h.Params) > len(m.Params) {
			c.errorf(h, "event %s gives %s", h.Name.Name, plural(len(m.Params), "value", "values"))
		}
		var vars []*Var
		for i, p := range h.Params {
			t := Type(Invalid)
			if i < len(m.Params) {
				t = m.Params[i].Type
			}
			v := c.declareVar(p, t, false)
			ctx.Params = append(ctx.Params, v)
			vars = append(vars, v)
		}
		c.info.Binds[h] = vars
		ec.Handlers = append(ec.Handlers, &HandlerArg{Event: m, Handler: h})
	}
	c.stmts(h.Body.Items)
	c.view, c.loops, c.inTry, c.inAwait, c.handled = savedView, savedLoops, savedTry, savedAwait, savedHandled
	c.leave(saved)
}

func (c *Checker) eventSuggest(comp *Component, name string) string {
	var events []string
	for _, m := range comp.Order {
		if m.Kind == EventMember {
			events = append(events, m.name)
		}
	}
	if len(events) == 0 {
		return " (it has no events)"
	}
	for _, e := range events {
		if editDistance(e, name) <= 2 {
			return " (did you mean " + e + "?)"
		}
	}
	s := ""
	for i, e := range events {
		if i > 0 {
			s += ", "
		}
		s += e
	}
	return " (its events: " + s + ")"
}

// nativeBindEvent is the event a native component reports its prop m
// with, <name>Change(value: m's type), or nil.
func nativeBindEvent(comp *Component, m *Member) *Member {
	ev := comp.Members[m.name+"Change"]
	if ev == nil || ev.Kind != EventMember || len(ev.Params) != 1 || !Identical(ev.Params[0].Type, m.Type) {
		return nil
	}
	return ev
}

// bindItem checks bind name: target: a control's two-way binding.
func (c *Checker) bindItem(b *syntax.BindItem, ec *ElementCall) {
	comp := ec.Comp.Comp
	m := comp.Members[b.Name.Name]
	var key *prelude.Key
	if comp.Element != nil {
		for i := range comp.Element.Keys {
			if comp.Element.Keys[i].Name == b.Name.Name {
				key = &comp.Element.Keys[i]
			}
		}
	}
	var ev *Member
	switch {
	case comp.Element == nil && comp.Native && m != nil && m.Kind == PropMember:
		// A package's native component binds a prop it reports with an
		// event <prop>Change(value) (docs/native.md).
		ev = nativeBindEvent(comp, m)
		if ev == nil {
			c.errorf(b.Name, "%s can't bind %s: it binds a prop it reports with event %sChange(%s: %s)", ec.Comp.name, b.Name.Name, b.Name.Name, b.Name.Name, typeString(m.Type))
			c.expr(b.Target, nil)
			return
		}
	case m == nil || key == nil || !key.Bind:
		c.errorf(b.Name, "%s can't bind %s: only a control's own value binds (TextField's text, Toggle's on)", ec.Comp.name, b.Name.Name)
		c.expr(b.Target, nil)
		return
	default:
		ev = comp.Members[key.Event]
	}
	saved := c.ctx
	c.enter(CtxBinding, b, "bind "+b.Name.Name)
	t := c.expr(b.Target, nil)
	c.leave(saved)
	ctx := c.enter(CtxHandler, b.Target, ec.Comp.name+".bind "+b.Name.Name)
	ctx.Throws = true
	savedView := c.view
	c.view = false
	v := &Var{object: object{"_value", b.Start}, Type: m.Type, Ctx: ctx}
	ctx.Params = append(ctx.Params, v)
	c.lvalue(b.Target, "bind")
	c.view = savedView
	c.leave(saved)
	if !IsInvalid(t) && !Identical(t, m.Type) {
		c.errorf(b.Target, "bind %s takes a %s, and this is a %s", b.Name.Name, typeString(m.Type), typeString(t))
	}
	ec.Binds = append(ec.Binds, &BindArg{Prop: m, Event: ev, Target: b.Target, Item: b})
}

// styleUse expands style name: its props, set where it's applied.
func (c *Checker) styleUse(s *syntax.StyleUse, ec *ElementCall) {
	id, ok := s.Name.(*syntax.Ident)
	if !ok {
		c.errorf(s.Name, "styles of other packages aren't supported yet")
		return
	}
	d := c.styles[id.Name]
	if d == nil {
		c.errorf(id, "unknown style %s", id.Name)
		return
	}
	savedScope := c.scope
	c.scope = c.declScope(d)
	for _, it := range d.Body.Items {
		ps, ok := it.(*syntax.PropSet)
		if !ok {
			c.errorf(it, "a style holds props: name: value")
			continue
		}
		c.propSet(ps.Name, ps.Value, ec, true)
	}
	c.scope = savedScope
}

// slotItem checks name { … }: views for a slot.
func (c *Checker) slotItem(s *syntax.SlotItem, ec *ElementCall) {
	comp := ec.Comp.Comp
	m := comp.Members[s.Name.Name]
	if m == nil || !m.IsSlot() {
		c.errorf(s.Name, "%s has no slot %s (a slot is a prop of type View)", ec.Comp.name, s.Name.Name)
		return
	}
	saved := c.ctx
	c.enter(CtxBuilder, s.Body, s.Name.Name)
	savedView := c.view
	c.view = true
	c.viewItems(s.Body.Items, nil)
	c.view = savedView
	c.leave(saved)
	ec.Slots = append(ec.Slots, &SlotArg{Prop: m, Block: s.Body, Item: s})
}

// TakesStyle says whether a component takes the style props: when what
// it shows is one element (a built-in one that takes them, a package's
// native component, or a component that takes them), the style its
// caller gives goes on that element, over the element's own.
func (c *Checker) TakesStyle(t *TypeName) bool {
	comp := t.Comp
	if comp == nil {
		return false
	}
	if comp.Native {
		return !comp.Style && !(comp.Element != nil && comp.Element.NoStyle)
	}
	if comp.styled == 0 {
		comp.styled = -1 // while it's worked out: a cycle doesn't
		if r := c.rootComponent(t); r != nil && c.TakesStyle(r) {
			comp.styled = 1
		}
	}
	return comp.styled > 0
}

// rootComponent is the element or component a component's body shows,
// when its views are that one call.
func (c *Checker) rootComponent(t *TypeName) *TypeName {
	comp := t.Comp
	if comp.Store || comp.Screen || comp.Body == nil || comp.Ctx == nil {
		return nil
	}
	var call *syntax.CallExpr
	for _, it := range comp.Body.Items {
		switch it := it.(type) {
		case *syntax.PropDecl, *syntax.EventDecl, *syntax.StateDecl, *syntax.LetDecl, *syntax.UseDecl,
			*syntax.FuncDecl, *syntax.ProvideDecl, *syntax.TaskDecl, *syntax.EffectDecl, *syntax.BadItem:
		case *syntax.ExprStmt:
			if call != nil {
				return nil
			}
			e, ok := unparen(it.X).(*syntax.CallExpr)
			if !ok {
				return nil
			}
			call = e
		default:
			return nil
		}
	}
	if call == nil {
		return nil
	}
	var o Object
	switch f := call.Fun.(type) {
	case *syntax.Ident:
		o = comp.Ctx.scope.Lookup(f.Name)
	case *syntax.MemberExpr:
		if id, ok := f.X.(*syntax.Ident); ok {
			if pn, ok := comp.Ctx.scope.Lookup(id.Name).(*PkgName); ok {
				o = pn.Pkg.Scope.LookupLocal(f.Name.Name)
			}
		}
	}
	if tn, ok := o.(*TypeName); ok && tn.Comp != nil && !tn.Comp.Store {
		return tn
	}
	return nil
}
