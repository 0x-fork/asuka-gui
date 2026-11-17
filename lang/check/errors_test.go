package check

import (
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"testing"
)

// Each testdata/errors/*.asuka marks the lines it expects a diagnostic
// on with // ERROR "regexp" (as Go's test suite does), and a warning
// with // WARNING "regexp"; every diagnostic must be expected, and every
// expectation met.
func TestErrors(t *testing.T) {
	files, _ := filepath.Glob("testdata/errors/*.asuka")
	if len(files) == 0 {
		t.Fatal("no files")
	}
	re := regexp.MustCompile(`// (ERROR|WARNING) "((?:[^"\\]|\\.)*)"`)
	for _, path := range files {
		path := path
		t.Run(filepath.Base(path), func(t *testing.T) {
			src, err := os.ReadFile(path)
			if err != nil {
				t.Fatal(err)
			}
			want := map[int]*regexp.Regexp{}
			wantWarn := map[int]*regexp.Regexp{}
			for i, line := range strings.Split(string(src), "\n") {
				if m := re.FindStringSubmatch(line); m != nil {
					pat, err := strconv.Unquote(`"` + m[2] + `"`)
					if err != nil {
						pat = m[2]
					}
					if m[1] == "WARNING" {
						wantWarn[i+1] = regexp.MustCompile(pat)
					} else {
						want[i+1] = regexp.MustCompile(pat)
					}
				}
			}
			prog, diags := checkSrc(t, map[string]string{path: string(src)})
			match := func(diags []Diagnostic, want map[int]*regexp.Regexp, what string) {
				met := map[int]bool{}
				for _, d := range diags {
					w := want[d.Pos.Line]
					if w == nil {
						t.Errorf("unexpected %s: %s", what, d)
						continue
					}
					if w.MatchString(d.Msg) {
						met[d.Pos.Line] = true
					} else {
						// A second diagnostic on a line is a cascade
						// unless it's expected too.
						t.Errorf("%s: want a %s matching %q", d, what, w)
					}
				}
				for line, w := range want {
					if !met[line] {
						t.Errorf("%s:%d: no %s matching %q", path, line, what, w)
					}
				}
			}
			match(diags, want, "error")
			match(prog.Warnings, wantWarn, "warning")
		})
	}
}
