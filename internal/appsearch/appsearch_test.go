package appsearch

import (
	"slices"
	"testing"
)

// search ranks names with Rank and gives them back by name.
func search(ns []string, query string) []string {
	var out []string
	for _, i := range Rank(ns, query) {
		out = append(out, ns[i])
	}
	return out
}

var installed = []string{
	"Activity Monitor", "App Store", "Calculator", "Calendar", "Café Mocha",
	"Code", "Fluß", "Google Chrome", "iPhone Mirroring", "Microsoft Word",
	"Office365", "Photo Booth", "Preview", "System Settings", "TextEdit",
	"Visual Studio Code", "VLC", "VSCodium", "Xcode", "XMLParser", "Ｗｉｄｅ",
}

func TestSearch(t *testing.T) {
	for _, c := range []struct {
		query string
		want  []string
	}{
		// The name, then names that start with it, then words that do
		// (and initials), then names that have it, then its letters in order.
		{"code", []string{"Code", "Visual Studio Code", "Xcode"}},
		{"cod", []string{"Code", "Visual Studio Code", "VSCodium", "Xcode", "Microsoft Word"}},
		{"cal", []string{"Calculator", "Calendar"}},
		{"CAL", []string{"Calculator", "Calendar"}},
		{"  cal  ", []string{"Calculator", "Calendar"}},
		{"calendar", []string{"Calendar"}},
		// Initials, CamelCase.
		{"vsc", []string{"VSCodium", "Visual Studio Code"}},
		{"visstu", []string{"Visual Studio Code"}},
		{"vis stu", []string{"Visual Studio Code"}},
		{"studio code", []string{"Visual Studio Code"}},
		{"gc", []string{"Google Chrome"}},
		{"edit", []string{"TextEdit"}},
		{"te", []string{"TextEdit", "System Settings", "App Store", "Visual Studio Code"}},
		{"parser", []string{"XMLParser"}},
		{"xmlp", []string{"XMLParser"}},
		{"phone", []string{"iPhone Mirroring"}},
		{"365", []string{"Office365"}},
		{"mw", []string{"Microsoft Word"}},
		// Case and accents, both ways; letters that don't decompose;
		// full-width letters.
		{"cafe", []string{"Café Mocha"}},
		{"CAFÉ", []string{"Café Mocha"}},
		{"cafè", []string{"Café Mocha"}},
		{"fluss", []string{"Fluß"}},
		{"wide", []string{"Ｗｉｄｅ"}},
		// Substrings, subsequences.
		{"ode", []string{"Code", "Visual Studio Code", "Xcode"}},
		{"clc", []string{"Calculator"}},
		{"xcd", []string{"Xcode"}},
		{"monitor", []string{"Activity Monitor"}},
		{"store", []string{"App Store"}},
		// Nothing.
		{"zzz", nil},
		{"qx", nil},
	} {
		got := search(installed, c.query)
		if !slices.Equal(got, c.want) {
			t.Errorf("Search(%q) = %q\nwant %q", c.query, got, c.want)
		}
	}
}

// Each rank, in order, and the names in a rank by name whatever order
// they came in.
func TestSearchRanks(t *testing.T) {
	list := []string{
		"Xcalc",           // has "calc": substring
		"Mini Calc",       // a word starts with it
		"Calc",            // the name
		"Calculator",      // starts with it
		"Cxaxlxc",         // its letters in order
		"Abacus Calc Pro", // a word starts with it
		"CalcPad",         // starts with it
		"Notes",           // none
	}
	want := []string{"Calc", "CalcPad", "Calculator", "Abacus Calc Pro", "Mini Calc", "Xcalc", "Cxaxlxc"}
	got := search(list, "calc")
	if !slices.Equal(got, want) {
		t.Fatalf("Search = %q\nwant %q", got, want)
	}
	slices.Reverse(list)
	if got := search(list, "calc"); !slices.Equal(got, want) {
		t.Fatalf("reversed: %q\nwant %q", got, want)
	}
	for _, c := range []struct {
		name, query string
		rank        int
	}{
		{"Calc", "calc", rankExact},
		{"Calculator", "calc", rankPrefix},
		{"Mini Calc", "calc", rankWord},
		{"Visual Studio Code", "vsc", rankWord},
		{"Visual Studio Code", "vc", rankWord}, // words skipped
		{"TextEdit", "tedit", rankWord},
		{"Xcalc", "calc", rankSubstring},
		{"Cxaxlxc", "calc", rankSubsequence},
		{"Notes", "calc", -1},
		{"Calc", "calcs", -1},
	} {
		q := prepare(c.query)
		if r := rank(prepare(c.name), q.text, q.compact()); r != c.rank {
			t.Errorf("rank(%q, %q) = %d, want %d", c.name, c.query, r, c.rank)
		}
	}
}

func TestWords(t *testing.T) {
	for name, want := range map[string][]string{
		"Visual Studio Code": {"visual", "studio", "code"},
		"TextEdit":           {"text", "edit"},
		"XMLParser":          {"xml", "parser"},
		"VSCodium":           {"vs", "codium"},
		"iPhone Mirroring":   {"i", "phone", "mirroring"},
		"Office365":          {"office", "365"},
		"Writer's Desk":      {"writer", "s", "desk"},
		"Café-Mocha":         {"cafe", "mocha"},
	} {
		var got []string
		for _, w := range prepare(name).words() {
			got = append(got, string(w))
		}
		if !slices.Equal(got, want) {
			t.Errorf("words(%q) = %q, want %q", name, got, want)
		}
	}
	if got := fold("Ärger ÆØ straße ﬁle"); got != "arger aeo strasse file" {
		t.Errorf("fold = %q", got)
	}
}
