package lsp

import (
	"regexp"
)

// Quick fixes: what the checker's messages say would fix them.

var didYouMean = regexp.MustCompile(`\(did you mean ([A-Za-z_][A-Za-z0-9_]*)\?\)`)

func (s *server) codeAction(p *CodeActionParams) ([]CodeAction, error) {
	actions := []CodeAction{}
	path := uriToPath(p.TextDocument.URI)
	src, err := s.readFile(path)
	if err != nil {
		return actions, nil
	}
	m := newMapper(src, s.utf8)
	for _, d := range p.Context.Diagnostics {
		mm := didYouMean.FindStringSubmatch(d.Message)
		if mm == nil {
			continue
		}
		// The diagnostic is on the misspelled name: replace it.
		a, b := m.offset(d.Range.Start), m.offset(d.Range.End)
		if a >= b || b > len(src) || wordEnd(src, a) != b || string(src[a:b]) == mm[1] {
			continue
		}
		actions = append(actions, CodeAction{
			Title:       "Change to " + mm[1],
			Kind:        "quickfix",
			Diagnostics: []Diagnostic{d},
			IsPreferred: true,
			Edit:        &WorkspaceEdit{Changes: map[string][]TextEdit{p.TextDocument.URI: {{Range: d.Range, NewText: mm[1]}}}},
		})
	}
	return actions, nil
}
