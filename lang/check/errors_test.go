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
// on with // ERROR "regexp" (as Go's test suite does); every diagnostic
// must be expected, and every expectation met.
func TestErrors(t *testing.T) {
	files, _ := filepath.Glob("testdata/errors/*.asuka")
	if len(files) == 0 {
		t.Fatal("no files")
	}
	re := regexp.MustCompile(`// ERROR "((?:[^"\\]|\\.)*)"`)
	for _, path := range files {
		path := path
		t.Run(filepath.Base(path), func(t *testing.T) {
			src, err := os.ReadFile(path)
			if err != nil {
				t.Fatal(err)
			}
			want := map[int]*regexp.Regexp{}
			for i, line := range strings.Split(string(src), "\n") {
				if m := re.FindStringSubmatch(line); m != nil {
					pat, err := strconv.Unquote(`"` + m[1] + `"`)
					if err != nil {
						pat = m[1]
					}
					want[i+1] = regexp.MustCompile(pat)
				}
			}
			_, diags := checkSrc(t, map[string]string{path: string(src)})
			met := map[int]bool{}
			for _, d := range diags {
				w := want[d.Pos.Line]
				if w == nil {
					t.Errorf("unexpected: %s", d)
					continue
				}
				if w.MatchString(d.Msg) {
					met[d.Pos.Line] = true
				} else if !met[d.Pos.Line] {
					t.Errorf("%s: want a match of %q", d, w)
				}
			}
			for line, w := range want {
				if !met[line] {
					t.Errorf("%s:%d: no diagnostic matching %q", path, line, w)
				}
			}
		})
	}
}
