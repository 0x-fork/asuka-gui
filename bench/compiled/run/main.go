// Command run times bench/compiled's programs: plain computation in
// Asuka (compiled to C by asuka build, as a release is), Go, Zig and
// Bun, the same algorithms on the same inputs. It builds each, checks
// that they print the same, times each benchmark with hyperfine (its
// warm-up runs, then -runs runs) and prints a Markdown table of the
// means with their standard deviations, and Asuka's time over Go's.
//
//	go run ./bench/compiled/run                    # every benchmark, every language there is
//	go run ./bench/compiled/run -only fib,nbody -runs 10
//	go run ./bench/compiled/run -langs asuka,go
//	go run ./bench/compiled/run -asuka old=/path/to/langbench  # another Asuka build too
package main

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
)

var names = []string{"fib", "sieve", "nbody", "trees", "words", "json", "sort"}

type prog struct {
	label string
	cmd   string // a shell command, the benchmark's name after it (paths quoted)
}

type multi []string

func (m *multi) String() string     { return strings.Join(*m, ",") }
func (m *multi) Set(v string) error { *m = append(*m, v); return nil }

func main() {
	runs := flag.Int("runs", 5, "timed runs of each benchmark in each language")
	warmup := flag.Int("warmup", 1, "warm-up runs before them")
	only := flag.String("only", "", "comma-separated benchmarks (default: all)")
	langs := flag.String("langs", "asuka,go,zig,bun", "which of asuka, go, zig, bun to run (those installed)")
	var extra multi
	flag.Var(&extra, "asuka", "another Asuka build to time too: label=path (repeatable)")
	flag.Parse()

	if _, err := exec.LookPath("hyperfine"); err != nil {
		fail(fmt.Errorf("hyperfine isn't installed (https://github.com/sharkdp/hyperfine)"))
	}
	root, err := repoRoot()
	check(err)
	dir := filepath.Join(root, "bench", "compiled")
	tmp, err := os.MkdirTemp("", "langbench")
	check(err)
	defer os.RemoveAll(tmp)

	var progs []prog
	for _, l := range strings.Split(*langs, ",") {
		switch l {
		case "asuka":
			fmt.Fprintln(os.Stderr, "building bench/compiled/asuka")
			run(root, "go", "run", "./cmd/asuka", "build", "bench/compiled/asuka")
			progs = append(progs, prog{"asuka", quote(filepath.Join(dir, "asuka", "dist", "langbench"))})
		case "go":
			fmt.Fprintln(os.Stderr, "building bench/compiled/go")
			bin := filepath.Join(tmp, "langbench-go")
			run(filepath.Join(dir, "go"), "go", "build", "-o", bin, ".")
			progs = append(progs, prog{"go", quote(bin)})
		case "zig":
			if _, err := exec.LookPath("zig"); err != nil {
				fmt.Fprintln(os.Stderr, "no zig: skipping it")
				continue
			}
			fmt.Fprintln(os.Stderr, "building bench/compiled/zig")
			bin := filepath.Join(tmp, "langbench-zig")
			run(filepath.Join(dir, "zig"), "zig", "build-exe", "-O", "ReleaseFast", "-femit-bin="+bin, "main.zig")
			progs = append(progs, prog{"zig", quote(bin)})
		case "bun":
			if _, err := exec.LookPath("bun"); err != nil {
				fmt.Fprintln(os.Stderr, "no bun: skipping it")
				continue
			}
			progs = append(progs, prog{"bun", "bun " + quote(filepath.Join(dir, "bun", "main.js"))})
		}
	}
	for _, e := range extra {
		label, path, ok := strings.Cut(e, "=")
		if !ok {
			fail(fmt.Errorf("-asuka %q: want label=path", e))
		}
		progs = append(progs, prog{label, quote(path)})
	}

	selected := names
	if *only != "" {
		selected = strings.Split(*only, ",")
	}
	fmt.Printf("%s, %s/%s; hyperfine, %d warm-up and %d timed runs (mean ± σ, ms)\n\n", cpuName(), runtime.GOOS, runtime.GOARCH, *warmup, *runs)
	header, rule := "| benchmark |", "|---|"
	for _, p := range progs {
		header += " " + p.label + " |"
		rule += "---:|"
	}
	ratio := has(progs, "asuka") && has(progs, "go")
	if ratio {
		header += " asuka/go |"
		rule += "---:|"
	}
	fmt.Println(header)
	fmt.Println(rule)
	for _, name := range selected {
		// The same output from each, or the timing means nothing.
		want := ""
		for _, p := range progs {
			out, err := exec.Command("sh", "-c", p.cmd+" "+name).CombinedOutput()
			if err != nil {
				fail(fmt.Errorf("%s %s: %v\n%s", p.label, name, err, out))
			}
			got := strings.TrimSpace(string(out))
			if want == "" {
				want = got
			} else if got != want {
				fail(fmt.Errorf("%s %s printed %q, %s printed %q", p.label, name, got, progs[0].label, want))
			}
		}
		js := filepath.Join(tmp, name+".json")
		args := []string{"--warmup", fmt.Sprint(*warmup), "--runs", fmt.Sprint(*runs), "--export-json", js, "--style", "none"}
		for _, p := range progs {
			args = append(args, "-n", p.label, p.cmd+" "+name)
		}
		cmd := exec.Command("hyperfine", args...)
		cmd.Stderr = os.Stderr
		check(cmd.Run())
		var res struct {
			Results []struct {
				Command string  `json:"command"`
				Mean    float64 `json:"mean"`
				Stddev  float64 `json:"stddev"`
			} `json:"results"`
		}
		data, err := os.ReadFile(js)
		check(err)
		check(json.Unmarshal(data, &res))
		row := "| " + name + " |"
		means := map[string]float64{}
		for i, r := range res.Results {
			means[progs[i].label] = r.Mean
			row += fmt.Sprintf(" %.0f ± %.0f |", r.Mean*1e3, r.Stddev*1e3)
		}
		if ratio {
			row += fmt.Sprintf(" %.2f× |", means["asuka"]/means["go"])
		}
		fmt.Println(row)
	}
}

