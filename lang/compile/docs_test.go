package compile

import (
	"encoding/json"
	"os"
	"regexp"
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
