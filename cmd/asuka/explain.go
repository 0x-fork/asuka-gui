package main

import (
	"fmt"
	"io"
	"os"
	"slices"
	"sort"
	"strings"

	"github.com/egoist/asuka/lang/diag"
)

// cmdExplain is asuka explain: a diagnostic's code's page (lang/diag), the
// same as asuka.egoist.dev/docs/errors/<code>, in the language the environment
// asks for; without a code, every code and its summary.
func cmdExplain(args []string) error {
	var lang string
	fs := newFlagSet("explain", "[code]")
	fs.about = "Prints the page of a diagnostic's code (error[code] in asuka check's output): why the\n" +
		"rule is there, how to fix what breaks it, and a program that breaks it and one that doesn't.\n" +
		"Without a code, lists them all. The pages are the CLI's own, for its version: no network."
	fs.String(&lang, "lang", 0, "", "the `language` of the page: en, zh or ja (default: LC_ALL, LC_MESSAGES or LANG's)")
	if err := fs.parse(args); err != nil {
		return err
	}
	if lang == "" {
		lang = envLang()
	} else if !slices.Contains(diag.Languages, lang) {
		return fmt.Errorf("--lang %s: en, zh or ja", lang)
	}
	return explain(os.Stdout, fs.Arg(0), lang)
}

func explain(w io.Writer, name, lang string) error {
	name = strings.TrimSuffix(strings.TrimPrefix(strings.TrimPrefix(name, "error["), "warning["), "]")
	if name == "" {
		for _, c := range diag.All() {
			fmt.Fprintf(w, "%-30s %s\n", c.Name, c.Text(lang).Summary)
		}
		return nil
	}
	c := diag.Lookup(name)
	if c == nil {
		msg := "no code " + name
		if near := nearCodes(name); len(near) > 0 {
			msg += ": did you mean " + strings.Join(near, ", ") + "?"
		}
		return fmt.Errorf("%s (asuka explain lists them)", msg)
	}
	fmt.Fprint(w, diag.Page(c, lang))
	return nil
}

// envLang is the language the environment asks for, as POSIX orders it:
// LC_ALL, then LC_MESSAGES, then LANG.
func envLang() string {
	for _, v := range []string{"LC_ALL", "LC_MESSAGES", "LANG"} {
		if s := os.Getenv(v); s != "" {
			return diag.Lang(s)
		}
	}
	return "en"
}

// nearCodes are the codes closest to name: those it's part of or that
// are part of it, else those a couple of edits away.
func nearCodes(name string) []string {
	type near struct {
		name string
		d    int
	}
	var out []near
	for _, c := range diag.All() {
		d := editDistance(name, c.Name)
		if strings.Contains(c.Name, name) || strings.Contains(name, c.Name) {
			d = 0
		}
		if d <= 3 {
			out = append(out, near{c.Name, d})
		}
	}
	sort.SliceStable(out, func(i, j int) bool { return out[i].d < out[j].d })
	var names []string
	for i, n := range out {
		if i == 3 {
			break
		}
		names = append(names, n.name)
	}
	return names
}

func editDistance(a, b string) int {
	prev := make([]int, len(b)+1)
	for j := range prev {
		prev[j] = j
	}
	for i := 1; i <= len(a); i++ {
		cur := make([]int, len(b)+1)
		cur[0] = i
		for j := 1; j <= len(b); j++ {
			cost := 1
			if a[i-1] == b[j-1] {
				cost = 0
			}
			cur[j] = min(prev[j]+1, cur[j-1]+1, prev[j-1]+cost)
		}
		prev = cur
	}
	return prev[len(b)]
}
