//go:build darwin

package mactest

import (
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// The shell's system modules the std packages call (System.swift,
// System2.swift, Documents.swift, Periodic.swift: share, compose,
// periodic, background, widget, liveActivity), called by name as the
// packages call them (asuka/platform's call), each call's answer or error
// shown in a Text: "label: ok <answer's hex>" (or, as text, "label: ok
// [<answer>]"), or "label: err <error>". What needs a user's click (a share
// picker's choice, a composer sent) is checked by its answer's shape and
// errors.

const sysCallSrc = `
	import asuka/platform

	component Sys(module: string, method: string, args: bytes) {
	  prop label = ""
	  prop text = false
	  state s = "pending"
	  task {
	    do {
	      let b = try await platform.call(module, method, args)
	      s = if text { "ok [{b.string()}]" } else { "ok {b.hex()}" }
	    } catch e {
	      s = "err {e.message}"
	    }
	  }
	  Text(if label.isEmpty() { "{module}.{method}: {s}" } else { "{label}: {s}" })
	}
`

// sysText is what the Text labeled label shows after its "label: ", once
// it isn't pending.
func sysText(t *testing.T, a *App, label string) string {
	t.Helper()
	var got string
	a.Until(func(tr *Tree) bool {
		for _, n := range tr.Kind("Text") {
			if s, ok := strings.CutPrefix(n.Text, label+": "); ok && s != "pending" {
				got = s
				return true
			}
		}
		return false
	})
	return got
}

// sysHex is the answer's bytes of an "ok" Text; it fails the test on an
// error.
func sysHex(t *testing.T, a *App, label string) []byte {
	t.Helper()
	s := sysText(t, a, label)
	h, ok := strings.CutPrefix(s, "ok")
	if !ok {
		t.Fatalf("%s: %s, want an answer", label, s)
	}
	b, err := hex.DecodeString(strings.ReplaceAll(strings.TrimSpace(h), " ", ""))
	if err != nil {
		t.Fatalf("%s: %q isn't hex", label, h)
	}
	return b
}

func sysWant(t *testing.T, a *App, label, want string) {
	t.Helper()
	if got := sysText(t, a, label); got != want {
		t.Errorf("%s: %q, want %q", label, got, want)
	}
}

func sysPrefix(t *testing.T, a *App, label, want string) {
	t.Helper()
	if got := sysText(t, a, label); !strings.HasPrefix(got, want) {
		t.Errorf("%s: %q, want it to start %q", label, got, want)
	}
}

// Every module and method the packages call is on macOS: none answers
// "isn't on macOS" (or that the host has no such module).
func TestShellSystemModulesThere(t *testing.T) {
	want := map[string][]string{
		"background":   {"update", "end", "next"},
		"widget":       {"set"},
		"liveActivity": {"enabled", "start", "update", "end", "next"},
		"periodic":     {"schedule", "cancel", "done"},
		"compose":      {"can", "mail", "message"},
		"share":        {"files"},
	}
	var calls strings.Builder
	var labels []string
	for mod, ms := range want {
		for _, m := range ms {
			// Arguments that answer at once (bad ones, mostly).
			fmt.Fprintf(&calls, "    Sys(%q, %q, \"x\".bytes())\n", mod, m)
			labels = append(labels, mod+"."+m)
		}
	}
	a := Run(t, sysCallSrc+`
		component App {
		  Scroll { Column {
	`+calls.String()+`
		  } }
		}
		app { App() }
	`)
	for _, l := range labels {
		if s := sysText(t, a, l); strings.Contains(s, "isn't on macOS") || strings.Contains(s, "no module") || strings.Contains(s, "unsupported") {
			t.Errorf("%s: %s", l, s)
		}
	}
}

// A background task holds an activity until the last one ends; "next"
// waits for it (and fails "ended" with none).
func TestShellBackgroundTasks(t *testing.T) {
	a := Run(t, sysCallSrc+`
		component Waits(module: string, method: string) {
		  state s = "waiting"
		  task {
		    do {
		      let _ = try await platform.call(module, method, bytes())
		      s = "answered"
		    } catch e {
		      s = "err {e.message}"
		    }
		  }
		  Text("waits {module}.{method}: {s}")
		}

		component App {
		  state phase = 0
		  Column {
		    Sys("background", "next", bytes()) { label: "next with none" }
		    Button("Start") { on press { phase = 1 } }
		    Button("End one") { on press { phase = 2 } }
		    Button("End two") { on press { phase = 3 } }
		    if phase >= 1 {
		      Sys("background", "start", "\{\"id\":1,\"title\":\"Sync\"}".bytes()) { label: "start 1" }
		      Sys("background", "start", "\{\"id\":2}".bytes()) { label: "start 2" }
		      Waits("background", "next")
		      Sys("background", "update", "\{\"id\":1,\"progress\":0.5}".bytes()) { label: "update" }
		    }
		    if phase >= 2 {
		      Sys("background", "end", "\{\"id\":1}".bytes()) { label: "end 1" }
		    }
		    if phase >= 3 {
		      Sys("background", "end", "\{\"id\":2}".bytes()) { label: "end 2" }
		    }
		  }
		}
		app { App() }
	`)
	sysWant(t, a, "next with none", "err ended")
	a.Do("tap Start")
	sysWant(t, a, "start 1", "ok ")
	sysWant(t, a, "start 2", "ok ")
	sysWant(t, a, "update", "ok ")
	a.Do("tap End one")
	sysWant(t, a, "end 1", "ok ")
	time.Sleep(300 * time.Millisecond)
	if a.Tree().FindText("waits background.next: waiting") == nil {
		t.Errorf("one task's end ended them all:\n%s", strings.Join(a.Texts(), "\n"))
	}
	a.Do("tap End two")
	a.UntilText("waits background.next: err ended")
}

// A Mac has no widgets nor live activities: none is enabled, and starting
// one fails.
func TestShellWidgetAndLiveActivity(t *testing.T) {
	a := Run(t, sysCallSrc+`
		component App {
		  Column {
		    Sys("widget", "set", "\{\"kind\":\"k\",\"entries\":[]}".bytes())
		    Sys("liveActivity", "enabled", bytes())
		    Sys("liveActivity", "start", "\{\"id\":\"a\",\"content\":\{}}".bytes())
		    Sys("liveActivity", "update", "\{\"id\":\"a\"}".bytes())
		    Sys("liveActivity", "end", "\{\"id\":\"a\"}".bytes())
		    Sys("liveActivity", "next", bytes())
		  }
		}
		app { App() }
	`)
	sysPrefix(t, a, "widget.set", "err not available")
	sysWant(t, a, "liveActivity.enabled", "ok 00")
	sysPrefix(t, a, "liveActivity.start", "err not available")
	sysWant(t, a, "liveActivity.update", "ok ")
	sysWant(t, a, "liveActivity.end", "ok ")
	sysPrefix(t, a, "liveActivity.next", "err not available")
}

// Periodic tasks are kept (UserDefaults' asuka.periodic) and scheduled; bad
// arguments fail; "next" waits for a turn.
func TestShellPeriodic(t *testing.T) {
	name := fmt.Sprintf("mactest-%d", time.Now().UnixNano())
	a := Run(t, sysCallSrc+`
		component App {
		  state cancel = false
		  Column {
		    Sys("periodic", "schedule", bytes([1])) { label: "bad" }
		    Sys("periodic", "schedule", platform.str("`+name+`").concat(platform.u32(3600)).concat(platform.u8(3))) { label: "schedule" }
		    Sys("periodic", "next", bytes()) { label: "next" }
		    Button("Cancel") { on press { cancel = true } }
		    if cancel { Sys("periodic", "cancel", "`+name+`".bytes()) { label: "cancel" } }
		  }
		}
		app { App() }
	`)
	sysWant(t, a, "bad", "err bad args")
	sysWant(t, a, "schedule", "ok ")
	// The tasks' JSON, as UserDefaults keeps it (data: base64 in the
	// exported plist).
	kept := func() string {
		plist := filepath.Join(t.TempDir(), "d.plist")
		exec.Command("defaults", "export", "dev.asuka.mactest", plist).Run()
		out, _ := exec.Command("plutil", "-extract", "asuka\\.periodic", "raw", "-o", "-", plist).Output()
		b, _ := base64.StdEncoding.DecodeString(strings.TrimSpace(string(out)))
		return string(b)
	}
	if k := kept(); !strings.Contains(k, `"`+name+`":{`) || !strings.Contains(k, `"charging":true`) {
		t.Errorf("the task wasn't kept: %s", k)
	}
	if a.Tree().FindText("next: pending") == nil {
		t.Errorf("next answered without a turn: %v", a.Texts())
	}
	a.Do("tap Cancel")
	sysWant(t, a, "cancel", "ok ")
	if k := kept(); strings.Contains(k, name) {
		t.Errorf("cancelled, the task is still kept")
	}
}

// compose says what it can, and turns down what the share service can't
// do (a mail's CC), and attachments that aren't there; share's files
// refuses bad arguments and files that aren't there.
func TestShellCompose(t *testing.T) {
	dir := t.TempDir()
	txt := filepath.Join(dir, "a.txt")
	os.WriteFile(txt, []byte("hi"), 0o644)
	q := func(s string) string { return strings.NewReplacer(`\`, `\\`, `"`, `\"`, "{", `\{`).Replace(s) }
	a := Run(t, sysCallSrc+`
		component App {
		  Column {
		    Sys("compose", "can", bytes())
		    Sys("compose", "mail", "`+q(`{"to":["a@example.com"],"cc":["b@example.com"],"attachments":[{"source":"`+txt+`","type":"text/plain","name":"a.txt"}]}`)+`".bytes()) { label: "mail cc" }
		    Sys("compose", "message", "`+q(`{"to":["+15550100"],"body":"hi","attachments":[{"source":"/nonexistent/b.txt"}]}`)+`".bytes()) { label: "missing" }
		    Sys("compose", "message", "nope".bytes()) { label: "bad" }
		    Sys("share", "files", bytes([1, 2])) { label: "share bad" }
		  }
		}
		app { App() }
	`)
	if b := sysHex(t, a, "compose.can"); len(b) != 4 || b[1] != 0 {
		t.Errorf("compose.can: %v", b)
	}
	sysWant(t, a, "mail cc", "err unavailable")
	sysWant(t, a, "missing", "err no file at /nonexistent/b.txt")
	sysWant(t, a, "bad", "err compose.message: bad arguments")
	sysWant(t, a, "share bad", "err share.files: bad arguments")
}
