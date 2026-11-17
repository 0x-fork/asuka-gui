package check

import (
	"encoding/json"
	"fmt"
	"strings"

	"github.com/egoist/asuka/lang/diag"
	"github.com/egoist/asuka/lang/syntax"
)

// A Diagnostic is a problem the compiler found: where, its code (lang/diag),
// and its message, which ends with the help when it has one.
type Diagnostic struct {
	Pos syntax.Position
	End syntax.Position
	// Code names the rule the diagnostic reports a break of: its entry
	// in lang/diag, its page at diag.URL(Code).
	Code string
	// Msg is what's wrong, where, and then, after ": ", what to do: Help.
	Msg string
	// Help is the end of Msg that says what to do ("" when it says none):
	// what a terminal shows on a line of its own, and an editor as a fix.
	Help string
}

func (d Diagnostic) String() string { return d.Pos.String() + ": " + d.Msg }
func (d Diagnostic) Error() string  { return d.String() }

// What is the message without its help: what's wrong.
func (d Diagnostic) What() string {
	if d.Help == "" {
		return d.Msg
	}
	return strings.TrimSuffix(d.Msg, ": "+d.Help)
}

// newDiagnostic makes a diagnostic of code, its message formatted and
// split (diag.Split).
func newDiagnostic(pos, end syntax.Position, code *diag.Code, format string, args ...any) Diagnostic {
	what, help := diag.Split(format, args...)
	msg := what
	if help != "" {
		msg += ": " + help
	}
	return Diagnostic{Pos: pos, End: end, Code: code.Name, Msg: msg, Help: help}
}

// NewDiagnostic is a diagnostic of code at pos (to end), its message
// formatted: what the compiler's other parts (the loader, the lowering)
// report.
func NewDiagnostic(pos, end syntax.Position, code *diag.Code, format string, args ...any) Diagnostic {
	return newDiagnostic(pos, end, code, format, args...)
}

// FromSyntax is a parser's or a scanner's error as a diagnostic.
func FromSyntax(e *syntax.Error) Diagnostic {
	code := e.Code
	if code == "" {
		code = diag.SyntaxError.Name
	}
	_, help := diag.Split("%s", e.Msg)
	return Diagnostic{Pos: e.Pos, End: e.End, Code: code, Msg: e.Msg, Help: help}
}

// Severity is how bad d is: an error, or a warning for a code that's one.
func (d Diagnostic) Severity() diag.Severity {
	if c := diag.Lookup(d.Code); c != nil {
		return c.Severity
	}
	return diag.Error
}

// Format is d as a terminal shows it, as rustc does:
//
//	app.asuka:14:9: error[write-in-binding]: can't assign to count here
//	  = help: change state in a handler (on press { … }), a fn, a task or an effect
//	  = see https://asuka.egoist.dev/docs/errors/write-in-binding
//
// sev is error or warning; after, when set, ends the first line (the
// platforms a check found it on: "(for Linux)").
func (d Diagnostic) Format(sev diag.Severity, after string) string {
	var b strings.Builder
	b.WriteString(d.Pos.String())
	b.WriteString(": ")
	b.WriteString(sev.String())
	if d.Code != "" {
		b.WriteString("[" + d.Code + "]")
	}
	b.WriteString(": ")
	b.WriteString(d.What())
	if after != "" {
		b.WriteString(" " + after)
	}
	if d.Help != "" {
		b.WriteString("\n  = help: " + d.Help)
	}
	if d.Code != "" {
		b.WriteString("\n  = see " + diag.URL(d.Code))
	}
	return b.String()
}

// A Record is a diagnostic as asuka check --json and asuka test --json
// write it: one JSON object per line.
type Record struct {
	Code     string `json:"code"`
	Severity string `json:"severity"`
	File     string `json:"file"`
	Range    Range  `json:"range"`
	Message  string `json:"message"`
	Help     string `json:"help,omitempty"`
	URL      string `json:"url,omitempty"`
	// Platforms are those whose compile reports it, when not every one
	// checked does.
	Platforms []string `json:"platforms,omitempty"`
}

// A Range is where a diagnostic is: lines and columns from 1, the end
// exclusive (LSP's, but from 1).
type Range struct {
	Start Point `json:"start"`
	End   Point `json:"end"`
}

type Point struct {
	Line   int `json:"line"`
	Column int `json:"column"`
}

// Record is d as JSON's record, its severity sev.
func (d Diagnostic) Record(sev diag.Severity) Record {
	end := d.End
	if !end.IsValid() || end.Line == 0 {
		end = d.Pos
	}
	r := Record{
		Code:     d.Code,
		Severity: sev.String(),
		File:     d.Pos.Filename,
		Range:    Range{Point{d.Pos.Line, d.Pos.Column}, Point{end.Line, end.Column}},
		Message:  d.What(),
		Help:     d.Help,
	}
	if d.Code != "" {
		r.URL = diag.URL(d.Code)
	}
	return r
}

// JSON is r as one line.
func (r Record) JSON() string {
	b, err := json.Marshal(r)
	if err != nil {
		panic(fmt.Sprint("check: ", err))
	}
	return string(b)
}
