package appbuild

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"unicode"

	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/cgen"
)

// What asuka test adds to test blocks (docs/testing.md): benches,
// property tests and fuzzing, snapshots and coverage. asuka-run
// (core/runner) runs them and says how they went in lines of JSON, which
// RunTests reads into the summary as they come.

// BenchResult is how a bench ran: its median time per iteration over
// its samples, the fastest's and the slowest's, and its allocations per
// iteration (ak_alloc_count, ak_alloc_bytes).
type BenchResult struct {
	Name        string  `json:"bench"`
	N           int64   `json:"n"`       // iterations per sample
	Samples     int     `json:"samples"` // how many
	NsPerOp     float64 `json:"nsPerOp"`
	MinNsPerOp  float64 `json:"minNsPerOp"`
	MaxNsPerOp  float64 `json:"maxNsPerOp"`
	AllocsPerOp float64 `json:"allocsPerOp"`
	BytesPerOp  float64 `json:"bytesPerOp"`
	Pos         string  `json:"pos,omitempty"`
	// Baseline is the same bench's in the run compared with (--compare),
	// and Change how its time per iteration changed, in percent; Noise:
	// the two runs' ranges overlap, so the change may be noise.
	Baseline *BenchResult `json:"baseline,omitempty"`
	Change   *float64     `json:"change,omitempty"`
	Noise    bool         `json:"noise,omitempty"`
}

// PropertyFailure is a property test's failing input: shrunk (Shrinks
// steps) from the Case'th generated with Seed, found by fuzzing, or a
// corpus file's.
type PropertyFailure struct {
	Seed    uint64 `json:"seed"`
	Case    int    `json:"case,omitempty"` // from 1
	Shrinks int    `json:"shrinks,omitempty"`
	Fuzz    bool   `json:"fuzz,omitempty"`
	Corpus  string `json:"corpus,omitempty"`
	// Input is the parameters' values as JSON (a corpus file's), Shown
	// as asuka test prints them ("xs = [0, -1]").
	Input string `json:"input"`
	Shown string `json:"shown"`
	// Saved is where a fuzz's failing input was saved (the CLI's).
	Saved string `json:"saved,omitempty"`
}

// Snapshot is what a snapshot saw: Status "ok" (it matched), "new" (none
// was stored), "changed" (it differs: the test failed), "updated" (it
// differed, and --update takes what it saw).
type Snapshot struct {
	Key    string `json:"snapshot"` // "<test> <n>"; an inline one's: its file
	Test   string `json:"test"`
	Where  string `json:"where"`
	Status string `json:"status"`
	Value  string `json:"value,omitempty"`
	Stored string `json:"stored,omitempty"`
	// An inline snapshot's call: its line and column in Key's file.
	Inline    bool `json:"inline,omitempty"`
	Line, Col int  `json:"-"`
}

// FuzzProgress is a fuzz's progress, or its end (Done).
type FuzzProgress struct {
	Test        string  `json:"test"`
	Cases       int64   `json:"cases"`
	Interesting int     `json:"interesting"`
	Seconds     float64 `json:"seconds"`
	Done        bool    `json:"done,omitempty"`
}

