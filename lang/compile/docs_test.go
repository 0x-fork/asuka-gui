package compile

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// The Asuka examples of the pages that show the standard library's
// packages compile, each on its own, in every language's docs.
func TestDocExamples(t *testing.T) {
	for _, lang := range []string{"", "zh", "ja"} {
		for _, page := range []string{"device.md", "components.md"} {
			path := filepath.Join("..", "..", "docs", lang, page)
			b, err := os.ReadFile(path)
			if err != nil {
				t.Fatal(err)
			}
			for i, ex := range asukaBlocks(string(b)) {
				name := fmt.Sprintf("%s#%d", path, i+1)
				t.Run(name, func(t *testing.T) {
					res := Files(map[string][]byte{"example.asuka": []byte(ex.code)}, nil)
					if err := res.Err(); err != nil {
						t.Errorf("%s:%d: the example doesn't compile:\n%v", path, ex.line, err)
					}
				})
			}
		}
	}
}

type example struct {
	code string
	line int
}

// asukaBlocks are a Markdown page's ```asuka blocks.
func asukaBlocks(md string) []example {
	var out []example
	lines := strings.Split(md, "\n")
	for i := 0; i < len(lines); i++ {
		if strings.TrimSpace(lines[i]) != "```asuka" {
			continue
		}
		start := i + 1
		j := start
		for j < len(lines) && strings.TrimSpace(lines[j]) != "```" {
			j++
		}
		out = append(out, example{code: strings.Join(lines[start:j], "\n") + "\n", line: start + 1})
		i = j
	}
	return out
}
