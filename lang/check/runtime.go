package check

import (
	"github.com/egoist/asuka/internal/shadercode"
	"github.com/egoist/asuka/lang/syntax"
)

// The prelude's functions the UI runtime implements (core/src/ui's
// builtins_ext.c and canvas.c): a table, as the built-in methods are, of
// a name, a signature over the prelude's types and the builtin the
// lowering calls with CALLB, its arguments in the parameters' order. A
// Ref argument crosses as the view it holds (none when it holds none).

// A RuntimeFunc is a prelude function the runtime implements.
type RuntimeFunc struct {
	Name    string
	Sig     string // (name: Type, …) -> Result, as Method's
	Builtin string
	Async   bool // returns a future the call awaits
	Throws  bool
	// NeedsView: the call does nothing when its Ref holds no view (the
	// builtin takes a view, not none).
	NeedsView bool
	// Body: a registration a component's body (or the app block) may
	// make as one of its items, for as long as it lives (useKeys(…)).
	Body bool
	// Once: what it's passed (a closure that reads state) doesn't make
	// it reactive: let stop = onFrame(…) is made once, not a memo.
	Once bool
	Doc  string
}

// RuntimeFuncs lists them (docs/language.md's "The system").
var RuntimeFuncs = []*RuntimeFunc{
	{Name: "share", Sig: "(text: string)", Builtin: "ui.share", Doc: "shows the system's share sheet for a text or a link"},
	{Name: "shareFiles", Sig: "(paths: [string], mime: string = \"\", uti: string = \"\", title: string = \"\", from: Ref? = none)", Builtin: "ui.share_files", Async: true, Throws: true, Doc: "shares files (their paths) with the share sheet, anchored on from where a popover points (an iPad, a Mac): returns once it's closed"},
	{Name: "haptic", Sig: "(kind: Haptic)", Builtin: "ui.haptic", Doc: "plays the platform's haptic feedback"},
	{Name: "copy", Sig: "(text: string)", Builtin: "ui.copy", Doc: "puts text on the clipboard"},
	{Name: "openURL", Sig: "(url: string)", Builtin: "ui.open_url", Doc: "opens url in the app that handles it (the browser, Mail, Maps)"},
	{Name: "announce", Sig: "(text: string)", Builtin: "ui.announce", Doc: "has a screen reader say text"},
	{Name: "a11yFocus", Sig: "(view: Ref)", Builtin: "ui.a11y_focus", NeedsView: true, Doc: "moves a screen reader's focus to the view"},
	{Name: "webCookies", Sig: "(url: string) -> string", Builtin: "ui.web_cookies", Async: true, Throws: true, Doc: "the cookies the web views keep for url, as a Cookie header"},
	{Name: "animating", Sig: "() -> bool", Builtin: "ui.animating", Doc: "whether the changes made now animate (inside animate's block and what it causes)"},
	{Name: "onFrame", Once: true, Sig: "(frame: fn(Duration)) -> fn()", Builtin: "ui.on_frame", Doc: "calls frame at each frame the display draws, with its time, until the closure it returns is called or what called it goes"},
	{Name: "frames", Sig: "() -> Stream<Duration>", Builtin: "ui.frames", Doc: "the frames the display draws, as a stream of their times: drawn only while it's read (for await t in frames() { … })"},
	{Name: "useKeys", Body: true, Sig: "(keys: [Shortcut])", Builtin: "ui.use_keys", Doc: "the window's keys while the component lives (the last one's win)"},
	{Name: "onOpenURL", Body: true, Sig: "(handler: fn(string))", Builtin: "ui.on_open_url", Doc: "hears the URLs the app is opened with while the component lives"},
	{Name: "onOpenFiles", Body: true, Sig: "(handler: fn([string]))", Builtin: "ui.on_open_files", Doc: "hears the files the app is opened with (their paths) while the component lives"},
	{Name: "onShare", Body: true, Sig: "(handler: fn([SharedItem]))", Builtin: "ui.on_share", Doc: "hears what other apps share with the app while the component lives"},
	{Name: "colorUniform", Sig: "(name: string, color: Color) -> Uniform", Builtin: "canvas.color_uniform", Doc: "a Shader's vec4 uniform of a color: its red, green, blue and alpha from 0 to 1"},
}

// AppEvents are the app block's events: on openURL(url) { }, and what
// they lower to.
var AppEvents = []struct {
	Name, Param, Type, Builtin string
}{
	{"openURL", "url", "string", "ui.on_open_url"},
	{"files", "paths", "[string]", "ui.on_open_files"},
	{"share", "items", "[SharedItem]", "ui.on_share"},
}

// runtimeSig is f's parameters and result in this checker's prelude.
func (c *Checker) runtimeSig(f *RuntimeFunc) ([]*Param, Type) {
	if s, ok := c.fsigs[f]; ok {
		return s.params, s.result
	}
	ps, res := c.parseSig(f.Sig, f.Name)
	c.fsigs[f] = parsedSig{ps, res}
	return ps, res
}

// runtimeCall checks a call of a RuntimeFunc.
func (c *Checker) runtimeCall(e *syntax.CallExpr, call *Call, f *RuntimeFunc) {
	params, res := c.runtimeSig(f)
	args := c.matchArgs(e, params, f.Name)
	c.trailingClosure(e, params, args)
	for i, a := range args {
		if a != nil {
			c.assign(a, params[i].Type, "argument "+params[i].Name+" of "+f.Name)
		}
	}
	c.typeDefaults(args, params)
	call.Args, call.Params, call.Result = args, params, res
	c.callEffects(e, f.Name, f.Async, f.Throws)
}