// testLine reads a line of asuka-run's JSON for what asuka test adds;
// false when it's none of those.
func (s *TestSummary) testLine(line string, o TestOptions, m *bytecode.Module, out io.Writer, width int) bool {
	switch {
	case strings.HasPrefix(line, `{"bench":`):
		var b BenchResult
		if json.Unmarshal([]byte(line), &b) != nil {
			return false
		}
		b.Pos = testPos(m, b.Name, o.Dir)
		if base, ok := o.Baseline[b.Name]; ok {
			b.compare(base)
		}
		s.Benches = append(s.Benches, b)
		if o.JSON {
			writeJSON(out, b)
		} else {
			fmt.Fprintln(out, b.Line(width))
		}
		return true
	case strings.HasPrefix(line, `{"property":`):
		var p struct {
			Name string `json:"property"`
			PropertyFailure
		}
		if json.Unmarshal([]byte(line), &p) != nil {
			return false
		}
		if s.properties == nil {
			s.properties = map[string]*PropertyFailure{}
		}
		f := p.PropertyFailure
		if f.Corpus != "" && o.Dir != "" {
			if r, err := filepath.Rel(o.Dir, f.Corpus); err == nil && filepath.IsLocal(r) {
				f.Corpus = r
			}
		}
		s.properties[p.Name] = &f
		return true
	case strings.HasPrefix(line, `{"snapshot":`), strings.HasPrefix(line, `{"inline":`):
		var r struct {
			Snapshot
			File string `json:"inline"`
			Line int    `json:"line"`
			Col  int    `json:"col"`
		}
		if json.Unmarshal([]byte(line), &r) != nil {
			return false
		}
		sn := r.Snapshot
		if r.File != "" {
			sn.Key, sn.Inline, sn.Line, sn.Col = r.File, true, r.Line, r.Col
		}
		s.Snapshots = append(s.Snapshots, sn)
		return true
	case strings.HasPrefix(line, `{"fuzzing":`), strings.HasPrefix(line, `{"fuzzed":`):
		var r struct {
			Fuzzing string `json:"fuzzing"`
			Fuzzed  string `json:"fuzzed"`
			FuzzProgress
		}
		if json.Unmarshal([]byte(line), &r) != nil {
			return false
		}
		p := r.FuzzProgress
		p.Test, p.Done = r.Fuzzing, r.Fuzzed != ""
		if p.Done {
			p.Test = r.Fuzzed
		}
		s.Fuzz = append(s.Fuzz, p)
		if o.JSON {
			writeJSON(out, map[string]any{"fuzz": p.Test, "cases": p.Cases, "interesting": p.Interesting, "seconds": p.Seconds, "done": p.Done})
		} else {
			what := "fuzz"
			if p.Done {
				what = "fuzzed"
			}
			fmt.Fprintf(out, "%-6s %s: %s cases in %.0fs (%s/s), %d %s that reached new code\n", what, p.Test, group(p.Cases), p.Seconds, group(int64(float64(p.Cases)/max(p.Seconds, 0.001))), p.Interesting, plural(p.Interesting, "input", "inputs"))
		}
		return true
	}
	return false
}

// failed adds what the summary knows of the test's failure to r: its
// property's input, its snapshots' differences.
func (s *TestSummary) failed(r *TestResult) {
	if p := s.properties[r.Name]; p != nil {
		r.Property = p
		delete(s.properties, r.Name)
		var b strings.Builder
		b.WriteString(r.Message)
		switch {
		case p.Corpus != "":
			fmt.Fprintf(&b, "\ninput (%s):", p.Corpus)
		case p.Fuzz:
			fmt.Fprintf(&b, "\ninput (found by fuzzing, shrunk in %d %s):", p.Shrinks, plural(p.Shrinks, "step", "steps"))
		default:
			fmt.Fprintf(&b, "\ninput (case %d of seed %d, shrunk in %d %s):", p.Case, p.Seed, p.Shrinks, plural(p.Shrinks, "step", "steps"))
		}
		for _, l := range strings.Split(p.Shown, "\n") {
			b.WriteString("\n  " + l)
		}
		if p.Corpus == "" && !p.Fuzz {
			fmt.Fprintf(&b, "\nrerun it: asuka test --run %s --seed %d", shellQuote(r.Name), p.Seed)
		}
		r.Message = b.String()
	}
	for _, sn := range s.Snapshots {
		if sn.Test != r.Name || sn.Status != "changed" {
			continue
		}
		if !strings.Contains(r.Message, sn.Where) {
			continue
		}
		r.Message += "\n" + Diff(sn.Stored, sn.Value)
	}
}

func plural(n int, one, many string) string {
	if n == 1 {
		return one
	}
	return many
}

// group writes n with its thousands grouped: 12,345.
func group(n int64) string {
	s := strconv.FormatInt(n, 10)
	neg := strings.HasPrefix(s, "-")
	s = strings.TrimPrefix(s, "-")
	var b strings.Builder
	for i, c := range s {
		if i > 0 && (len(s)-i)%3 == 0 {
			b.WriteByte(',')
		}
		b.WriteRune(c)
	}
	if neg {
		return "-" + b.String()
	}
	return b.String()
}

