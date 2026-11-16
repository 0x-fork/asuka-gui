package check

import (
	"sort"
	"strings"

	"github.com/egoist/asuka/lang/syntax"
)

// The app's catalogs: tr.key(args) says a message of them, in the
// language the app shows (docs/i18n.md). lang/compile loads and checks
// the catalogs (internal/i18ngen) and compiles them (package i18n's
// Catalog.Compile); the checker knows their keys and the arguments each
// takes, so a key or an argument that isn't there is an error where it's
// used.

// A Catalog is what the checker knows of the app's catalogs.
type Catalog struct {
	// Blob is the compiled catalog the module registers as it starts
	// (i18n.catalog).
	Blob []byte
	// Dir names the catalogs' directory in messages ("locales").
	Dir string
	// Source is the language the app is written in.
	Source string
	// Keys are the messages tr names, by key.
	Keys map[string]*CatalogKey
	// Implicit is the catalog of an app without catalogs that formats
	// numbers and dates (asuka/i18n): its source language's formats, and
	// no tr.
	Implicit bool
	// Broken is a catalog whose errors were reported: tr's calls aren't
	// checked against it.
	Broken bool
}

// A CatalogKey is a message of the source language.
type CatalogKey struct {
	Name        string
	Message     string // the source language's, as written
	Description string
	// Index is the key's index in the compiled catalog (i18n.say's).
	Index int
	// Params are its arguments, in the compiled catalog's order (the
	// tuple i18n.say takes).
	Params []*CatalogParam
}

// A CatalogParam is a message's argument.
type CatalogParam struct {
	Name string
	// ARB is its placeholder's type: String, int, double, num, DateTime
	// or Object.
	ARB string
}

// TrName is tr, the app's catalogs.
type TrName struct {
	object
	Catalog *Catalog
}

// SetCatalog gives the program tr, of cat's messages. Call it before
// checking any package.
func (c *Checker) SetCatalog(cat *Catalog) {
	if !cat.Broken {
		c.catalog = cat
	}
	if !cat.Implicit {
		c.universe.Insert(&TrName{object: object{name: "tr"}, Catalog: cat})
	}
}

// trackedNative reports whether the standard library's native fn name of
// package path reads what the runtime tracks (the language shown, the
// regional formats): a binding that calls it follows them, and spawn
// can't call it (they're the main thread's).
func trackedNative(path, name string) bool {
	if path != "asuka/i18n" {
		return false
	}
	switch name {
	case "locale", "preferred_languages", "is_rtl", "region", "format_locale", "format_number", "format_int", "format_date", "languages", "autonym":
		return true
	}
	return false
}

// trCall checks tr.key(name: value, ...).
func (c *Checker) trCall(e *syntax.CallExpr, fn *syntax.MemberExpr, tr *TrName) Type {
	call := &Call{Kind: CallTr, Result: String}
	c.info.Calls[e] = call
	if c.ctx != nil {
		// It reads the language shown: a function that says one follows
		// the language where a binding calls it (and spawn can't).
		c.ctx.readsState = true
	}
	if c.pkg != nil && (strings.HasPrefix(c.pkg.Path, "asuka/") || strings.HasPrefix(c.pkg.Path, "@")) {
		c.errorf(fn.X, "tr is the app's catalogs: a package can't say their messages (take the text as a parameter)")
	}
	if e.Block != nil {
		c.errorf(e.Block, "tr.%s takes no block", fn.Name.Name)
	}
	name := fn.Name.Name
	if tr.Catalog.Broken {
		c.checkArgsLoosely(e)
		return String
	}
	k := tr.Catalog.Keys[name]
	if k == nil {
		c.errorf(fn.Name, "%s has no message %s%s", tr.Catalog.sourceFile(), name, tr.Catalog.suggest(name))
		c.checkArgsLoosely(e)
		return String
	}
	call.TrKey = k
	call.Args = make([]syntax.Expr, len(k.Params))
	var list []*syntax.Arg
	if e.Args != nil {
		list = e.Args.List
	}
	for _, a := range list {
		if a.Label == nil {
			// One argument may go without its name.
			if len(k.Params) == 1 && len(list) == 1 {
				call.Args[0] = a.Value
				continue
			}
			c.errorf(a.Value, "tr.%s's arguments are named after the message's: %s", name, k.signature())
			c.expr(a.Value, nil)
			continue
		}
		j := -1
		for pi, p := range k.Params {
			if p.Name == a.Label.Name {
				j = pi
			}
		}
		switch {
		case j < 0:
			if len(k.Params) == 0 {
				c.errorf(a.Label, "tr.%s takes no arguments: its message is %q", name, k.Message)
			} else {
				c.errorf(a.Label, "tr.%s has no argument %s: it takes %s", name, a.Label.Name, k.signature())
			}
			c.expr(a.Value, nil)
		case call.Args[j] != nil:
			c.errorf(a.Label, "tr.%s's %s is given twice", name, a.Label.Name)
			c.expr(a.Value, nil)
		default:
			call.Args[j] = a.Value
		}
	}
	for i, p := range k.Params {
		x := call.Args[i]
		if x == nil {
			c.errorf(e, "tr.%s needs its argument %s (%s): its message is %q", name, p.Name, c.trTypeName(p), k.Message)
			continue
		}
		c.trArg(x, p, name)
	}
	return String
}

