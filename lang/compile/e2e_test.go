package compile

import (
	"flag"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"testing"

	"github.com/egoist/asuka/internal/cbuild"
	bc "github.com/egoist/asuka/lang/bytecode"
)

// The language's conformance suite: each testdata/run/*.asuka is a
// program whose test blocks run on the VM (asuka-run, built with
// sanitizers); what it prints must be what the comment block at its end,
// after a line "// Output:", says (the runner's "ok name" lines
// included). A file whose first line is "// skip: reason" is skipped.

var dumpFlag = flag.Bool("dump", false, "print each module's disassembly")

var (
	runnerOnce sync.Once
	runnerExe  string
	runnerErr  error
)

// runBoth runs m's tests on the VM, then compiled to C (lang/cgen), and
// fails when the two backends don't print the same or don't agree on
// failing: the conformance suite holds for both. It returns the VM's.
func runBoth(t *testing.T, m *bc.Module) (string, error) {
	t.Helper()
	out, err := Run(testRunner(t), m, true, "")
	cout, cerr := RunC(m, true, true, "")
	if _, ok := cerr.(*exec.ExitError); cerr != nil && !ok {
		t.Fatalf("the C backend: %v\n%s", cerr, cout)
	}
	if cout != out || (cerr == nil) != (err == nil) {
		t.Fatalf("the VM and the compiled C disagree:\nVM (%v):\n%s\nC (%v):\n%s", err, out, cerr, cout)
	}
	return out, err
}

func testRunner(t *testing.T) string {
	t.Helper()
	if _, err := cbuild.Find(); err != nil {
		t.Skip(err)
	}
	runnerOnce.Do(func() { runnerExe, runnerErr = Runner(true) })
	if runnerErr != nil {
		t.Fatal(runnerErr)
	}
	return runnerExe
}

// wantOutput is the expected output written at the end of src.
func wantOutput(src string) (string, bool) {
	i := strings.Index(src, "\n// Output:\n")
	if i < 0 {
		return "", false
	}
	var lines []string
	for _, l := range strings.Split(strings.TrimSpace(src[i+len("\n// Output:\n"):]), "\n") {
		l = strings.TrimPrefix(l, "//")
		lines = append(lines, strings.TrimPrefix(l, " "))
	}
	return strings.Join(lines, "\n"), true
}

func TestRun(t *testing.T) {
	files, _ := filepath.Glob("testdata/run/*.asuka")
	if len(files) == 0 {
		t.Fatal("no programs")
	}
	for _, path := range files {
		path := path
		name := strings.TrimSuffix(filepath.Base(path), ".asuka")
		t.Run(name, func(t *testing.T) {
			src, err := os.ReadFile(path)
			if err != nil {
				t.Fatal(err)
			}
			if strings.HasPrefix(string(src), "// skip:") {
				t.Skip(strings.SplitN(string(src), "\n", 2)[0])
			}
			want, ok := wantOutput(string(src))
			if !ok {
				t.Fatal("no // Output: block")
			}
			res := Files(map[string][]byte{path: src}, nil)
			if err := res.Err(); err != nil {
				t.Fatalf("compile:\n%v", err)
			}
			if err := res.IR.Validate(); err != nil {
				t.Fatalf("invalid IR: %v", err)
			}
			if *dumpFlag {
				for i := range res.Module.Funcs {
					t.Log(bc.Disasm(res.Module, &res.Module.Funcs[i]))
				}
			}
			out, err := runBoth(t, res.Module)
			got := strings.TrimSpace(out)
			if got != want {
				t.Fatalf("got:\n%s\nwant:\n%s", got, want)
			}
			if strings.Contains(want, "FAIL ") {
				if err == nil {
					t.Fatal("the runner should fail")
				}
			} else if err != nil {
				if _, ok := err.(*exec.ExitError); !ok {
					t.Fatal(err)
				}
				t.Fatalf("runner: %v", err)
			}
		})
	}
}
