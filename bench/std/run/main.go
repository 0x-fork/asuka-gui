// Command run times the standard library's benchmarks (bench/std) in
// Asuka, Go and Bun, on the same inputs, and prints a Markdown table:
// each one's median and spread over -runs runs, interleaved after a
// warm-up run of each, and how many times Go's time Asuka's is.
//
//	go run ./bench/std/run                     # every benchmark
//	go run ./bench/std/run -only sha256,gzip -runs 9
//	go run ./bench/std/run -asuka old=/path/to/stdbench   # another build too
//
// Each program times its work itself (after setting up its inputs) and
// prints its result and the nanoseconds on stderr; the runner checks
// that the results agree, so each language does the same work.
package main

import (
	"bytes"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"slices"
	"sort"
	"strconv"
	"strings"
	"time"
)

var names = []string{
	"interp", "concat", "split", "find", "case", "count", "num", "fmt",
	"mapint", "mapstr", "set", "sortint", "sortstr", "sortby",
	"regex", "files", "lines", "print", "stdout", "csv", "toml",
	"url", "base64", "hex", "html", "zip", "tar",
	"sha256", "sha512", "sha1", "md5", "crc32", "hmac", "aesgcm", "chacha", "argon2", "bcrypt", "gzip",
	"bigint", "modpow", "time", "template", "semver", "heap", "sortedmap", "seq",
}

// Results whose bytes legitimately differ between implementations
// (compressed sizes): only Asuka's own builds are compared on them.
var sizeVaries = map[string]bool{"gzip": true}

type prog struct {
	label string
	cmd   []string
	asuka bool
}

func main() {
	runs := flag.Int("runs", 5, "timed runs of each benchmark in each language")
	only := flag.String("only", "", "comma-separated benchmarks (default: all)")
	langs := flag.String("langs", "asuka,go,bun", "which of asuka, go, bun to run")
	var extra multi
	flag.Var(&extra, "asuka", "another Asuka build to time too: label=path (repeatable)")
	flag.Parse()

	root, err := repoRoot()
	check(err)
	tmp, err := os.MkdirTemp("", "stdbench")
	check(err)
	defer os.RemoveAll(tmp)

	var progs []prog
	for _, l := range strings.Split(*langs, ",") {
		switch l {
		case "asuka":
			fmt.Fprintln(os.Stderr, "building bench/std/asuka")
			run(root, "go", "run", "./cmd/asuka", "build", "bench/std/asuka")
			progs = append(progs, prog{"asuka", []string{filepath.Join(root, "bench/std/asuka/dist/stdbench")}, true})
		case "go":
			fmt.Fprintln(os.Stderr, "building bench/std/go")
			bin := filepath.Join(tmp, "stdbench-go")
			run(filepath.Join(root, "bench/std/go"), "go", "build", "-o", bin, ".")
			progs = append(progs, prog{"go", []string{bin}, false})
		case "bun":
			if _, err := exec.LookPath("bun"); err != nil {
				fmt.Fprintln(os.Stderr, "no bun: skipping it")
				continue
			}
			progs = append(progs, prog{"bun", []string{"bun", filepath.Join(root, "bench/std/bun/main.js")}, false})
		}
	}
	for _, e := range extra {
		label, path, ok := strings.Cut(e, "=")
		if !ok {
			check(fmt.Errorf("-asuka %q: want label=path", e))
		}
		progs = append(progs, prog{label, []string{path}, true})
	}

	selected := names
	if *only != "" {
		selected = strings.Split(*only, ",")
	}
	fmt.Printf("%s, %s/%s, %d runs (median ± median absolute deviation, ms)\n\n", cpuName(), runtime.GOOS, runtime.GOARCH, *runs)
	header := "| benchmark |"
	rule := "|---|"
	for _, p := range progs {
		header += " " + p.label + " |"
		rule += "---:|"
	}
	if has(progs, "asuka") && has(progs, "go") {
		header += " asuka/go |"
		rule += "---:|"
	}
	fmt.Println(header)
	fmt.Println(rule)
	for _, name := range selected {
		times := make([][]float64, len(progs))
		results := make([]string, len(progs))
		for r := -1; r < *runs; r++ { // -1: the warm-up
			for i, p := range progs {
				out, ns, err := once(p, name, tmp)
				if err != nil {
					fmt.Fprintf(os.Stderr, "%s %s: %v\n", p.label, name, err)
					results[i] = "error"
					continue
				}
				results[i] = out
				if r >= 0 {
					times[i] = append(times[i], ns/1e6)
				}
			}
		}
		row := "| " + name + " |"
		meds := map[string]float64{}
		for i, p := range progs {
			if results[i] == "n/a" || len(times[i]) == 0 {
				row += " n/a |"
				continue
			}
			m, d := medianMAD(times[i])
			meds[p.label] = m
			row += fmt.Sprintf(" %s ± %s |", ms(m), ms(d))
		}
		if a, ok := meds["asuka"]; ok {
			if g, ok := meds["go"]; ok {
				row += fmt.Sprintf(" %.2f× |", a/g)
			} else {
				row += " |"
			}
		}
		fmt.Println(row)
		agree(name, progs, results)
	}
}

