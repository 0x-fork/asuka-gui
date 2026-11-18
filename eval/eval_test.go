package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/egoist/asuka/lang/compile"
)

// Every task can be done: its reference program passes its tests on the
// fake host, and reports no diagnostic at run time either. (The eval
// itself needs a model: go run ./eval.)
func TestTasksHaveAnswers(t *testing.T) {
	runner, err := compile.Runner(false)
	if err != nil {
		t.Skip(err)
	}
	tasks, _ := filepath.Glob(filepath.Join("tasks", "*"))
	if len(tasks) < 12 {
		t.Fatalf("%d tasks", len(tasks))
	}
	for _, task := range tasks {
		t.Run(filepath.Base(task), func(t *testing.T) {
			t.Parallel()
			for _, f := range []string{"prompt.md", "eval_test.asuka", "reference.asuka"} {
				if _, err := os.Stat(filepath.Join(task, f)); err != nil {
					t.Fatal(err)
				}
			}
			ref, _ := os.ReadFile(filepath.Join(task, "reference.asuka"))
			test, _ := os.ReadFile(filepath.Join(task, "eval_test.asuka"))
			res := compile.Files(map[string][]byte{"app.asuka": ref, "eval_test.asuka": test}, nil)
			if err := res.Err(); err != nil {
				t.Fatal(err)
			}
			out, err := compile.Run(runner, res.Module, true, "", "-diag-json")
			if err != nil || strings.Contains(out, "FAIL") || !strings.Contains(out, "ok eval: ") || strings.Contains(out, `{"diag":`) {
				t.Fatalf("%v:\n%s", err, out)
			}
		})
	}
}