func shellQuote(s string) string {
	for _, c := range s {
		if !(unicode.IsLetter(c) || unicode.IsDigit(c) || c == '-' || c == '_' || c == '.') {
			return "'" + strings.ReplaceAll(s, "'", `'\''`) + "'"
		}
	}
	return s
}

// compare says how b changed from base: its time's change in percent,
// and whether the two runs' ranges overlap (noise).
func (b *BenchResult) compare(base BenchResult) {
	b.Baseline = &base
	if base.NsPerOp > 0 {
		c := (b.NsPerOp - base.NsPerOp) / base.NsPerOp * 100
		b.Change = &c
	}
	b.Noise = b.MinNsPerOp <= base.MaxNsPerOp && base.MinNsPerOp <= b.MaxNsPerOp
}

// Line is b as asuka test prints it: its name (padded to width), its
// time per iteration and how far its samples spread, its allocations and
// bytes per iteration, its iterations; and against its baseline.
func (b BenchResult) Line(width int) string {
	spread := 0.0
	if b.NsPerOp > 0 {
		spread = max(b.MaxNsPerOp-b.NsPerOp, b.NsPerOp-b.MinNsPerOp) / b.NsPerOp * 100
	}
	s := fmt.Sprintf("bench %-*s %12s/op %-5s %8s allocs/op %10s/op  (%d × %s)", width, b.Name, Nanos(b.NsPerOp), fmt.Sprintf("±%.0f%%", spread), trimFloat(b.AllocsPerOp), Bytes(b.BytesPerOp), b.Samples, group(b.N))
	if b.Baseline != nil && b.Change != nil {
		verdict := fmt.Sprintf("%+.1f%%", *b.Change)
		if b.Noise {
			verdict += " (within noise)"
		}
		s += fmt.Sprintf("  was %s/op: %s", Nanos(b.Baseline.NsPerOp), verdict)
	}
	return s
}

func trimFloat(f float64) string {
	if f >= 10 || f == float64(int64(f)) {
		return strconv.FormatInt(int64(f+0.5), 10)
	}
	return strconv.FormatFloat(f, 'f', 2, 64)
}

// Nanos says a time in nanoseconds in the unit that suits it: 812ns,
// 4.21µs, 13.5ms, 1.20s.
func Nanos(ns float64) string {
	switch {
	case ns < 1000:
		return strconv.FormatFloat(ns, 'f', min(2, digitsFor(ns)), 64) + "ns"
	case ns < 1e6:
		return strconv.FormatFloat(ns/1e3, 'f', digitsFor(ns/1e3), 64) + "µs"
	case ns < 1e9:
		return strconv.FormatFloat(ns/1e6, 'f', digitsFor(ns/1e6), 64) + "ms"
	}
	return strconv.FormatFloat(ns/1e9, 'f', digitsFor(ns/1e9), 64) + "s"
}

// digitsFor: three significant digits.
func digitsFor(v float64) int {
	switch {
	case v >= 100:
		return 0
	case v >= 10:
		return 1
	}
	return 2
}

// Bytes says a size: 0 B, 512 B, 4.0 KB, 1.2 MB.
func Bytes(n float64) string {
	switch {
	case n < 1024:
		return strconv.FormatInt(int64(n+0.5), 10) + " B"
	case n < 1<<20:
		return strconv.FormatFloat(n/1024, 'f', 1, 64) + " KB"
	}
	return strconv.FormatFloat(n/(1<<20), 'f', 1, 64) + " MB"
}

// benchWidth is the longest bench's name.
func benchWidth(m *bytecode.Module) int {
	w := 0
	for _, t := range m.Tests {
		if t.Bench {
			w = max(w, len([]rune(t.Name)))
		}
	}
	return w
}

