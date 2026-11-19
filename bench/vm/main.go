// Command vm measures the bytecode VM (asuka run, asuka test, the
// players) on the programs in bench/vm/programs: each compiled once to a
// module, then timed as asuka-run runs it, so what's measured is
// loading and running, not compiling. bench/README.md says how to read
// it.
//
//	go run ./bench/vm                      # every benchmark, hyperfine's table
//	go run ./bench/vm -only fib,sieve -runs 10
//	go run ./bench/vm -emit /tmp/b         # just the module and the runner, for a profiler
//	go run ./bench/vm -noopt               # the module without the optimizer's passes
//	go run ./bench/vm -instr               # instructions and cycles, not time: what a busy machine doesn't change
//	go run ./bench/vm -ref                 # with CPython, LuaJIT's interpreter and Bun on bench/vm/ref's
//	go run ./bench/vm -c                   # the program compiled to C (asuka build's), not the VM
package main

import (
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"slices"
	"strconv"
	"strings"
	"time"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/lang/bytecode"
	"github.com/egoist/asuka/lang/target"
)

// refs are the benchmarks bench/vm/ref has in Python, Lua and
// JavaScript: the interpreter-bound ones (the others measure libraries).
var refs = map[string]bool{"fib": true, "loop": true, "sieve": true, "nbody": true, "trees": true, "closures": true}

// benches are main's arguments in programs/bench: each one benchmark.
var benches = []string{"fib", "loop", "sieve", "nbody", "trees", "words", "json", "sort", "closures", "iface", "strings"}

func main() {
	only := flag.String("only", "", "comma-separated benchmarks to run (default: all)")
	runs := flag.Int("runs", 5, "hyperfine's runs of each")
	warmup := flag.Int("warmup", 1, "hyperfine's warmup runs")
	emit := flag.String("emit", "", "write the module and print the runner's command into `dir`, without timing")
	dir := flag.String("dir", "bench/vm/programs/bench", "the program to measure")
	noopt := flag.Bool("noopt", false, "compile without the optimizer (ASUKA_OPT=0)")
	export := flag.String("json", "", "hyperfine's JSON export, to `file`")
	compiled := flag.Bool("c", false, "measure the program compiled to C (asuka build's), not the VM")
	instr := flag.Bool("instr", false, "count each run's instructions and cycles (the median of -runs), not its time: what a busy machine doesn't change")
	ref := flag.Bool("ref", false, "also run bench/vm/ref's programs (CPython, LuaJIT's interpreter, Bun) where there's one and the tool is installed")
	disasm := flag.String("disasm", "", "print the bytecode of the functions whose names contain `text`, and stop")
	flag.Parse()

	if *noopt {
		os.Setenv("ASUKA_OPT", "0")
	}
	p, err := appbuild.Load(*dir)
	check(err)
	t0 := time.Now()
	m, diags, err := p.Compile(target.Host())
	check(err)
	if m == nil {
		for _, d := range diags {
			fmt.Fprintln(os.Stderr, d)
		}
		os.Exit(1)
	}
	fmt.Fprintf(os.Stderr, "compiled in %v\n", time.Since(t0).Round(time.Millisecond))
	if *disasm != "" {
		for i := range m.Funcs {
			if strings.Contains(m.Funcs[i].Name, *disasm) {
				fmt.Println(bytecode.Disasm(m, &m.Funcs[i]))
			}
		}
		return
	}
	work := *emit
	if work == "" {
		work, err = os.MkdirTemp("", "asuka-vmbench-")
		check(err)
		defer os.RemoveAll(work)
	} else {
		check(os.MkdirAll(work, 0o755))
	}
	var run func(name string) string
	if *compiled {
		exe := filepath.Join(work, "bench")
		t1 := time.Now()
		check(appbuild.ProgramExe(cbuild.Host(), m, appbuild.ProgramBuild{Name: "bench", Version: "0.1.0"}, exe))
		fmt.Fprintf(os.Stderr, "built in %v\n", time.Since(t1).Round(time.Millisecond))
		run = func(name string) string { return exe + " " + name }
	} else {
		exe, err := appbuild.RunnerFor(m, work)
		check(err)
		mod := filepath.Join(work, "bench.akb")
		check(os.WriteFile(mod, appbuild.StripTests(m).Encode(), 0o644))
		run = func(name string) string {
			return fmt.Sprintf("%s -main -program bench %s -- %s", exe, mod, name)
		}
	}
	list := benches
	if *only != "" {
		list = strings.Split(*only, ",")
	}
	if *emit != "" {
		for _, b := range list {
			fmt.Println(run(b))
		}
		return
	}
	type job struct{ name, command string }
	var jobs []job
	for _, b := range list {
		jobs = append(jobs, job{b, run(b)})
		if *ref && refs[b] {
			for _, r := range [][3]string{{"python3", "", "bench.py"}, {"luajit", "-joff", "bench.lua"}, {"bun", "", "bench.js"}} {
				if exe, err := exec.LookPath(r[0]); err == nil {
					name := map[string]string{"python3": "cpython", "luajit": "luajit-interp", "bun": "bun"}[r[0]]
					jobs = append(jobs, job{b + "/" + name, strings.TrimSpace(exe + " " + r[1] + " bench/vm/ref/" + r[2] + " " + b)})
				}
			}
		}
	}
	if *instr {
		fmt.Printf("%-22s %16s %16s %10s\n", "benchmark", "instructions", "cycles", "time")
		for _, j := range jobs {
			in, cy, el := count(j.command, *runs)
			fmt.Printf("%-22s %16d %16d %10s\n", j.name, in, cy, el.Round(time.Millisecond))
		}
		return
	}
	args := []string{"-N", "--warmup", fmt.Sprint(*warmup), "--runs", fmt.Sprint(*runs), "--style", "basic"}
	if *export != "" {
		args = append(args, "--export-json", *export)
	}
	for _, j := range jobs {
		args = append(args, "-n", j.name, j.command)
	}
	cmd := exec.Command("hyperfine", args...)
	cmd.Stdout, cmd.Stderr = os.Stdout, os.Stderr
	check(cmd.Run())
}

// count runs command (words separated by spaces) n times and returns the
// medians of its instructions retired and its cycles (/usr/bin/time -l
// on macOS, perf stat on Linux), and of its time.
func count(command string, n int) (int64, int64, time.Duration) {
	var ins, cys []int64
	var els []time.Duration
	for i := 0; i < n; i++ {
		var cmd *exec.Cmd
		words := strings.Fields(command)
		if runtime.GOOS == "darwin" {
			cmd = exec.Command("/usr/bin/time", append([]string{"-l"}, words...)...)
		} else {
			cmd = exec.Command("perf", append([]string{"stat", "-x,", "-e", "instructions:u,cycles:u"}, words...)...)
		}
		var stderr strings.Builder
		cmd.Stderr = &stderr
		t0 := time.Now()
		check(cmd.Run())
		els = append(els, time.Since(t0))
		var in, cy int64
		for _, l := range strings.Split(stderr.String(), "\n") {
			f := strings.Fields(strings.ReplaceAll(l, ",", " "))
			switch {
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
		ins, cys = append(ins, in), append(cys, cy)
	}
	slices.Sort(ins)
	slices.Sort(cys)
	slices.Sort(els)
	return ins[n/2], cys[n/2], els[n/2]
}

func check(err error) {
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
