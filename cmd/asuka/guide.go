package main

//go:generate go run ../../internal/guide/gen

import (
	"embed"
	"fmt"
	"os"
	"slices"

	"github.com/egoist/asuka/lang/diag"
)

// guides are asuka guide's pages, one per language, made from the docs
// and the diagnostics' table as the CLI was built (go generate
// ./cmd/asuka; TestGuideCurrent checks them).
//
//go:embed guide/*.md
var guides embed.FS

// cmdGuide is asuka guide: the language on one page, for this CLI's
// version, with the workflow and every diagnostic code: what a coding
// agent reads before it writes Asuka (asuka new's AGENTS.md says so).
func cmdGuide(args []string) error {
	var lang string
	fs := newFlagSet("guide", "")
	fs.about = "Prints the Asuka language on one page, for the version of this CLI: a tour and the whole\n" +
		"reference, how to check, test and run a project, and every diagnostic code's summary and fix.\n" +
		"What a coding agent reads before it writes Asuka."
	fs.String(&lang, "lang", 0, "", "the `language`: en, zh or ja (default: LC_ALL, LC_MESSAGES or LANG's)")
	if err := fs.parse(args); err != nil {
		return err
	}
	if lang == "" {
		lang = envLang()
	} else if !slices.Contains(diag.Languages, lang) {
		return fmt.Errorf("--lang %s: en, zh or ja", lang)
	}
	b, err := guides.ReadFile("guide/" + lang + ".md")
	if err != nil {
		return err
	}
	fmt.Printf("<!-- asuka %s -->\n", version)
	_, err = os.Stdout.Write(b)
	return err
}
