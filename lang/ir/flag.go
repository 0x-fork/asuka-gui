package ir

import (
	"encoding/json"
	"fmt"
	"strconv"
	"strings"
	"unicode"

	"github.com/egoist/asuka/lang/check"
	"github.com/egoist/asuka/lang/syntax"
)

// asuka/flag's spec<T>() (std/flag): what the compiler knows of T, a
// struct of options or an enum of commands, written as JSON for the
// parser (std/flag/flag.asuka), in place of the descriptor a generic
// native fn gets. A struct's field with a default is an option
// (--kebab-case), its doc comment its help (a first word -x its short
// name), its default the value when it isn't given; a field without one
// is an argument, in order. An enum's cases are commands, each with its
// struct of options (or none); a struct's field whose type is such an
// enum is the command its arguments end with. T's doc comment says what
// the program does.

type flagSpec struct {
	About        string     `json:"about"`
	Version      string     `json:"version"`
	Options      []flagItem `json:"options"`
	Args         []flagItem `json:"args"`
	Commands     []flagCmd  `json:"commands"`
	CommandField string     `json:"commandField"` // the struct's command field ("" for an enum T)
	IsEnum       bool       `json:"isEnum"`
}

type flagItem struct {
	Field    string   `json:"field"`
	Name     string   `json:"name"` // --name, <name>
	Short    string   `json:"short"`
	Help     string   `json:"help"`
	Kind     string   `json:"kind"` // bool, int, float, string, enum
	Default  string   `json:"default"`
	Repeat   bool     `json:"repeat"`
	Optional bool     `json:"optional"`
	Choices  []string `json:"choices"` // an enum's values as typed
	Cases    []string `json:"cases"`   // and its cases' names
}

type flagCmd struct {
	Name  string `json:"name"`
	Case  string `json:"case"`
	Help  string `json:"help"`
	Form  string `json:"form"` // none, list ({"case": [o]}), object ({"case": {"label": o}})
	Label string `json:"label"`
	Spec  string `json:"spec"` // its options' spec ("" for none)
}

// flagSpecOf is T's spec as JSON, or an error at what in T the parser
// can't take.
func (l *Lowerer) flagSpecOf(t check.Type) (string, error) {
	s, err := l.flagSpec(t, true)
	if err != nil {
		return "", err
	}
	b, err := json.Marshal(s)
	return string(b), err
}

