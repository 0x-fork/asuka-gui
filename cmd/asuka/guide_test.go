package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/guide"
	"github.com/egoist/asuka/lang/diag"
)

// asuka guide's pages, embedded in the CLI, are what the docs and the
// diagnostics' table make now: go generate ./cmd/asuka writes them.
func TestGuideCurrent(t *testing.T) {
	root := filepath.Join("..", "..")
	for _, lang := range diag.Languages {
		want, err := guide.Build(root, lang)
		if err != nil {
			t.Fatal(err)
		}
		got, err := guides.ReadFile("guide/" + lang + ".md")
		if err != nil || string(got) != want {
			t.Errorf("cmd/asuka/guide/%s.md isn't current: go generate ./cmd/asuka", lang)
		}
	}
	en, _ := guides.ReadFile("guide/en.md")
	for _, part := range []string{"## Workflow", "asuka check --json", "### A tour of Asuka", "### Components", "### Tests", "## Diagnostic codes", "- write-in-binding: state written where the UI follows it."} {
		if !strings.Contains(string(en), part) {
			t.Errorf("the guide has no %q", part)
		}
	}
	if strings.Contains(string(en), "```asuka check") || strings.Contains(string(en), "](../") {
		t.Error("the guide has the docs' own markup: checked fences, relative links")
	}
}

// asuka new writes AGENTS.md beside the project's files, for an app, a
// program and a library; never into a directory that has files.
func TestNewAgentsMD(t *testing.T) {
	for _, args := range [][]string{{}, {"--bin"}, {"--lib"}} {
		dir := filepath.Join(t.TempDir(), "proj")
		if err := cmdNew(append(args, dir)); err != nil {
			t.Fatal(err)
		}
		b, err := os.ReadFile(filepath.Join(dir, "AGENTS.md"))
		if err != nil {
			t.Fatalf("%v: %v", args, err)
		}
		if !strings.Contains(string(b), "`asuka guide`") || !strings.Contains(string(b), "asuka explain <code>") {
			t.Errorf("%v: AGENTS.md:\n%s", args, b)
		}
	}
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, "AGENTS.md"), []byte("mine"), 0o644)
	if err := cmdNew([]string{dir}); err == nil {
		t.Error("asuka new in a directory with files")
	}
	if b, _ := os.ReadFile(filepath.Join(dir, "AGENTS.md")); string(b) != "mine" {
		t.Errorf("a project's own AGENTS.md: %s", b)
	}
}