// trArg checks an argument against its placeholder's type.
func (c *Checker) trArg(x syntax.Expr, p *CatalogParam, key string) {
	what := "tr." + key + "'s " + p.Name
	switch p.ARB {
	case "int":
		c.assign(x, Int, what)
	case "double", "num":
		// A number: an int or a float, as it is.
		t := c.expr(x, Float)
		if !IsInvalid(t) && t != Int && t != Float {
			c.errorf(x, "%s is a number (%s), not a %s", what, p.ARB, typeString(t))
		}
	case "DateTime":
		t := c.expr(x, Int)
		if IsInvalid(t) || t == Int || isTime(t) {
			return
		}
		c.errorf(x, "%s is a date: a Time (time.now()), or nanoseconds since 1970, not a %s", what, typeString(t))
	default: // String, Object
		c.assign(x, String, what)
	}
}

// isTime reports whether t is asuka/time's Time, whose instant a
// message's date argument writes.
func isTime(t Type) bool {
	n, ok := t.(*Named)
	if !ok || n.Obj.Kind != StructType || n.Obj.name != "Time" || n.Obj.Pkg == nil || n.Obj.Pkg.Path != "asuka/time" {
		return false
	}
	return TimeInstant(t) >= 0
}

// TimeInstant is the index of a Time's instant field (nanoseconds since
// the Unix epoch), or -1.
func TimeInstant(t Type) int {
	n, ok := t.(*Named)
	if !ok || len(n.Obj.Fields) == 0 {
		return -1
	}
	for i, f := range n.Obj.Fields {
		if f.Name == "instant" && f.Type == Int {
			return i
		}
	}
	return -1
}

func (c *Checker) trTypeName(p *CatalogParam) string {
	switch p.ARB {
	case "int":
		return "an int"
	case "double", "num":
		return "a number"
	case "DateTime":
		return "a date"
	}
	return "a string"
}

func (k *CatalogKey) signature() string {
	var ps []string
	for _, p := range k.Params {
		ps = append(ps, p.Name+":")
	}
	return "tr." + k.Name + "(" + strings.Join(ps, " ") + ")"
}

func (cat *Catalog) sourceFile() string {
	dir := cat.Dir
	if dir == "" {
		dir = "locales"
	}
	return dir + "/" + cat.Source + ".json"
}

func (cat *Catalog) suggest(name string) string {
	best, bestD := "", 3
	names := make([]string, 0, len(cat.Keys))
	for n := range cat.Keys {
		names = append(names, n)
	}
	sort.Strings(names)
	for _, n := range names {
		if d := editDistance(strings.ToLower(name), strings.ToLower(n)); d < bestD && d < len(name) {
			best, bestD = n, d
		}
	}
	if best == "" {
		return ""
	}
	return " (did you mean tr." + best + "?)"
}
