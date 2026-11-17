//go:build darwin

package mactest

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"testing"
	"time"
)

const shellCrashApp = `
	import platform/crash

	component Main {
	  state seen = ""
	  task {
	    for await r in crash.reports() {
	      seen = seen + r.kind + ":" + r.message + ";"
	    }
	  }
	  Column {
	    padding: 16
	    Text("ready")
	    Text("reports: {seen}")
	  }
	}
	app { Main() }
`

// A fatal signal is caught (on a stack of its own), written in the crash
// record's inbox with the crashed thread's frames, and the next launch
// reports it.
func TestShellCrashSignal(t *testing.T) {
	data := t.TempDir()
	env := []string{"ASUKA_DATA=" + data}
	a := RunWith(t, shellCrashApp, Options{Env: env})
	a.UntilText("ready")
	a.cmd.Process.Signal(syscall.SIGSEGV)
	if !a.Wait(10 * time.Second) {
		t.Fatal("the app didn't end")
	}
	files, _ := filepath.Glob(filepath.Join(data, "*", "inbox", "exception-*.json"))
	if len(files) != 1 {
		all, _ := filepath.Glob(filepath.Join(data, "*", "*"))
		t.Fatalf("the inbox: %v (%v)", files, all)
	}
	b, _ := os.ReadFile(files[0])
	var e struct {
		Name   string `json:"name"`
		PID    int    `json:"pid"`
		Run    string `json:"run"`
		Thread string `json:"thread"`
		Fatal  bool   `json:"fatal"`
		Frames []struct {
			Address  uint64 `json:"address"`
			Binary   string `json:"binary"`
			BinaryID string `json:"binaryId"`
		} `json:"frames"`
	}
	if err := json.Unmarshal(b, &e); err != nil {
		t.Fatalf("the report isn't JSON: %v\n%s", err, b)
	}
	if e.Name != "SIGSEGV" || e.PID != a.cmd.Process.Pid || e.Thread != "main" || !e.Fatal || e.Run == "" || len(e.Frames) == 0 {
		t.Fatalf("the report: %s", b)
	}
	if e.Frames[0].Binary == "" || len(e.Frames[0].BinaryID) != 36 {
		t.Fatalf("the crashed frame's binary: %s", b)
	}

	n := RunWith(t, shellCrashApp, Options{Env: env})
	n.Until(func(tr *Tree) bool {
		for _, x := range tr.Kind("Text") {
			if strings.Contains(x.Text, "exception:SIGSEGV") {
				return true
			}
		}
		return false
	})
}
