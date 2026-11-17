package compile

import (
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"testing"
	"unicode"

	"github.com/egoist/asuka/lang/doc"
	"github.com/egoist/asuka/std"
)

// referencePage is the Markdown of the API reference's page a docs link
// names (docs/std/time.md, docs/zh/platform/camera.md: asuka doc's, which
// the website serves at /docs/std/time; docs/std.md, the list, which has
// no anchors to check), and whether it is one.
func referencePage(path string) (string, bool) {
	slash := filepath.ToSlash(path)
	i := strings.LastIndex(slash, "/docs/")
	if i < 0 || !strings.HasSuffix(slash, ".md") {
		return "", false
	}
	rel := strings.TrimSuffix(slash[i+len("/docs/"):], ".md")
	rel = strings.TrimPrefix(strings.TrimPrefix(rel, "zh/"), "ja/")
	if rel == "std" || rel == "platform" {
		return "", true // the list of the set's packages
	}
	if !strings.HasPrefix(rel, "std/") && !strings.HasPrefix(rel, "platform/") {
		return "", false
	}
	if p, ok := doc.Builtin(rel); ok {
		return p.Markdown(), true
	}
	dir, ok := std.Package(rel)
	if !ok {
		return "", false
	}
	p, err := doc.Load(dir, rel)
	if err != nil {
		return "", false
	}
	return p.Markdown(), true
}

// The docs' links resolve, in every language: each relative link names a
// file of the repository, and each #anchor a heading of its page (as
// GitHub makes a heading's anchor, which the website follows). The docs'
// pages in each language have the same headings, in the same order and
// at the same levels, as the English ones.

// docSubdirs are the docs' directories of pages, in each language: the
// guides, and the language reference.
var docSubdirs = []string{"", "language"}

// docPages are the pages checked: the docs in every language and the
// README.
func docPages(t *testing.T) []string {
	t.Helper()
	pages := []string{filepath.Join("..", "..", "README.md")}
	for _, dir := range []string{"", "zh", "ja"} {
		for _, sub := range docSubdirs {
			ms, err := filepath.Glob(filepath.Join("..", "..", "docs", dir, sub, "*.md"))
			if err != nil {
				t.Fatal(err)
			}
			pages = append(pages, ms...)
		}
	}
	return pages
}

var (
	mdLink     = regexp.MustCompile(`!?\[(?:[^\[\]]|\[[^\]]*\])*\]\(([^)\s]+)(?:\s+"[^"]*")?\)`)
	inlineCode = regexp.MustCompile("`[^`]*`")
	htmlAnchor = regexp.MustCompile(`<a\s+(?:id|name)="([^"]+)"`)
	mdHeading  = regexp.MustCompile(`^(#{1,6})\s+(.*?)\s*#*\s*$`)
)

// mdLines are a page's lines outside its fenced code blocks, with their
// numbers.
func mdLines(src string) (lines []string, nums []int) {
	fence := ""
	for i, l := range strings.Split(src, "\n") {
		trim := strings.TrimLeft(l, " ")
		if fence != "" {
			if strings.HasPrefix(trim, fence) && strings.TrimSpace(strings.TrimLeft(trim, fence[:1])) == "" {
				fence = ""
			}
			continue
		}
		if strings.HasPrefix(trim, "```") || strings.HasPrefix(trim, "~~~") {
			n := len(trim) - len(strings.TrimLeft(trim, trim[:1]))
			fence = strings.Repeat(trim[:1], n)
			continue
		}
		lines = append(lines, l)
		nums = append(nums, i+1)
	}
	return lines, nums
}

// mdHeadings are a page's headings: their levels and text.
func mdHeadings(src string) (levels []int, texts []string) {
	lines, _ := mdLines(src)
	for _, l := range lines {
		if m := mdHeading.FindStringSubmatch(l); m != nil {
			levels = append(levels, len(m[1]))
			texts = append(texts, m[2])
		}
	}
	return levels, texts
}

