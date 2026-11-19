package diag

import (
	"fmt"
	"path"
	"sort"
	"strings"
	"unicode"
	"unicode/utf8"
)

// words are a page's own words, in each language.
type words struct {
	why, fix, wrong, right string
	compileError, warning  string
	panic, detector        string
	title, intro           string
	compileTime, runTime   string
}

var pageWords = map[string]words{
	"en": {
		why: "Why", fix: "How to fix", wrong: "Wrong", right: "Right",
		compileError: "An error the compiler reports: the program doesn't build until it's fixed.",
		warning:      "A warning the compiler reports: the program builds, and it's worth a look.",
		panic:        "A panic of the core as the app runs, in every build: what was running stops, and a crash report says this code.",
		detector:     "Reported by the core as the app runs, in development builds (`asuka dev`, `asuka run`, `asuka test`), once per place in the code: release builds don't check it.",
		title:        "Error codes",
		intro: "Every error and warning Asuka reports has a code: a stable name, said in the terminal (`error[write-in-binding]`), " +
			"by `asuka check --json` and `asuka test --json`, and by the editor, which links it here. " +
			"Each page says why the rule is there, how to fix what breaks it, and shows a program that breaks it and one that doesn't. " +
			"`asuka explain <code>` prints the page in a terminal, for the version installed.",
		compileTime: "The compiler's",
		runTime:     "As the app runs",
	},
	"zh": {
		why: "原因", fix: "如何修正", wrong: "错误示例", right: "正确示例",
		compileError: "编译器报告的错误：修正之前程序无法构建。",
		warning:      "编译器报告的警告：程序仍能构建，但值得看一看。",
		panic:        "应用运行时核心触发的 panic，任何构建都会检查：正在运行的部分会停止，崩溃报告里会带上这个代码。",
		detector:     "应用运行时由核心报告，只在开发构建中检查（`asuka dev`、`asuka run`、`asuka test`），代码里的每个位置只报告一次；发布构建不做这项检查。",
		title:        "错误代码",
		intro: "Asuka 报告的每个错误和警告都有一个代码：一个稳定的名称，会出现在终端里（`error[write-in-binding]`）、" +
			"`asuka check --json` 和 `asuka test --json` 的输出里，以及编辑器里，编辑器会链接到这里。" +
			"每一页都说明这条规则为什么存在、违反时如何修正，并给出一个违反它的程序和一个不违反它的程序。" +
			"在终端里运行 `asuka explain <code>` 可以看到与所装版本一致的页面。",
		compileTime: "编译器报告的",
		runTime:     "运行时报告的",
	},
	"ja": {
		why: "理由", fix: "直し方", wrong: "誤った例", right: "正しい例",
		compileError: "コンパイラーが報告するエラーです。直すまでプログラムはビルドできません。",
		warning:      "コンパイラーが報告する警告です。プログラムはビルドできますが、確認する価値があります。",
		panic:        "アプリの実行中にコアが起こすパニックで、どのビルドでも検査されます。実行中の処理は止まり、クラッシュレポートにこのコードが記録されます。",
		detector:     "アプリの実行中にコアが報告します。検査するのは開発ビルド（`asuka dev`、`asuka run`、`asuka test`）だけで、コードの場所ごとに 1 回だけ報告されます。リリースビルドでは検査しません。",
		title:        "エラーコード",
		intro: "Asuka が報告するエラーと警告には、すべてコードがあります。コードは変わらない名前で、ターミナル（`error[write-in-binding]`）、" +
			"`asuka check --json` と `asuka test --json` の出力、エディターに表示され、エディターからはこのページにリンクされます。" +
			"各ページでは、そのルールがある理由と、破ったときの直し方を説明し、ルールを破るプログラムと破らないプログラムを示します。" +
			"ターミナルで `asuka explain <code>` を実行すると、インストールしたバージョンのページが表示されます。",
		compileTime: "コンパイラーが報告するもの",
		runTime:     "実行時に報告されるもの",
	},
}

// Page is c's page in lang, as Markdown: what docs/errors/<code>.md holds
// (docs/zh/errors, docs/ja/errors) and asuka explain prints.
func Page(c *Code, lang string) string {
	w, ok := pageWords[lang]
	if !ok {
		lang, w = "en", pageWords["en"]
	}
	t := c.Text(lang)
	var b strings.Builder
	fmt.Fprintf(&b, "# %s\n\n%s\n\n", c.Name, sentence(t.Summary, lang))
	switch {
	case c.RunTime && c.Panic:
		b.WriteString(w.panic)
	case c.RunTime:
		b.WriteString(w.detector)
	case c.Severity == Warning:
		b.WriteString(w.warning)
	default:
		b.WriteString(w.compileError)
	}
	fmt.Fprintf(&b, "\n\n## %s\n\n%s\n\n## %s\n\n%s\n", w.why, prose(t.Why), w.fix, prose(t.Fix))
	if wrong := c.WrongSource(); wrong != "" {
		fmt.Fprintf(&b, "\n## %s\n\n%s\n\n## %s\n\n%s\n", w.wrong, example(wrong), w.right, example(c.Right))
	}
	return b.String()
}