// animateCall checks animate(body), animate(anim, body) and their
// trailing-block forms, animate { … } and animate(.spring) { … }. The
// body is a closure that doesn't wait. It needn't be awaited; awaited,
// it returns once the animation is complete (Call.Awaited).
func (c *Checker) animateCall(e *syntax.CallExpr, call *Call) {
	var list []syntax.Expr
	if e.Args != nil {
		for _, a := range e.Args.List {
			if a.Label != nil {
				c.errorf(a.Label, "animate's arguments have no names: animate(Animation(…)) { … }")
			}
			list = append(list, a.Value)
		}
	}
	var anim, body syntax.Expr
	switch {
	case e.Block != nil && len(list) <= 1:
		lam := &syntax.LambdaExpr{Span: e.Block.Span, Body: e.Block}
		c.info.Trailing[e] = lam
		body = lam
		if len(list) == 1 {
			anim = list[0]
		}
	case e.Block == nil && len(list) == 1:
		body = list[0]
	case e.Block == nil && len(list) == 2:
		anim, body = list[0], list[1]
	default:
		c.errorf(e, "animate takes its changes, and maybe how they animate: animate { … }, animate(.spring) { … }")
		for _, a := range list {
			c.expr(a, nil)
		}
	}
	if anim != nil {
		at := c.prelude.Scope.LookupLocal("Animation").(*TypeName)
		c.resolveType(at)
		c.assign(anim, at.Type, "how animate animates")
	}
	if body != nil {
		c.assign(body, &FuncType{Result: Void}, "animate's changes")
	}
	call.Args = []syntax.Expr{anim, body}
	call.Result = Void
}

// shaderCall checks shader("""…"""): a Kage program, compiled now into
// the bytes a Shader's program takes.
func (c *Checker) shaderCall(e *syntax.CallExpr, call *Call) {
	call.Result = Bytes
	if e.Args == nil || len(e.Args.List) != 1 || e.Args.List[0].Label != nil {
		c.errorf(e, "shader takes a Kage program, as a string: shader(\"\"\"…\"\"\")")
		c.checkArgsLoosely(e)
		return
	}
	a := e.Args.List[0].Value
	lit, ok := unparen(a).(*syntax.StringLit)
	c.assign(a, String, "the shader's program")
	if !ok || len(lit.Interps) > 0 {
		c.errorf(a, "shader's program is compiled with the app: write it as a string literal, without {…}")
		return
	}
	text := lit.Segments[0]
	if lit.Multiline {
		text = Dedent(text)
	}
	text, _ = Unescape(text, lit.Raw)
	code, err := shadercode.Compile(text)
	if err != nil {
		c.errorf(a, "the shader doesn't compile: %v", err)
		return
	}
	c.info.Shaders[e] = code.Encode()
	call.Args = []syntax.Expr{a}
}

// bodyCall reports an item of a component's body (or the app block)
// that's a registration (useKeys(…), onOpenURL(…)), not a view.
func (c *Checker) bodyCall(it syntax.Item) bool {
	s, ok := it.(*syntax.ExprStmt)
	if !ok {
		return false
	}
	e, ok := s.X.(*syntax.CallExpr)
	if !ok {
		return false
	}
	id, ok := e.Fun.(*syntax.Ident)
	if !ok {
		return false
	}
	b, ok := c.lookup(id.Name).(*Builtin)
	return ok && b.ID == BRuntime && b.Fn.Body
}

// bodyCalls checks a body's registrations, as statements of its
// function, and returns the rest of its items.
func (c *Checker) bodyCalls(items []syntax.Item) []syntax.Item {
	var rest []syntax.Item
	for _, it := range items {
		if c.bodyCall(it) {
			c.info.BodyCalls[it] = true
			savedView := c.view
			c.view = false
			c.expr(it.(*syntax.ExprStmt).X, nil)
			c.view = savedView
			continue
		}
		rest = append(rest, it)
	}
	return rest
}

// appEvent checks the app block's on openURL(url) { }, on files(paths)
// { }, on share(items) { }.
func (c *Checker) appEvent(h *syntax.OnHandler) {
	var ev *Member
	for _, a := range AppEvents {
		if a.Name == h.Name.Name {
			ps, _ := c.parseSig("("+a.Param+": "+a.Type+")", "app's "+a.Name)
			ev = &Member{object: object{name: a.Name}, Kind: EventMember, Type: Void, Params: ps}
		}
	}
	saved := c.ctx
	ctx := c.enter(CtxHandler, h, "app.on "+h.Name.Name)
	ctx.Async, ctx.Throws = true, true
	savedView, savedLoops := c.view, c.loops
	c.view, c.loops = false, 0
	c.openScope()
	if ev == nil {
		c.errorf(h.Name, "the app has no event %s (its events: openURL, files, share)", h.Name.Name)
		for _, p := range h.Params {
			c.declareVar(p, Invalid, false)
		}
	} else {
		if len(h.Params) > len(ev.Params) {
			c.errorf(h, "event %s gives %s", h.Name.Name, plural(len(ev.Params), "value", "values"))
		}
		var vars []*Var
		for i, p := range h.Params {
			t := Type(Invalid)
			if i < len(ev.Params) {
				t = ev.Params[i].Type
			}
			v := c.declareVar(p, t, false)
			ctx.Params = append(ctx.Params, v)
			vars = append(vars, v)
		}
		c.info.Binds[h] = vars
		c.info.AppHandlers = append(c.info.AppHandlers, &HandlerArg{Event: ev, Handler: h})
	}
	c.stmts(h.Body.Items)
	c.closeScope()
	c.view, c.loops = savedView, savedLoops
	c.leave(saved)
}