// quote is s as one word of the shell's.
func quote(s string) string { return "'" + strings.ReplaceAll(s, "'", `'\''`) + "'" }

func has(ps []prog, label string) bool {
	for _, p := range ps {
		if p.label == label {
			return true
		}
	}
	return false
}

func run(dir string, name string, args ...string) {
	cmd := exec.Command(name, args...)
	cmd.Dir = dir
	var out bytes.Buffer
	cmd.Stdout, cmd.Stderr = &out, &out
	if err := cmd.Run(); err != nil {
		fail(fmt.Errorf("%s %s: %v\n%s", name, strings.Join(args, " "), err, out.String()))
	}
}

// repoRoot is the repository's root: where go.mod is, from here up.
func repoRoot() (string, error) {
	d, err := os.Getwd()
	if err != nil {
		return "", err
	}
	for {
		if _, err := os.Stat(filepath.Join(d, "go.mod")); err == nil {
			if _, err := os.Stat(filepath.Join(d, "bench", "compiled")); err == nil {
				return d, nil
			}
		}
		p := filepath.Dir(d)
		if p == d {
			return "", fmt.Errorf("run it in the repository (no bench/compiled above %s)", d)
		}
		d = p
	}
}

// cpuName is the machine's processor, as the system names it.
func cpuName() string {
	var out []byte
	switch runtime.GOOS {
	case "darwin":
		out, _ = exec.Command("sysctl", "-n", "machdep.cpu.brand_string").Output()
	case "linux":
		data, _ := os.ReadFile("/proc/cpuinfo")
		for _, l := range strings.Split(string(data), "\n") {
			if k, v, ok := strings.Cut(l, ":"); ok && strings.TrimSpace(k) == "model name" {
				return strings.TrimSpace(v)
			}
		}
	}
	if s := strings.TrimSpace(string(out)); s != "" {
		return s
	}
	return runtime.GOARCH
}

func check(err error) {
	if err != nil {
		fail(err)
	}
}

func fail(err error) {
	fmt.Fprintln(os.Stderr, "run:", err)
	os.Exit(1)
}
