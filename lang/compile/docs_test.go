package compile

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"testing/fstest"
)

// docs/i18n.md's examples compile, against the catalog it shows (and the
// few keys it names besides).
func TestI18nDocExamples(t *testing.T) {
	doc, err := os.ReadFile("../../docs/i18n.md")
	if err != nil {
		t.Fatal(err)
	}
	blocks := func(lang string) []string {
		var out []string
		for _, part := range strings.Split(string(doc), "```"+lang+"\n")[1:] {
			out = append(out, part[:strings.Index(part, "```")])
		}
		return out
	}
	// The catalog: the JSON example with a source's metadata.
	var catalog map[string]any
	for _, b := range blocks("json") {
		if strings.Contains(b, `"@@locale": "en"`) {
			if err := json.Unmarshal([]byte(b), &catalog); err != nil {
				t.Fatal(err)
			}
		}
	}
	if catalog == nil {
		t.Fatal("no source catalog in docs/i18n.md")
	}
	for k, v := range map[string]string{"inboxTitle": "Inbox", "allRead": "All read", "language": "Language", "systemLanguage": "As the system's", "previous": "Previous", "play": "Play", "next": "Next"} {
		catalog[k] = v
	}
	data, _ := json.Marshal(catalog)
	cats := &Catalogs{FS: fstest.MapFS{"en.json": {Data: data}}, Dir: "locales", Source: "en"}
	examples := blocks("asuka")
	if len(examples) < 6 {
		t.Fatalf("%d examples", len(examples))
	}
	for _, src := range examples {
		if !strings.Contains(src, "app {") {
			src += "\napp { Text(\"x\") }\n"
		}
		res := Files(map[string][]byte{"example.asuka": []byte(src)}, &Options{Catalogs: cats})
		if err := res.Err(); err != nil {
			t.Errorf("%s\n%v", src, err)
		}
	}
}

// The Asuka examples of the pages that show the standard library's
// packages compile, in every language's docs. Those of eachBlock compile
// each on its own; a page of onePage builds one program as it goes, its
// blocks compiled together (their imports first).
func TestDocExamples(t *testing.T) {
	eachBlock := []string{"language-tour.md", "device.md", "components.md", "accessibility.md", "motion.md", "desktop.md", "navigation.md", "web.md", "programs.md"}
	onePage := []string{"data.md", "purchases.md", "nfc.md", "bluetooth.md", "icloud.md"}
	for _, lang := range []string{"", "zh", "ja"} {
		for _, page := range eachBlock {
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
		for _, page := range onePage {
			path := filepath.Join("..", "..", "docs", lang, page)
			b, err := os.ReadFile(path)
			if err != nil {
				t.Fatal(err)
			}
			t.Run(path, func(t *testing.T) {
				var imports, body []string
				seen := map[string]bool{}
				for _, ex := range asukaBlocks(string(b)) {
					for _, line := range strings.Split(ex.code, "\n") {
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
				if len(body) == 0 {
					t.Fatalf("%s has no Asuka", path)
				}
				src := strings.Join(imports, "\n") + "\n\n" + strings.Join(body, "\n") + "\n"
				if res := Files(map[string][]byte{"page.asuka": []byte(src)}, nil); res.Err() != nil {
					t.Errorf("%s's Asuka doesn't compile:\n%v", path, res.Err())
				}
			})
		}
	}
}

// The updater's examples (platform/update) in the shipping and desktop
// guides compile, in every language's docs.
func TestUpdateDocExamples(t *testing.T) {
	n := 0
	for _, lang := range []string{"", "zh", "ja"} {
		for _, page := range []string{"shipping.md", "desktop.md"} {
			path := filepath.Join("..", "..", "docs", lang, page)
			b, err := os.ReadFile(path)
			if err != nil {
				t.Fatal(err)
			}
			for _, ex := range asukaBlocks(string(b)) {
				if !strings.Contains(ex.code, "import platform/update") {
					continue
				}
				n++
				res := Files(map[string][]byte{"example.asuka": []byte(ex.code)}, nil)
				if err := res.Err(); err != nil {
					t.Errorf("%s:%d: the example doesn't compile:\n%v", path, ex.line, err)
				}
			}
		}
	}
	if n < 9 {
		t.Errorf("%d examples of platform/update, want 3 in each language", n)
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