func (l *Lowerer) flagSpec(t check.Type, top bool) (*flagSpec, error) {
	n, ok := t.(*check.Named)
	if !ok || n.Obj.Kind != check.StructType && n.Obj.Kind != check.EnumType {
		return nil, fmt.Errorf("flag.parse takes a struct of options or an enum of commands, not %s", t)
	}
	s := &flagSpec{About: declDoc(n.Obj.Decl), Options: []flagItem{}, Args: []flagItem{}, Commands: []flagCmd{}}
	if top {
		s.Version = l.opts.Version
	}
	if n.Obj.Kind == check.EnumType {
		cmds, err := l.flagCommands(n)
		if err != nil {
			return nil, err
		}
		s.IsEnum, s.Commands = true, cmds
		return s, nil
	}
	decl, _ := n.Obj.Decl.(*syntax.StructDecl)
	shorts := map[string]string{}
	for _, f := range n.Obj.Fields {
		var sf *syntax.Field
		if decl != nil {
			for _, m := range decl.Members {
				if x, ok := m.(*syntax.Field); ok && x.Name.Name == f.Name {
					sf = x
				}
			}
		}
		doc := ""
		if sf != nil {
			doc = sf.Doc()
		}
		short, help := shortName(doc)
		where := func(format string, args ...any) error {
			return l.atf(f.Pos, "%s.%s: "+format, append([]any{n.Obj.Name(), f.Name}, args...)...)
		}
		if cmd, ok := commandEnum(f.Type); ok {
			if f.Default != nil || s.CommandField != "" {
				return nil, where("a command is a field without a default, and a struct has one")
			}
			cmds, err := l.flagCommands(cmd)
			if err != nil {
				return nil, err
			}
			s.CommandField, s.Commands = f.Name, cmds
			continue
		}
		it := flagItem{Field: f.Name, Name: kebab(f.Name), Help: help, Choices: []string{}, Cases: []string{}}
		elem := f.Type
		if o, ok := elem.(*check.Optional); ok {
			it.Optional, elem = true, o.Elem
		} else if a, ok := elem.(*check.Array); ok {
			it.Repeat, elem = true, a.Elem
		}
		switch elem {
		case check.Bool:
			it.Kind = "bool"
		case check.Int:
			it.Kind = "int"
		case check.Float:
			it.Kind = "float"
		case check.String:
			it.Kind = "string"
		default:
			en, ok := elem.(*check.Named)
			if !ok || en.Obj.Kind != check.EnumType || en.Obj.RefEnum() {
				return nil, where("an option or an argument is a bool, an int, a float, a string or an enum without payloads (or an optional or an array of one), not %s", f.Type)
			}
			it.Kind = "enum"
			for _, c := range en.Obj.Cases {
				it.Choices = append(it.Choices, kebab(c.Name))
				it.Cases = append(it.Cases, c.Name)
			}
		}
		if (it.Kind == "bool") && (it.Optional || it.Repeat) {
			return nil, where("a switch is a bool, not %s", f.Type)
		}
		if f.Default == nil {
			// An argument.
			if it.Kind == "bool" {
				return nil, where("a switch has a default (%s = false): a field without one is an argument", f.Name)
			}
			if short != "" {
				return nil, where("an argument has no short name: -%s is an option's (give it a default)", short)
			}
			if len(s.Args) > 0 {
				if last := s.Args[len(s.Args)-1]; last.Repeat {
					return nil, where("an array of arguments takes the rest: it's the last argument")
				} else if last.Optional && !it.Optional && !it.Repeat {
					return nil, where("an argument that may be left out comes after the ones that may not")
				}
			}
			s.Args = append(s.Args, it)
			continue
		}
		def, err := constJSON(f.Default, elem, it.Repeat || it.Optional)
		if err != nil {
			return nil, where("%v: flag.parse shows an option's default, so it's a value written out (a literal, an enum's case, none, [])", err)
		}
		it.Default = def
		if short != "" {
			if short == "h" {
				return nil, where("-h is --help's")
			}
			if o, ok := shorts[short]; ok {
				return nil, where("-%s is %s's already", short, o)
			}
			shorts[short] = f.Name
			it.Short = short
		}
		s.Options = append(s.Options, it)
	}
	if s.CommandField != "" && len(s.Args) > 0 {
		return nil, l.atf(n.Obj.Pos(), "%s: a struct with a command has no arguments of its own: the command's are its", n.Obj.Name())
	}
	return s, nil
}

// commandEnum reports whether t is an enum whose cases are commands:
// one at least carries a struct of options.
func commandEnum(t check.Type) (*check.Named, bool) {
	n, ok := t.(*check.Named)
	if !ok || n.Obj.Kind != check.EnumType {
		return nil, false
	}
	for _, c := range n.Obj.Cases {
		if len(c.Params) > 0 {
			return n, true
		}
	}
	return nil, false
}

func (l *Lowerer) flagCommands(n *check.Named) ([]flagCmd, error) {
	decl, _ := n.Obj.Decl.(*syntax.EnumDecl)
	var out []flagCmd
	for _, c := range n.Obj.Cases {
		cmd := flagCmd{Name: kebab(c.Name), Case: c.Name, Form: "none"}
		if decl != nil {
			for _, ec := range decl.Cases {
				if ec.Name.Name == c.Name {
					cmd.Help = firstLine(ec.Doc())
				}
			}
		}
		switch len(c.Params) {
		case 0:
		case 1:
			if p, ok := c.Params[0].(*check.Named); !ok || p.Obj.Kind != check.StructType {
				return nil, l.atf(c.Pos, "%s.%s: a command carries its options' struct, not %s", n.Obj.Name(), c.Name, c.Params[0])
			}
			sub, err := l.flagSpec(c.Params[0], false)
			if err != nil {
				return nil, err
			}
			if sub.About == "" {
				sub.About = cmd.Help
			}
			b, _ := json.Marshal(sub)
			cmd.Spec = string(b)
			cmd.Form = "list"
			if c.Labels[0] != "" {
				cmd.Form, cmd.Label = "object", c.Labels[0]
			}
		default:
			return nil, l.atf(c.Pos, "%s.%s: a command carries one struct, its options", n.Obj.Name(), c.Name)
		}
		out = append(out, cmd)
	}
	return out, nil
}

// posError is an error at a place in the source.
type posError struct {
	pos syntax.Pos
	msg string
}

func (e *posError) Error() string { return e.msg }

// atf is an error at pos.
func (l *Lowerer) atf(pos syntax.Pos, format string, args ...any) error {
	return &posError{pos, fmt.Sprintf(format, args...)}
}

