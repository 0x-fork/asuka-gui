// Package diag is the table of Asuka's diagnostics: every error and
// warning the compiler reports, and every one the core reports as an app
// runs in a development build, has a code here (rfcs/0015). A code is a
// stable kebab-case name (write-in-binding), never renamed or reused
// once released, with a one-line summary, why the rule is there, how to
// fix what breaks it, and two programs: one that breaks it and one that
// doesn't, which go test ./lang/diag compiles. Its Chinese and Japanese
// text is in codes_zh.go and codes_ja.go.
//
// The table is the documentation: asuka explain prints a code's page and
// go generate ./lang/diag writes docs/errors/<code>.md (and its zh and ja
// pages) from it.
package diag

//go:generate go run ./gen

import (
	"fmt"
	"sort"
	"strings"
)

// Severity says whether a diagnostic stops the build.
type Severity uint8

const (
	Error   Severity = iota + 1 // the program doesn't compile (or, at run time, it panics)
	Warning                     // worth saying; the build goes on
)

func (s Severity) String() string {
	if s == Warning {
		return "warning"
	}
	return "error"
}

// A Code is a rule a diagnostic reports a break of.
type Code struct {
	Name     string
	Severity Severity
	// RunTime: the core reports it as the app runs, in a development
	// build (the player, asuka run, asuka test), not the compiler. Wrong
	// is then a program whose test block makes the core report it.
	RunTime bool
	// Panic: a run-time code the core panics with, in every build (a
	// release build's crash report says it); the others are the
	// development builds' detectors.
	Panic bool
	// Internal: a compiler bug, which no program should hit; it has no
	// examples.
	Internal bool
	// Summary is the rule in one line; Why, why it's there; Fix, what to
	// write instead. All three are Markdown.
	Summary, Why, Fix string
	// Wrong breaks the rule; Right is it fixed. Each is a file
	// (app.asuka), or several, each after a line "-- name --" (txtar's
	// form): a catalog, a package beside the app. Read Wrong with
	// WrongSource: a code whose wrong program is too big to keep made
	// has it made when it's read (wrong).
	Wrong, Right string
	wrong        func() string
}

// WrongSource is c's program that breaks its rule.
func (c *Code) WrongSource() string {
	if c.wrong != nil {
		return c.wrong()
	}
	return c.Wrong
}

// Text is a code's prose in one language.
type Text struct {
	Summary, Why, Fix string
}

var (
	all    []*Code
	byName = map[string]*Code{}
)

func def(c Code) *Code {
	if _, dup := byName[c.Name]; dup {
		panic("diag: " + c.Name + " is defined twice")
	}
	if c.Severity == 0 {
		c.Severity = Error
	}
	p := &c
	all = append(all, p)
	byName[c.Name] = p
	return p
}

// Lookup is the code named name, or nil.
func Lookup(name string) *Code { return byName[name] }

// All is every code, by name.
func All() []*Code {
	out := append([]*Code(nil), all...)
	sort.Slice(out, func(i, j int) bool { return out[i].Name < out[j].Name })
	return out
}

// Languages are the languages the table is written in.
var Languages = []string{"en", "zh", "ja"}

var translations = map[string]map[string]Text{"zh": zh, "ja": ja}

// Text is c's prose in lang ("en", "zh", "ja"); English when the
// language has none.
func (c *Code) Text(lang string) Text {
	if t, ok := translations[lang][c.Name]; ok {
		return t
	}
	return Text{c.Summary, c.Why, c.Fix}
}

// Translated reports whether c has its text in lang.
func (c *Code) Translated(lang string) bool {
	if lang == "en" {
		return true
	}
	t, ok := translations[lang][c.Name]
	return ok && t.Summary != "" && t.Why != "" && t.Fix != ""
}

// Site is where the documentation lives.
const Site = "https://asuka.egoist.dev/docs/errors/"

// URL is code's page.
func URL(code string) string { return Site + code }

// Lang is the table's language for a locale as LANG says it (zh_CN.UTF-8,
// ja_JP, en_US): "zh", "ja", else "en".
func Lang(locale string) string {
	l := strings.ToLower(locale)
	switch {
	case strings.HasPrefix(l, "zh"):
		return "zh"
	case strings.HasPrefix(l, "ja"):
		return "ja"
	}
	return "en"
}

// Split formats a diagnostic's message (fmt.Sprintf(format, args...))
// as what's wrong and the help after it: the message's first ": " in
// the format's text (not in what a verb puts there) ends what's wrong,
// when there's something before it besides verbs ("can't assign to %s
// here: change state in a handler" is "can't assign to count here" and
// "change state in a handler"). A format that is a verb alone ("%s")
// splits what it formats likewise. msg + ": " + help is the message.
func Split(format string, args ...any) (msg, help string) {
	if format == "%s" || format == "%v" {
		return splitText(fmt.Sprintf(format, args...))
	}
	verbs, words := 0, false
	for i := 0; i < len(format); i++ {
		ch := format[i]
		if ch == '%' {
			i++
			for i < len(format) && strings.IndexByte("+-# 0123456789.[]", format[i]) >= 0 {
				i++
			}
			if i < len(format) && format[i] != '%' {
				verbs++
			}
			continue
		}
		if ch == ':' && i+1 < len(format) && format[i+1] == ' ' {
			if !words || verbs > len(args) {
				break
			}
			return fmt.Sprintf(format[:i], args[:verbs]...), fmt.Sprintf(format[i+2:], args[verbs:]...)
		}
		if ch >= 'a' && ch <= 'z' || ch >= 'A' && ch <= 'Z' {
			words = true
		}
	}
	return fmt.Sprintf(format, args...), ""
}

// splitText splits a message already formatted at its first ": ", when
// there's a word before it.
func splitText(s string) (string, string) {
	i := strings.Index(s, ": ")
	if i <= 0 || !strings.ContainsFunc(s[:i], func(r rune) bool { return r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z' }) {
		return s, ""
	}
	return s[:i], s[i+2:]
}