// ReadBaseline reads the benches of a run asuka test --json wrote, to
// compare with (--compare).
func ReadBaseline(path string) (map[string]BenchResult, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	out := map[string]BenchResult{}
	for _, l := range strings.Split(string(b), "\n") {
		if !strings.HasPrefix(l, `{"bench":`) {
			continue
		}
		var r BenchResult
		if err := json.Unmarshal([]byte(l), &r); err != nil {
			return nil, fmt.Errorf("%s: %v", path, err)
		}
		r.Baseline, r.Change, r.Noise = nil, nil, false
		out[r.Name] = r
	}
	if len(out) == 0 {
		return nil, fmt.Errorf("%s has no benches' results: write one with asuka test --bench --json > %s", path, filepath.Base(path))
	}
	return out, nil
}

// cRunner is asuka-run with m compiled to C in it (lang/cgen), as a
// release build runs it: benches measure what ships. Linked in dir.
func cRunner(m *bytecode.Module, dir string) (string, error) {
	core, err := cbuild.CoreDir()
	if err != nil {
		return "", err
	}
	src, err := cgen.Generate(m)
	if err != nil {
		return "", err
	}
	app := filepath.Join(dir, "app.c")
	if err := os.WriteFile(app, src, 0o644); err != nil {
		return "", err
	}
	var f *cbuild.Foreign
	if m.C != nil {
		if f, err = Foreign(m.C, cbuild.Host()); err != nil {
			return "", err
		}
	}
	exe, err := cbuild.RunnerFor(core, cbuild.RunnerOptions{Extra: []string{app}, Foreign: f, Dir: dir, Builtins: m.Builtins})
	if err != nil {
		return "", fmt.Errorf("the C the package compiles to doesn't build: %v", err)
	}
	return exe, nil
}

// Diff says how want's lines became got's: a line each, "- " what was
// there, "+ " what's there now, "  " what both have (a long run of those
// shortened to its ends).
func Diff(want, got string) string {
	a, b := strings.Split(want, "\n"), strings.Split(got, "\n")
	type op struct {
		kind byte
		text string
	}
	var ops []op
	if len(a)*len(b) > 4_000_000 {
		for _, l := range a {
			ops = append(ops, op{'-', l})
		}
		for _, l := range b {
			ops = append(ops, op{'+', l})
		}
	} else {
		// The longest common subsequence, from the ends.
		n, m := len(a), len(b)
		lcs := make([][]int32, n+1)
		for i := range lcs {
			lcs[i] = make([]int32, m+1)
		}
		for i := n - 1; i >= 0; i-- {
			for j := m - 1; j >= 0; j-- {
				if a[i] == b[j] {
					lcs[i][j] = lcs[i+1][j+1] + 1
				} else {
					lcs[i][j] = max(lcs[i+1][j], lcs[i][j+1])
				}
			}
		}
		i, j := 0, 0
		for i < n || j < m {
			switch {
			case i < n && j < m && a[i] == b[j]:
				ops = append(ops, op{' ', a[i]})
				i, j = i+1, j+1
			case i < n && (j == m || lcs[i+1][j] >= lcs[i][j+1]):
				ops = append(ops, op{'-', a[i]})
				i++
			default:
				ops = append(ops, op{'+', b[j]})
				j++
			}
		}
	}
	var out strings.Builder
	out.WriteString("- the snapshot\n+ what it shows")
	const context = 3
	for k := 0; k < len(ops); k++ {
		if ops[k].kind != ' ' {
			fmt.Fprintf(&out, "\n%c %s", ops[k].kind, ops[k].text)
			continue
		}
		e := k
		for e < len(ops) && ops[e].kind == ' ' {
			e++
		}
		run := ops[k:e]
		if len(run) > 2*context+1 {
			head, tail := run[:context], run[len(run)-context:]
			if k == 0 {
				head = nil
			}
			if e == len(ops) {
				tail = nil
			}
			for _, o := range head {
				out.WriteString("\n  " + o.text)
			}
			fmt.Fprintf(&out, "\n  … (%d %s)", len(run)-len(head)-len(tail), plural(len(run)-len(head)-len(tail), "line", "lines"))
			for _, o := range tail {
				out.WriteString("\n  " + o.text)
			}
		} else {
			for _, o := range run {
				out.WriteString("\n  " + o.text)
			}
		}
		k = e - 1
	}
	return out.String()
}