// flagSpecArg is spec<T>()'s argument: T's spec, a string constant; an
// error is reported where it is (else at the call).
func (b *fb) flagSpecArg(t check.Type) int {
	s, err := b.l.flagSpecOf(t)
	if err != nil {
		var pos syntax.Position
		if pe, ok := err.(*posError); ok && pe.pos.IsValid() {
			pos = b.l.fset.Position(pe.pos)
		}
		b.l.errs = append(b.l.errs, check.Diagnostic{Pos: pos, End: pos, Msg: err.Error()})
	}
	return b.kstr(s)
}

func declDoc(d syntax.Node) string {
	switch d := d.(type) {
	case *syntax.StructDecl:
		return d.Doc()
	case *syntax.EnumDecl:
		return d.Doc()
	}
	return ""
}

// shortName reads a doc comment's short name: a first word -x (a letter
// or a digit). It returns the name and the rest, the help.
func shortName(doc string) (string, string) {
	doc = strings.TrimSpace(doc)
	if len(doc) >= 2 && doc[0] == '-' && doc[1] < 0x80 && (unicode.IsLetter(rune(doc[1])) || unicode.IsDigit(rune(doc[1]))) &&
		(len(doc) == 2 || doc[2] == ' ' || doc[2] == '\n' || doc[2] == ',' || doc[2] == ':') {
		rest := strings.TrimLeft(doc[2:], ",: \n")
		return doc[1:2], strings.Join(strings.Fields(rest), " ")
	}
	return "", strings.Join(strings.Fields(doc), " ")
}

func firstLine(s string) string {
	s = strings.TrimSpace(s)
	if i := strings.Index(s, "\n\n"); i >= 0 {
		s = s[:i]
	}
	return strings.Join(strings.Fields(s), " ")
}

// kebab is a camelCase name as a command line writes it: dryRun,
// dry-run.
func kebab(s string) string {
	var b strings.Builder
	for i, r := range s {
		if unicode.IsUpper(r) {
			if i > 0 {
				b.WriteByte('-')
			}
			r = unicode.ToLower(r)
		}
		if r == '_' {
			r = '-'
		}
		b.WriteRune(r)
	}
	return b.String()
}

// constJSON is a default written out as JSON: a literal, a negated
// number, none, an enum's case (.fast, Mode.fast), an array of them.
func constJSON(e syntax.Expr, elem check.Type, wrapped bool) (string, error) {
	switch x := e.(type) {
	case *syntax.ParenExpr:
		return constJSON(x.X, elem, wrapped)
	case *syntax.NoneLit:
		return "null", nil
	case *syntax.BoolLit:
		return strconv.FormatBool(x.Value), nil
	case *syntax.BasicLit:
		switch x.Kind {
		case syntax.INT:
			v, err := check.ParseInt(x.Value)
			if err != nil {
				return "", err
			}
			if elem == check.Float {
				return strconv.FormatFloat(float64(v), 'g', -1, 64), nil
			}
			return strconv.FormatInt(v, 10), nil
		case syntax.FLOAT:
			v, err := strconv.ParseFloat(strings.ReplaceAll(x.Value, "_", ""), 64)
			if err != nil {
				return "", err
			}
			return strconv.FormatFloat(v, 'g', -1, 64), nil
		}
	case *syntax.UnaryExpr:
		if x.Op == syntax.SUB {
			v, err := constJSON(x.X, elem, wrapped)
			if err == nil && v != "" && (v[0] >= '0' && v[0] <= '9') {
				return "-" + v, nil
			}
		}
	case *syntax.StringLit:
		if len(x.Interps) == 0 {
			v, err := check.Unescape(strings.Join(x.Segments, ""), x.Raw)
			if err != nil {
				return "", err
			}
			b, _ := json.Marshal(v)
			return string(b), nil
		}
	case *syntax.ImplicitMember:
		b, _ := json.Marshal(x.Name.Name)
		return string(b), nil
	case *syntax.MemberExpr:
		if _, ok := x.X.(*syntax.Ident); ok {
			b, _ := json.Marshal(x.Name.Name)
			return string(b), nil
		}
	case *syntax.ArrayExpr:
		var parts []string
		for _, el := range x.Elems {
			v, err := constJSON(el, elem, false)
			if err != nil {
				return "", err
			}
			parts = append(parts, v)
		}
		return "[" + strings.Join(parts, ",") + "]", nil
	}
	return "", fmt.Errorf("its default isn't written out")
}
