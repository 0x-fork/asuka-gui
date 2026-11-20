package syntax

import (
	"os"
	"regexp"
	"slices"
	"testing"
)

// The tree-sitter grammar (editors/tree-sitter-asuka, which Zed's
// extension highlights with) has the scanner's keywords, soft where the
// parser's are, and Zed's highlights are the grammar's.
func TestTreeSitterGrammar(t *testing.T) {
	const dir = "../../editors/tree-sitter-asuka/"
	b, err := os.ReadFile(dir + "grammar.js")
	if err != nil {
		t.Fatal(err)
	}
	src := string(b)
	list := func(name string) []string {
		m := regexp.MustCompile(`(?s)const ` + name + ` = \[(.*?)\];`).FindStringSubmatch(src)
		if m == nil {
			t.Fatalf("grammar.js has no %s", name)
		}
		var words []string
		for _, w := range regexp.MustCompile(`'([a-z]+)'`).FindAllStringSubmatch(m[1], -1) {
			words = append(words, w[1])
		}
		slices.Sort(words)
		return words
	}
	var hard, soft []string
	for kw := keywordBeg + 1; kw < keywordEnd; kw++ {
		if softKeyword(kw) {
			soft = append(soft, kw.String())
		} else {
			hard = append(hard, kw.String())
		}
	}
	slices.Sort(hard)
	slices.Sort(soft)
	if got := list("HARD_KEYWORDS"); !slices.Equal(got, hard) {
		t.Errorf("grammar.js's HARD_KEYWORDS are\n%v, the scanner's keywords that aren't soft\n%v", got, hard)
	}
	if got := list("SOFT_KEYWORDS"); !slices.Equal(got, soft) {
		t.Errorf("grammar.js's SOFT_KEYWORDS are\n%v, the parser's soft keywords\n%v", got, soft)
	}

	grammar, err := os.ReadFile(dir + "queries/highlights.scm")
	if err != nil {
		t.Fatal(err)
	}
	zed, err := os.ReadFile("../../editors/zed/languages/asuka/highlights.scm")
	if err != nil {
		t.Fatal(err)
	}
	if string(grammar) != string(zed) {
		t.Error("editors/zed/languages/asuka/highlights.scm isn't editors/tree-sitter-asuka/queries/highlights.scm: copy it")
	}
}