// slug is a heading's anchor, as GitHub makes it: its text without
// markup, lower case, without punctuation, its spaces hyphens.
func slug(heading string) string {
	s := mdLink.ReplaceAllStringFunc(heading, func(m string) string {
		if i := strings.Index(m, "]("); i > 0 {
			return m[strings.Index(m, "[")+1 : i]
		}
		return m
	})
	var b strings.Builder
	for _, r := range strings.ToLower(s) {
		switch {
		case r == ' ':
			b.WriteRune('-')
		case r == '-' || r == '_' || unicode.IsLetter(r) || unicode.IsNumber(r) || unicode.Is(unicode.Mn, r):
			b.WriteRune(r)
		}
	}
	return b.String()
}

// anchors are a page's anchors: its headings' (a repeated one's second
// is -1, as GitHub numbers them) and its HTML anchors.
func anchors(src string) map[string]bool {
	out := map[string]bool{}
	_, texts := mdHeadings(src)
	seen := map[string]int{}
	for _, h := range texts {
		s := slug(h)
		if n := seen[s]; n > 0 {
			out[s+"-"+strconv.Itoa(n)] = true
		} else {
			out[s] = true
		}
		seen[s]++
	}
	for _, m := range htmlAnchor.FindAllStringSubmatch(src, -1) {
		out[m[1]] = true
	}
	return out
}

func TestDocLinks(t *testing.T) {
	cache := map[string]map[string]bool{}
	anchorsOf := func(path string) (map[string]bool, error) {
		if a, ok := cache[path]; ok {
			return a, nil
		}
		text, ok := referencePage(path)
		if !ok {
			b, err := os.ReadFile(path)
			if err != nil {
				return nil, err
			}
			text = string(b)
		}
		cache[path] = anchors(text)
		return cache[path], nil
	}
	for _, page := range docPages(t) {
		b, err := os.ReadFile(page)
		if err != nil {
			t.Fatal(err)
		}
		lines, nums := mdLines(string(b))
		for i, l := range lines {
			l = inlineCode.ReplaceAllString(l, "")
			for _, m := range mdLink.FindAllStringSubmatch(l, -1) {
				target := m[1]
				if strings.Contains(target, "://") || strings.HasPrefix(target, "mailto:") {
					continue
				}
				file, frag, _ := strings.Cut(target, "#")
				path := page
				if file != "" {
					path = filepath.Join(filepath.Dir(page), filepath.FromSlash(file))
					if _, ok := referencePage(path); ok {
						// A page of the API reference, generated.
					} else if _, err := os.Stat(path); err != nil {
						t.Errorf("%s:%d: %s: no such file", page, nums[i], target)
						continue
					}
				}
				if frag == "" || !strings.HasSuffix(path, ".md") {
					continue
				}
				as, err := anchorsOf(path)
				if err != nil {
					t.Errorf("%s:%d: %s: %v", page, nums[i], target, err)
					continue
				}
				if !as[frag] {
					t.Errorf("%s:%d: %s: no heading #%s in %s", page, nums[i], target, frag, path)
				}
			}
		}
	}
}

// The translations have the English pages' headings: as many, at the
// same levels, in the same order.
func TestDocHeadings(t *testing.T) {
	var en []string
	for _, sub := range docSubdirs {
		ms, err := filepath.Glob(filepath.Join("..", "..", "docs", sub, "*.md"))
		if err != nil {
			t.Fatal(err)
		}
		en = append(en, ms...)
	}
	for _, page := range en {
		b, err := os.ReadFile(page)
		if err != nil {
			t.Fatal(err)
		}
		levels, texts := mdHeadings(string(b))
		for _, lang := range []string{"zh", "ja"} {
			rel, _ := filepath.Rel(filepath.Join("..", "..", "docs"), page)
			tr := filepath.Join("..", "..", "docs", lang, rel)
			tb, err := os.ReadFile(tr)
			if os.IsNotExist(err) {
				continue // a page only in English (device-checklist.md)
			}
			if err != nil {
				t.Fatal(err)
			}
			tl, tt := mdHeadings(string(tb))
			if len(tl) != len(levels) {
				t.Errorf("%s has %d headings, %s %d", tr, len(tl), page, len(levels))
			}
			for i := range min(len(tl), len(levels)) {
				if tl[i] != levels[i] {
					t.Errorf("%s: heading %d, %q, is level %d; in %s, %q is %d", tr, i+1, tt[i], tl[i], page, texts[i], levels[i])
					break
				}
			}
		}
	}
}
