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
	// Follow: the parameter (its index + 1) whose argument, when it reads
	// state, is passed as a thunk the runtime follows (alert's actions).
	Follow int
	Doc    string
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
	{Name: "quit", Sig: "()", Builtin: "ui.quit", Doc: "quits the app as the user's Quit does (⌘Q): on quit's handlers first, which may cancel it; then its windows close (desktop; phones and the web ignore it)"},
	{Name: "relaunch", Sig: "()", Builtin: "ui.relaunch", Doc: "quits the app as quit() does, then starts it again with the same arguments once it has (a canceled quit cancels it)"},
	{Name: "closeWindow", Sig: "()", Builtin: "ui.window_close", Doc: "closes the window the calling code is in (the main window outside every Window) now, without asking: what a closeRequest that kept it open calls once the user has chosen"},
	{Name: "onCloseRequest", Body: true, Sig: "(handler: fn() -> bool)", Builtin: "ui.on_close_request", Doc: "asked when the user closes the window the component is in, while it lives: false keeps it open"},
	{Name: "onQuit", Body: true, Sig: "(handler: fn(QuitEvent))", Builtin: "ui.on_quit", Doc: "hears the app about to quit, however that started, while the component lives: e.cancel() keeps it running"},
	{Name: "onSecondInstance", Body: true, Sig: "(handler: fn([string], string))", Builtin: "ui.on_second_instance", Doc: "hears the app started again while it runs (its arguments, its working directory), while the component lives"},
	{Name: "alert", Sig: "(title: string, message: string = \"\", actions: [Action]? = none, actionSheet: bool = false, fields: [AlertField] = [])", Builtin: "ui.alert", Follow: 3, Doc: "shows an alert now (an action sheet with actionSheet): its actions are its buttons (none: a single OK), followed while it shows when they read state (a button disabled while a field is empty); fields are text fields under the message. A button's run runs as it's chosen"},
	{Name: "snackbar", Sig: "(message: string, action: Action? = none, duration: SnackbarDuration = .short, onDismiss: fn()? = none) -> fn()", Builtin: "ui.snackbar", Doc: "shows a brief message at the bottom of the window now, with an action (a .button's title and run), and returns a closure that dismisses it; a new one replaces the one showing, whose onDismiss runs"},
	{Name: "onBack", Body: true, Sig: "(handler: fn() -> bool)", Builtin: "ui.on_back", Doc: "asked when the user goes back from the nearest pushed screen, sheet or popover around the component (else the app's: Android's back), while it lives: true takes it (the screen stays), false lets it go back"},
	{Name: "depth", Sig: "() -> int", Builtin: "ui.depth", Doc: "how many screens the nearest stack holds"},
	{Name: "mountApp", Sig: "() -> TestApp", Builtin: "test.mount_app", Doc: "in a test: mounts the app block's view on the fake host (its main window, its menus, its handlers), as mount does a view"},
	{Name: "colorUniform", Sig: "(name: string, color: Color) -> Uniform", Builtin: "canvas.color_uniform", Doc: "a Shader's vec4 uniform of a color: its red, green, blue and alpha from 0 to 1"},
}

// AppEvents are the app block's events: on openURL(url) { }, their
// parameters, and what they lower to.
var AppEvents = []struct {
	Name, Params, Builtin string
}{
	{"openURL", "(url: string)", "ui.on_open_url"},
	{"files", "(paths: [string])", "ui.on_open_files"},
	{"share", "(items: [SharedItem])", "ui.on_share"},
	{"quit", "(e: QuitEvent)", "ui.on_quit"},
	{"secondInstance", "(args: [string], dir: string)", "ui.on_second_instance"},
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
		if a == nil {
			continue
		}
		if f.Follow == i+1 {
			// A binding: followed when it reads state.
			saved := c.ctx
			c.enter(CtxBinding, a, f.Name+"'s "+params[i].Name)
			c.assign(a, params[i].Type, "argument "+params[i].Name+" of "+f.Name)
			c.leave(saved)
			continue
		}
		c.assign(a, params[i].Type, "argument "+params[i].Name+" of "+f.Name)
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
	var e *syntax.CallExpr
	switch s := it.(type) {
	case *syntax.ExprStmt:
		e, _ = s.X.(*syntax.CallExpr)
	case *syntax.SlotItem:
		// onBack { … }: the call with its trailing block.
		if b, ok := c.lookup(s.Name.Name).(*Builtin); ok && b.ID == BRuntime && b.Fn.Body {
			call := c.info.SlotCalls[s]
			if call == nil {
				call = &syntax.CallExpr{Span: s.Span, Fun: s.Name, Block: s.Body}
				c.info.SlotCalls[s] = call
			}
			return true
		}
	}
	if e == nil {
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
			if s, ok := it.(*syntax.SlotItem); ok {
				c.expr(c.info.SlotCalls[s], nil)
			} else {
				c.expr(it.(*syntax.ExprStmt).X, nil)
			}
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
			ps, _ := c.parseSig(a.Params, "app's "+a.Name)
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
		c.errorf(h.Name, "the app has no event %s (its events: openURL, files, share, quit, secondInstance)", h.Name.Name)
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