func once(p prog, name, dir string) (string, float64, error) {
	cmd := exec.Command(p.cmd[0], append(p.cmd[1:], name, dir)...)
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	devnull, err := os.Open(os.DevNull)
	if err != nil {
		return "", 0, err
	}
	defer devnull.Close()
	cmd.Stdout = devnull
	if err := cmd.Run(); err != nil {
		return "", 0, fmt.Errorf("%v: %s", err, stderr.String())
	}
	lines := strings.Split(strings.TrimSpace(stderr.String()), "\n")
	out, ns, ok := strings.Cut(lines[len(lines)-1], "\t")
	if !ok {
		return "", 0, fmt.Errorf("no result: %q", stderr.String())
	}
	n, err := strconv.ParseFloat(ns, 64)
	return out, n, err
}

// agree warns when the languages' results differ: they didn't do the
// same work.
func agree(name string, progs []prog, results []string) {
	want := ""
	for i, p := range progs {
		if results[i] == "n/a" || results[i] == "error" || (sizeVaries[name] && !p.asuka) {
			continue
		}
		if want == "" {
			want = results[i]
		} else if results[i] != want {
			fmt.Fprintf(os.Stderr, "%s: %s's result %q isn't %q\n", name, p.label, trim(results[i]), trim(want))
		}
	}
}

func medianMAD(xs []float64) (float64, float64) {
	s := slices.Clone(xs)
	sort.Float64s(s)
	m := s[len(s)/2]
	if len(s)%2 == 0 {
		m = (s[len(s)/2-1] + s[len(s)/2]) / 2
	}
	dev := make([]float64, len(s))
	for i, x := range s {
		dev[i] = abs(x - m)
	}
	sort.Float64s(dev)
	return m, dev[len(dev)/2]
}

func ms(x float64) string {
	switch {
	case x >= 100:
		return strconv.FormatFloat(x, 'f', 0, 64)
	case x >= 10:
		return strconv.FormatFloat(x, 'f', 1, 64)
	}
	return strconv.FormatFloat(x, 'f', 2, 64)
}

func abs(x float64) float64 {
	if x < 0 {
		return -x
	}
	return x
}

func trim(s string) string {
	if len(s) > 60 {
		return s[:60] + "…"
	}
	return s
}

func has(progs []prog, label string) bool {
	for _, p := range progs {
		if p.label == label {
			return true
		}
	}
	return false
}

func cpuName() string {
	if runtime.GOOS == "darwin" {
		if out, err := exec.Command("sysctl", "-n", "machdep.cpu.brand_string").Output(); err == nil {
			return strings.TrimSpace(string(out))
		}
	}
	if b, err := os.ReadFile("/proc/cpuinfo"); err == nil {
		for _, l := range strings.Split(string(b), "\n") {
			if k, v, ok := strings.Cut(l, ":"); ok && strings.TrimSpace(k) == "model name" {
				return strings.TrimSpace(v)
			}
		}
	}
	return runtime.GOARCH
}

func repoRoot() (string, error) {
	out, err := exec.Command("go", "env", "GOMOD").Output()
	if err != nil {
		return "", err
	}
	return filepath.Dir(strings.TrimSpace(string(out))), nil
}

func run(dir string, name string, args ...string) {
	cmd := exec.Command(name, args...)
	cmd.Dir = dir
	cmd.Stdout = os.Stderr
	cmd.Stderr = os.Stderr
	start := time.Now()
	check(cmd.Run())
	fmt.Fprintf(os.Stderr, "  (%s)\n", time.Since(start).Round(time.Millisecond))
}

func check(err error) {
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

type multi []string

func (m *multi) String() string     { return strings.Join(*m, ",") }
func (m *multi) Set(v string) error { *m = append(*m, v); return nil }
