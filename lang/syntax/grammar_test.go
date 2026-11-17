package syntax

import (
	"os"
	"regexp"
	"slices"
	"strings"
	"testing"
)

// GRAMMAR.md lists the keywords the scanner has, every decision of
// parser.go it names is one, and its Asuka parses.
func TestGrammarDoc(t *testing.T) {
	b, err := os.ReadFile("GRAMMAR.md")
	if err != nil {
		t.Fatal(err)
	}
	doc := string(b)
	_, list, _ := strings.Cut(doc, "### Keywords\n\n```\n")
	list, _, _ = strings.Cut(list, "```")
	var want []string
	for kw := keywordBeg + 1; kw < keywordEnd; kw++ {
		want = append(want, kw.String())
	}
	slices.Sort(want)
	if got := strings.Fields(list); !slices.Equal(got, want) {
		t.Errorf("GRAMMAR.md's keywords are\n%v, the scanner's\n%v", got, want)
	}
	src, err := os.ReadFile("parser.go")
	if err != nil {
		t.Fatal(err)
	}
	for _, m := range regexp.MustCompile(`decisions? (\d+)`).FindAllStringSubmatch(doc, -1) {
		if !regexp.MustCompile(`(?m)^//\s+` + m[1] + `\. `).Match(src) {
			t.Errorf("GRAMMAR.md names decision %s, which parser.go hasn't", m[1])
		}
	}
	for _, part := range strings.Split(doc, "```asuka\n")[1:] {
		code, _, _ := strings.Cut(part, "```")
		if _, errs := ParseFile(NewFileSet(), "grammar.asuka", []byte(code)); len(errs) > 0 {
			t.Errorf("%q: %v", code, errs)
		}
	}
}
