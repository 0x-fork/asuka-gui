package diag_test

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"

	"github.com/egoist/asuka/lang/compile"
	"github.com/egoist/asuka/lang/diag"
)

var kebab = regexp.MustCompile(`^[a-z]+(-[a-z]+)*$`)

// Every code is named in kebab-case and has its summary, why and fix in
// every language, and examples unless it's a compiler bug's.
func TestTable(t *testing.T) {
	codes := diag.All()
	if len(codes) < 100 {
		t.Fatalf("%d codes", len(codes))
	}
	for _, c := range codes {
		if !kebab.MatchString(c.Name) {
			t.Errorf("%s: not kebab-case", c.Name)
		}
		if c.Summary == "" || c.Why == "" || c.Fix == "" {
			t.Errorf("%s: its summary, why and fix", c.Name)
		}
		if strings.HasSuffix(c.Summary, ".") || strings.Contains(c.Summary, "\n") {
			t.Errorf("%s: a summary is a line, without a period: %q", c.Name, c.Summary)
		}
		for _, lang := range diag.Languages {
			if !c.Translated(lang) {
				t.Errorf("%s: no %s text", c.Name, lang)
			}
		}
		if c.Internal != (c.Wrong == "") || c.Internal != (c.Right == "") {
			t.Errorf("%s: examples, unless it's internal", c.Name)
		}
	}
}

func write(t *testing.T, src string) string {
	t.Helper()
	dir := t.TempDir()
	for name, text := range diag.Files(src) {
		p := filepath.Join(dir, filepath.FromSlash(name))
		if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(p, []byte(text), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	return dir
}

// check is what asuka check reports of the example, for every platform:
// each diagnostic as "code: message".
func check(t *testing.T, src string) []string {
	t.Helper()
	dir := write(t, src)
	res, err := compile.DirPlatforms(dir, nil, nil)
	if err != nil {
		return []string{"error: " + err.Error()}
	}
	var out []string
	for _, d := range append(res.Diags, res.Warnings...) {
		if r, err := filepath.Rel(dir, d.Pos.Filename); err == nil {
			d.Pos.Filename = r
		}
		out = append(out, fmt.Sprintf("%s: %s", d.Code, d))
	}
	return out
}

// Each code's wrong example is reported with that code, and only that
// code; its right one compiles without a diagnostic. Both are checked
// for every platform, as asuka check does.
func TestExamples(t *testing.T) {
	for _, c := range diag.All() {
		if c.Internal {
			continue
		}
		t.Run(c.Name, func(t *testing.T) {
			t.Parallel()
			if c.RunTime {
				// What the core reports as it runs: both compile clean
				// (TestRunTimeExamples runs them).
				for _, src := range []string{c.Wrong, c.Right} {
					for _, d := range check(t, src) {
						t.Errorf("%s", d)
					}
				}
				return
			}
			got := check(t, c.Wrong)
			if len(got) == 0 {
				t.Errorf("the wrong example compiles:\n%.400s", c.Wrong)
			}
			for _, d := range got {
				if !strings.HasPrefix(d, c.Name+": ") {
					t.Errorf("the wrong example: %.300s", d)
				}
			}
			for _, d := range check(t, c.Right) {
				t.Errorf("the right example: %s", d)
			}
		})
	}
}

func TestSplit(t *testing.T) {
	for _, x := range []struct {
		format     string
		args       []any
		what, help string
	}{
		{"can't assign to %s here: change state in a handler", []any{"n"}, "can't assign to n here", "change state in a handler"},
		{"%s isn't optional: ?? has nothing to do", []any{"int"}, "int isn't optional", "?? has nothing to do"},
		{"parameter %s needs a type: %s: Type", []any{"x", "x"}, "parameter x needs a type", "x: Type"},
		{"%s(%q): %v", []any{"embed.text", "a", "no"}, `embed.text("a"): no`, ""},
		{"%s: this package has no directory", []any{"embed.text"}, "embed.text: this package has no directory", ""},
		{"unknown name %s%s", []any{"x", ": did you mean y?"}, "unknown name x: did you mean y?", ""},
		{"%s", []any{"rows are kept by position: say key x.id"}, "rows are kept by position", "say key x.id"},
		{"50%% done: %s", []any{"go"}, "50% done", "go"},
	} {
		what, help := diag.Split(x.format, x.args...)
		if what != x.what || help != x.help {
			t.Errorf("Split(%q): %q, %q; want %q, %q", x.format, what, help, x.what, x.help)
		}
	}
}

func TestLang(t *testing.T) {
	for in, want := range map[string]string{"zh_CN.UTF-8": "zh", "ja_JP.UTF-8": "ja", "en_US.UTF-8": "en", "": "en", "C": "en", "zh-Hant": "zh"} {
		if got := diag.Lang(in); got != want {
			t.Errorf("Lang(%q) = %q, want %q", in, got, want)
		}
	}
}

// The generated pages are current: go generate ./lang/diag wrote what the
// table says, and nothing else is in their directories.
func TestPagesCurrent(t *testing.T) {
	root := filepath.Join("..", "..")
	pages := diag.Pages()
	for name, want := range pages {
		got, err := os.ReadFile(filepath.Join(root, filepath.FromSlash(name)))
		if err != nil || string(got) != want {
			t.Errorf("%s isn't current: go generate ./lang/diag", name)
		}
	}
	for _, dir := range []string{"docs/errors", "docs/zh/errors", "docs/ja/errors"} {
		files, _ := filepath.Glob(filepath.Join(root, dir, "*.md"))
		for _, f := range files {
			rel, _ := filepath.Rel(root, f)
			if _, ok := pages[filepath.ToSlash(rel)]; !ok {
				t.Errorf("%s: no code has it (go generate ./lang/diag removes it)", rel)
			}
		}
	}
}