// Index is the list of the codes in lang: docs/errors/README.md.
func Index(lang string) string {
	w, ok := pageWords[lang]
	if !ok {
		lang, w = "en", pageWords["en"]
	}
	var b strings.Builder
	fmt.Fprintf(&b, "# %s\n\n%s\n", w.title, w.intro)
	for _, runTime := range []bool{false, true} {
		heading := w.compileTime
		if runTime {
			heading = w.runTime
		}
		fmt.Fprintf(&b, "\n## %s\n\n", heading)
		for _, c := range All() {
			if c.RunTime == runTime {
				fmt.Fprintf(&b, "- [%s](%s.md): %s\n", c.Name, c.Name, sentence(c.Text(lang).Summary, lang))
			}
		}
	}
	return b.String()
}

// Pages are the files go generate ./lang/diag writes, by their path in
// the repository: each language's index and its codes' pages.
func Pages() map[string]string {
	out := map[string]string{}
	for _, lang := range Languages {
		dir := "docs/errors"
		if lang != "en" {
			dir = "docs/" + lang + "/errors"
		}
		out[path.Join(dir, "README.md")] = Index(lang)
		for _, c := range All() {
			out[path.Join(dir, c.Name+".md")] = Page(c, lang)
		}
	}
	return out
}

// Line is c in one line, in lang: its name, its summary and its fix's
// first sentence (asuka guide's list).
func Line(c *Code, lang string) string {
	t := c.Text(lang)
	return fmt.Sprintf("%s: %s. %s", c.Name, t.Summary, firstSentence(prose(t.Fix)))
}

// sentence makes a summary a sentence: a capital, and its full stop.
func sentence(s, lang string) string {
	if lang == "zh" || lang == "ja" {
		return s + "。"
	}
	r, n := utf8.DecodeRuneInString(s)
	return string(unicode.ToUpper(r)) + s[n:] + "."
}

// prose is the table's text as Markdown paragraphs: its lines joined.
func prose(s string) string {
	var paras []string
	for _, p := range strings.Split(strings.TrimSpace(s), "\n\n") {
		paras = append(paras, strings.Join(strings.Fields(p), " "))
	}
	return strings.Join(paras, "\n\n")
}

func firstSentence(s string) string {
	for i := 0; i+1 < len(s); i++ {
		if s[i] == '.' && s[i+1] == ' ' {
			return s[:i+1]
		}
	}
	if i := strings.Index(s, "。"); i >= 0 {
		return s[:i+len("。")]
	}
	return s
}

// example is an example as Markdown: its file in a fence, or each of its
// files, named, in one. A long one is shown in part.
func example(src string) string {
	fs := Files(src)
	names := make([]string, 0, len(fs))
	for name := range fs {
		names = append(names, name)
	}
	sort.Slice(names, func(i, j int) bool {
		// app.asuka first, then the rest by name.
		if (names[i] == "app.asuka") != (names[j] == "app.asuka") {
			return names[i] == "app.asuka"
		}
		return names[i] < names[j]
	})
	var b strings.Builder
	for _, name := range names {
		if len(names) > 1 {
			fmt.Fprintf(&b, "`%s`\n\n", name)
		}
		fmt.Fprintf(&b, "```%s\n%s```\n\n", fence(name), excerpt(fs[name]))
	}
	return strings.TrimRight(b.String(), "\n")
}

func fence(name string) string {
	switch path.Ext(name) {
	case ".asuka":
		return "asuka"
	case ".json":
		return "json"
	case ".toml":
		return "toml"
	case ".md":
		return "markdown"
	}
	return ""
}

// excerpt is src, or for one of more than 40 lines its first and last
// lines around a "…".
func excerpt(src string) string {
	lines := strings.SplitAfter(src, "\n")
	if len(lines) <= 40 {
		return src
	}
	return strings.Join(lines[:6], "") + "  …\n" + strings.Join(lines[len(lines)-6:], "")
}

// Files is an example's files, by name: app.asuka alone, or each one a
// line "-- name --" starts.
func Files(src string) map[string]string {
	if !strings.HasPrefix(src, "-- ") {
		return map[string]string{"app.asuka": src}
	}
	out := map[string]string{}
	name := ""
	for _, line := range strings.SplitAfter(src, "\n") {
		if n, ok := strings.CutPrefix(strings.TrimSpace(line), "-- "); ok && strings.HasSuffix(n, " --") {
			name = strings.TrimSuffix(n, " --")
			out[name] = ""
			continue
		}
		out[name] += line
	}
	return out
}
