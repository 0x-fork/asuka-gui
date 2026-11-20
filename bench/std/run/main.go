// Command run times the standard library's benchmarks (bench/std) in
// Asuka, Go and Bun, on the same inputs, and prints a Markdown table:
// each one's median and spread over -runs runs, interleaved after a
// warm-up run of each, and how many times Go's time Asuka's is.
//
//	go run ./bench/std/run                     # every benchmark
//	go run ./bench/std/run -only sha256,gzip -runs 9
//	go run ./bench/std/run -asuka old=/path/to/stdbench   # another build too
//	go run ./bench/std/run -instr                # cycles and instructions, not time
//
// Each program times its work itself (after setting up its inputs) and
// prints its result and the nanoseconds on stderr; the runner checks
// that the results agree, so each language does the same work. A
// second table is each program's peak memory: the whole process's
// maximum resident set size, its inputs included (the same inputs in
// each language), the median of the runs.
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
	"syscall"
	"time"
)

var names = []string{
	"interp", "concat", "split", "find", "case", "count", "num", "fmt",
	"mapint", "mapstr", "set", "sortint", "sortstr", "sortby",
	"regex", "files", "lines", "print", "stdout", "csv", "toml",
	"url", "base64", "hex", "html", "zip", "tar",
	"sha256", "sha512", "sha1", "md5", "crc32", "hmac", "aesgcm", "chacha", "argon2", "bcrypt", "gzip",
	"bigint", "modpow", "time", "template", "semver", "heap", "sortedmap", "sortedops", "seq",
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
	instr := flag.Bool("instr", false, "each benchmark's cycles and instructions retired, not its time (macOS's /usr/bin/time -l, Linux's perf stat): what a busy machine changes little")
	reps := flag.Int("reps", 4, "-instr: a run's work this many times, less a run's once, over reps-1 (the setup cancels out)")
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
	if *instr {
		counted(progs, selected, tmp, *runs, *reps)
		return
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
	peaks := map[string]map[string][]float64{} // benchmark → label → each run's peak bytes
	for _, name := range selected {
		times := make([][]float64, len(progs))
		results := make([]string, len(progs))
		peaks[name] = map[string][]float64{}
		for r := -1; r < *runs; r++ { // -1: the warm-up
			for i, p := range progs {
				out, ns, peak, err := once(p, name, tmp)
				if err != nil {
					fmt.Fprintf(os.Stderr, "%s %s: %v\n", p.label, name, err)
					results[i] = "error"
					continue
				}
				results[i] = out
				if r >= 0 {
					times[i] = append(times[i], ns/1e6)
					if out != "n/a" && peak > 0 {
						peaks[name][p.label] = append(peaks[name][p.label], peak)
					}
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
	printPeaks(progs, selected, peaks)
}

// counted prints each benchmark's millions of cycles and instructions
// retired a run of its work, in each language: the medians over runs of
// (a process doing it k times - one doing it once) / (k - 1), so a
// program's setup (its inputs made) and start cancel out; k is reps, or
// more where the setup takes long beside the work (its cycles vary by
// more than a short benchmark's), up to 64.
func counted(progs []prog, selected []string, dir string, runs, reps int) {
	fmt.Printf("%s, %s/%s, %d runs of at least %d less 1 (median, millions of cycles a run; instructions' ratio)\n\n", cpuName(), runtime.GOOS, runtime.GOARCH, runs, reps)
	header, rule := "| benchmark |", "|---|"
	for _, p := range progs {
		header += " " + p.label + " |"
		rule += "---:|"
	}
	pair := has(progs, "asuka") && has(progs, "go")
	if pair {
		header += " asuka/go | instructions asuka/go |"
		rule += "---:|---:|"
	}
	fmt.Println(header)
	fmt.Println(rule)
	peaks := map[string]map[string][]float64{}
	for _, name := range selected {
		peaks[name] = map[string][]float64{}
		cys := map[string]float64{}
		ins := map[string]float64{}
		results := make([]string, len(progs))
		row := "| " + name + " |"
		for i, p := range progs {
			var cs, is []float64
			k := 0 // the repetitions: enough that the work outweighs the setup's noise
			for r := 0; r < runs; r++ {
				out, c1, i1, setup, work, peak, err := count(p, name, dir, 1)
				if err == nil && out != "n/a" {
					if peak > 0 {
						peaks[name][p.label] = append(peaks[name][p.label], peak)
					}
					if k == 0 {
						k = reps
						if work > 0 && setup > 0 {
							k = max(k, min(64, 1+int(4*setup/work)))
						}
					}
					var ck, ik int64
					_, ck, ik, _, _, _, err = count(p, name, dir, k)
					cs = append(cs, float64(ck-c1)/float64(k-1)/1e6)
					is = append(is, float64(ik-i1)/float64(k-1)/1e6)
				}
				if err != nil {
					fmt.Fprintf(os.Stderr, "%s %s: %v\n", p.label, name, err)
					out = "error"
				}
				results[i] = out
				if out == "n/a" || err != nil {
					break
				}
			}
			if len(cs) == 0 {
				row += " n/a |"
				continue
			}
			c, _ := medianMAD(cs)
			n, _ := medianMAD(is)
			cys[p.label], ins[p.label] = c, n
			row += fmt.Sprintf(" %s |", ms(c))
		}
		if pair {
			if a, ok := cys["asuka"]; ok && cys["go"] > 0 {
				row += fmt.Sprintf(" %.2f× | %.2f× |", a/cys["go"], ins["asuka"]/ins["go"])
			} else {
				row += " | |"
			}
		}
		fmt.Println(row)
		agree(name, progs, results)
	}
	printPeaks(progs, selected, peaks)
}

// count runs p's name, its work reps times (STDBENCH_REPS), and returns
// its result, the process's cycles and instructions retired, how long
// its setup and its work took (s), and its peak resident memory (bytes).
func count(p prog, name, dir string, reps int) (string, int64, int64, float64, float64, float64, error) {
	words := append(append([]string{}, p.cmd...), name, dir)
	var cmd *exec.Cmd
	if runtime.GOOS == "darwin" {
		cmd = exec.Command("/usr/bin/time", append([]string{"-l"}, words...)...)
	} else {
		cmd = exec.Command("perf", append([]string{"stat", "-x,", "-e", "instructions:u,cycles:u"}, words...)...)
	}
	cmd.Env = append(os.Environ(), "STDBENCH_REPS="+strconv.Itoa(reps))
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	devnull, err := os.OpenFile(os.DevNull, os.O_WRONLY, 0)
	if err != nil {
		return "", 0, 0, 0, 0, 0, err
	}
	defer devnull.Close()
	cmd.Stdout = devnull
	t0 := time.Now()
	if err := cmd.Run(); err != nil {
		return "", 0, 0, 0, 0, 0, fmt.Errorf("%v: %s", err, stderr.String())
	}
	wall := time.Since(t0).Seconds()
	var out string
	var cy, in int64
	var work, peak float64
	for _, l := range strings.Split(stderr.String(), "\n") {
		if a, ns, ok := strings.Cut(l, "\t"); ok {
			out = a
			if v, err := strconv.ParseFloat(strings.TrimSpace(ns), 64); err == nil {
				work = v / 1e9
			}
		}
		f := strings.Fields(strings.ReplaceAll(l, ",", " "))
		switch {
		case len(f) >= 5 && f[1] == "maximum" && f[2] == "resident":
			// macOS's time -l: the program's own peak, in bytes.
			if v, err := strconv.ParseFloat(f[0], 64); err == nil {
				peak = v
			}
		case len(f) >= 3 && f[1] == "instructions" && f[2] == "retired":
			in, _ = strconv.ParseInt(f[0], 10, 64)
		case len(f) >= 3 && f[1] == "cycles" && f[2] == "elapsed":
			cy, _ = strconv.ParseInt(f[0], 10, 64)
		case len(f) >= 2 && strings.HasPrefix(f[1], "instructions"):
			in, _ = strconv.ParseInt(f[0], 10, 64)
		case len(f) >= 2 && strings.HasPrefix(f[1], "cycles"):
			cy, _ = strconv.ParseInt(f[0], 10, 64)
		}
	}
	if peak == 0 {
		// Linux's perf stat says nothing of memory: the rusage of perf,
		// which counts the program it waited for.
		peak = maxRSS(cmd.ProcessState)
	}
	return out, cy, in, max(wall-work, 0), work, peak, nil
}

// once runs p's name once: its result, its work's nanoseconds, and the
// process's peak resident memory (bytes).
func once(p prog, name, dir string) (string, float64, float64, error) {
	cmd := exec.Command(p.cmd[0], append(p.cmd[1:], name, dir)...)
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	devnull, err := os.OpenFile(os.DevNull, os.O_WRONLY, 0)
	if err != nil {
		return "", 0, 0, err
	}
	defer devnull.Close()
	cmd.Stdout = devnull
	if err := cmd.Run(); err != nil {
		return "", 0, 0, fmt.Errorf("%v: %s", err, stderr.String())
	}
	lines := strings.Split(strings.TrimSpace(stderr.String()), "\n")
	out, ns, ok := strings.Cut(lines[len(lines)-1], "\t")
	if !ok {
		return "", 0, 0, fmt.Errorf("no result: %q", stderr.String())
	}
	n, err := strconv.ParseFloat(ns, 64)
	return out, n, maxRSS(cmd.ProcessState), err
}

// maxRSS is a finished process's peak resident memory, in bytes (the
// rusage's ru_maxrss: bytes on macOS, kilobytes on Linux).
func maxRSS(ps *os.ProcessState) float64 {
	ru, ok := ps.SysUsage().(*syscall.Rusage)
	if !ok || ru == nil {
		return 0
	}
	if runtime.GOOS == "darwin" {
		return float64(ru.Maxrss)
	}
	return float64(ru.Maxrss) * 1024
}

// printPeaks is the memory table: each program's peak resident memory a
// run (the whole process: its inputs made, then the work; the median
// over the runs), in MB.
func printPeaks(progs []prog, selected []string, peaks map[string]map[string][]float64) {
	fmt.Printf("\nPeak memory (maximum resident set size of the whole process, inputs included; median of the runs, MB)\n\n")
	header, rule := "| benchmark |", "|---|"
	for _, p := range progs {
		header += " " + p.label + " |"
		rule += "---:|"
	}
	pair := has(progs, "asuka") && has(progs, "go")
	if pair {
		header += " asuka/go |"
		rule += "---:|"
	}
	fmt.Println(header)
	fmt.Println(rule)
	for _, name := range selected {
		row := "| " + name + " |"
		meds := map[string]float64{}
		for _, p := range progs {
			xs := peaks[name][p.label]
			if len(xs) == 0 {
				row += " n/a |"
				continue
			}
			m, _ := medianMAD(xs)
			meds[p.label] = m
			row += fmt.Sprintf(" %.1f |", m/1e6)
		}
		if pair {
			if a, g := meds["asuka"], meds["go"]; a > 0 && g > 0 {
				row += fmt.Sprintf(" %.2f× |", a/g)
			} else {
				row += " |"
			}
		}
		fmt.Println(row)
	}
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
