package compile

import (
	"os"
	"regexp"
	"strings"
	"testing"
)

// The Asuka in docs/data.md compiles: its blocks, as one program (their
// imports first).
func TestDataDocCompiles(t *testing.T) {
	b, err := os.ReadFile("../../docs/data.md")
	if err != nil {
		t.Fatal(err)
	}
	var imports, body []string
	seen := map[string]bool{}
	for _, m := range regexp.MustCompile("(?s)```asuka\n(.*?)```").FindAllStringSubmatch(string(b), -1) {
		for _, line := range strings.Split(m[1], "\n") {
			if strings.HasPrefix(line, "import ") {
				if !seen[line] {
					seen[line] = true
					imports = append(imports, line)
				}
				continue
			}
			body = append(body, line)
		}
	}
	src := strings.Join(imports, "\n") + "\n\n" + strings.Join(body, "\n") + "\n"
	if res := Files(map[string][]byte{"data.asuka": []byte(src)}, nil); res.Err() != nil {
		t.Fatalf("docs/data.md's Asuka:\n%v", res.Err())
	}
}
