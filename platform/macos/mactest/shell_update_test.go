//go:build darwin

package mactest

import (
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"
)

// updateApp is an app that calls the "update" module (platform/host's
// call) as its buttons are pressed, and shows each answer (or "error: "
// and why) as "out: ...".
func shellUpdateApp(buttons string) string {
	return `
		import platform/host
		import std/json

		struct DownloadArgs {
		  url: string
		  to: string
		}
		struct ID {
		  id: int
		}
		struct Staged {
		  staged: string
		}
		struct Progress {
		  downloaded: int
		  total: int
		  done: bool
		  error: string
		  status: int
		}

		async fn call(method: string, args: string) -> string {
		  do {
		    let b = try await host.call("update", method, args.bytes())
		    return b.string()
		  } catch e {
		    return "error: " + e.message
		  }
		}

		component Main {
		  state out = ""
		  Column {
		    gap: 8; padding: 16
		    Text("out: {out}")
		    ` + buttons + `
		  }
		}
		app { Main() }
	`
}

// out waits for the app's answer, which has prefix, and returns it.
func shellOut(t *testing.T, a *App, prefix string) string {
	t.Helper()
	return shellOutWith(t, a, func(s string) bool { return s != "" && strings.HasPrefix(s, prefix) })
}

// outWith waits for the app's answer that ok holds of.
func shellOutWith(t *testing.T, a *App, ok func(string) bool) string {
	t.Helper()
	var got string
	a.Until(func(tr *Tree) bool {
		for _, n := range tr.Kind("Text") {
			if s, has := strings.CutPrefix(n.Text, "out: "); has && ok(s) {
				got = s
				return true
			}
		}
		return false
	})
	return got
}

// info says whether the build can install updates itself, why not, and
// what the package needs to install one: the bundle, the running
// executable, the work directory beside the bundle, the target.
func TestShellUpdateInfo(t *testing.T) {
	type info struct {
		Installable           bool
		Reason, Message       string
		Target, Version       string
		Name, Feed, Key       string
		Kind, Root, Exe, Work string
	}
	read := func(a *App) info {
		a.Do("tap Info")
		s := shellOut(t, a, "{")
		var i info
		if err := json.Unmarshal([]byte(s), &i); err != nil {
			t.Fatalf("info %q: %v", s, err)
		}
		return i
	}
	src := shellUpdateApp(`Button("Info") { on press { out = await call("info", "") } }`)
	// The player has no feed or key (only asuka build's builds do).
	i := read(Run(t, src))
	if i.Installable || i.Reason != "notConfigured" || i.Message == "" || i.Target != "darwin-"+runtime.GOARCH {
		t.Fatalf("the player: %+v", i)
	}
	if i.Kind != "bundle" || !strings.HasSuffix(i.Root, ".app") || !strings.HasPrefix(i.Exe, i.Root+"/Contents/MacOS/") || i.Work != filepath.Dir(i.Root) || i.Name == "" {
		t.Fatalf("where the player is: %+v", i)
	}
	// Configured, in a folder of the test's: it can.
	cfg := filepath.Join(t.TempDir(), "asuka-app.json")
	os.WriteFile(cfg, []byte(`{"name":"Mac Test","version":"1.0.0","updateFeed":"https://example.com/u","updateKey":"a2V5","updateTarget":"darwin-arm64"}`), 0o644)
	i = read(RunWith(t, src, Options{Env: []string{"ASUKA_APP_CONFIG=" + cfg}}))
	if !i.Installable || i.Reason != "" || i.Feed != "https://example.com/u" || i.Key != "a2V5" {
		t.Fatalf("configured: %+v", i)
	}
}

// A download from a local server, its progress as it moves, the file where
// it's asked to go; and one canceled.
func TestShellUpdateDownload(t *testing.T) {
	const size = 2 << 20
	release := make(chan struct{})
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("User-Agent") != "asuka-updater" {
			http.Error(w, "who are you", http.StatusForbidden)
			return
		}
		if r.URL.Path == "/missing" {
			http.Error(w, "no such file", http.StatusNotFound)
			return
		}
		w.Header().Set("Content-Length", fmt.Sprint(size))
		chunk := make([]byte, 64<<10)
		for i := range chunk {
			chunk[i] = byte(i)
		}
		f, _ := w.(http.Flusher)
		for sent := 0; sent < size; sent += len(chunk) {
			if r.URL.Path == "/stuck" && sent >= 4*len(chunk) {
				select {
				case <-release:
				case <-r.Context().Done():
				}
				return
			}
			w.Write(chunk)
			if f != nil {
				f.Flush()
			}
			time.Sleep(10 * time.Millisecond)
		}
	}))
	defer srv.Close()
	defer close(release)
	dir := t.TempDir()
	to := filepath.Join(dir, "sub", "update.tar.gz")
	a := Run(t, shellUpdateApp(fmt.Sprintf(`
		Button("Download") {
		  on press {
		    let s = await call("download", json.encode(DownloadArgs(url: %q, to: %q)))
		    var answers = 0
		    var last = ""
		    while true {
		      last = await call("next", s)
		      answers += 1
		      if last.hasPrefix("error") { break }
		      if let p = try? json.decode<Progress>(last) {
		        if p.done { break }
		      } else {
		        break
		      }
		    }
		    out = "{answers} {last}"
		  }
		}
		Button("Cancel") {
		  on press {
		    let s = await call("download", json.encode(DownloadArgs(url: %q, to: %q)))
		    let first = await call("next", s)
		    let _ = await call("cancel", s)
		    let last = await call("next", s)
		    let again = await call("next", s)
		    out = "canceled {last} | {again} | {first}"
		  }
		}
		Button("Missing") {
		  on press {
		    let s = await call("download", json.encode(DownloadArgs(url: %q, to: %q)))
		    var last = ""
		    while true {
		      last = await call("next", s)
		      if last.hasPrefix("error") { break }
		      if let p = try? json.decode<Progress>(last) { if p.done { break } } else { break }
		    }
		    out = "missing {last}"
		  }
		}
		Button("Plain") {
		  on press { out = await call("download", json.encode(DownloadArgs(url: "http://example.com/a", to: %q))) }
		}
	`, srv.URL+"/file", to, srv.URL+"/stuck", filepath.Join(dir, "stuck"), srv.URL+"/missing", filepath.Join(dir, "missing"), filepath.Join(dir, "plain"))))

	a.Do("tap Download")
	s := shellOutWith(t, a, func(s string) bool { return strings.Contains(s, "done") })
	var answers int
	var last string
	fmt.Sscanf(s, "%d", &answers)
	_, last, _ = strings.Cut(s, " ")
	var p struct {
		Downloaded, Total, Status int
		Done                      bool
		Error                     string
	}
	if err := json.Unmarshal([]byte(last), &p); err != nil {
		t.Fatalf("next %q: %v", s, err)
	}
	if !p.Done || p.Error != "" || p.Downloaded != size || p.Total != size || p.Status != 200 {
		t.Fatalf("the download ended %s", s)
	}
	// It moved in steps of 1%: more than its start and its end, at most a
	// hundred and its end.
	if answers < 3 || answers > 102 {
		t.Fatalf("%d answers to next: %s", answers, s)
	}
	if st, err := os.Stat(to); err != nil || st.Size() != size {
		t.Fatalf("the file: %v %v", st, err)
	}

	a.Do("tap Cancel")
	s = shellOut(t, a, "canceled")
	if !strings.Contains(s, `"done":true`) || !strings.Contains(s, `"error":"canceled"`) {
		t.Fatalf("canceled: %s", s)
	}
	if !strings.Contains(s, "| error: ") {
		t.Fatalf("a download told it ended is gone: %s", s)
	}
	if st, err := os.Stat(filepath.Join(dir, "stuck")); err == nil && st.Size() != 0 {
		t.Fatal("a canceled download's file has its bytes")
	}

	// Another status than 200: the download ends, its status said, the
	// file empty.
	a.Do("tap Missing")
	s = shellOut(t, a, "missing")
	if !strings.Contains(s, `"status":404`) || !strings.Contains(s, `"done":true`) || !strings.Contains(s, `"error":""`) {
		t.Fatalf("a 404: %s", s)
	}
	if st, err := os.Stat(filepath.Join(dir, "missing")); err != nil || st.Size() != 0 {
		t.Fatalf("a 404's file: %v %v", st, err)
	}

	// Not HTTPS (nor the computer's own): refused.
	a.Do("tap Plain")
	if s := shellOut(t, a, "error"); !strings.Contains(s, "not an HTTPS URL") {
		t.Fatalf("plain http: %s", s)
	}
}

// signedCopy copies an app bundle to dir and signs it ad hoc as identifier.
func shellSignedCopy(t *testing.T, bundle, dir, identifier string, marker bool) string {
	t.Helper()
	return shellSignedAs(t, bundle, dir, identifier, "-", marker)
}

// signedAs copies an app bundle to dir and signs it as identifier with
// the identity ("-": ad hoc).
func shellSignedAs(t *testing.T, bundle, dir, identifier, identity string, marker bool) string {
	t.Helper()
	dst := filepath.Join(dir, filepath.Base(bundle))
	os.MkdirAll(dir, 0o755)
	if b, err := exec.Command("ditto", bundle, dst).CombinedOutput(); err != nil {
		t.Fatalf("ditto: %v\n%s", err, b)
	}
	if marker {
		os.WriteFile(filepath.Join(dst, "Contents", "Resources", "asuka-update-marker"), []byte("new"), 0o644)
	}
	if b, err := exec.Command("codesign", "--force", "--deep", "-s", identity, "--identifier", identifier, dst).CombinedOutput(); err != nil {
		t.Fatalf("codesign: %v\n%s", err, b)
	}
	return dst
}

// A code signing identity of this computer's keychain, or "" if it has
// none: its SHA-1.
func shellIdentity() string {
	out, _ := exec.Command("security", "find-identity", "-v", "-p", "codesigning").Output()
	for _, line := range strings.Split(string(out), "\n") {
		if f := strings.Fields(line); len(f) >= 2 && len(f[1]) == 40 {
			return f[1]
		}
	}
	return ""
}

// staged copies a bundle into a new work directory of the package's
// beside the installed one (.<Name>.app.update-<id>/app/<Name>.app), as
// platform/update stages a verified update, and returns the copy.
func shellStaged(t *testing.T, installed, bundle, id string) string {
	t.Helper()
	dir := filepath.Join(filepath.Dir(installed), "."+filepath.Base(installed)+".update-"+id, "app")
	os.MkdirAll(dir, 0o755)
	dst := filepath.Join(dir, filepath.Base(installed))
	if b, err := exec.Command("ditto", bundle, dst).CombinedOutput(); err != nil {
		t.Fatalf("ditto: %v\n%s", err, b)
	}
	return dst
}

// install refuses an update signed as another app, by another signer (a
// team, where the computer has an identity to sign with), whose signature
// doesn't hold, or not staged beside the app; and puts one of the app's
// own in place of its bundle once it has quit. At launch, the host
// removes what an update left whose helper isn't running.
func TestShellUpdateInstall(t *testing.T) {
	exe := Player(t)
	player := filepath.Dir(filepath.Dir(filepath.Dir(exe)))
	tmp := t.TempDir()
	running := shellSignedCopy(t, player, filepath.Join(tmp, "installed"), "dev.asuka.mactest", false)
	// The bundles, signed in folders of their own, then staged beside the
	// app once it has started (as platform/update does: the host cleans what
	// it finds there as it starts).
	otherSigned := shellSignedCopy(t, player, filepath.Join(tmp, "other"), "com.example.other", false)
	tamperedSigned := shellSignedCopy(t, player, filepath.Join(tmp, "tampered"), "dev.asuka.mactest", false)
	teamSigned := ""
	if id := shellIdentity(); id != "" {
		teamSigned = shellSignedAs(t, player, filepath.Join(tmp, "team"), "dev.asuka.mactest", id, false)
	}
	signed := shellSignedCopy(t, player, filepath.Join(tmp, "good"), "dev.asuka.mactest", true)
	stagedAt := func(id string) string {
		return filepath.Join(filepath.Dir(running), "."+filepath.Base(running)+".update-"+id, "app", filepath.Base(running))
	}
	other, tampered, good, team := stagedAt("other"), stagedAt("tampered"), stagedAt("good"), ""
	if teamSigned != "" {
		team = stagedAt("team")
	}
	// What a run before left: its helper gone.
	dead := filepath.Join(filepath.Dir(running), "."+filepath.Base(running)+".update-dead")
	os.MkdirAll(dead, 0o755)
	os.WriteFile(filepath.Join(dead, "staged"), []byte("999999"), 0o644)

	a := RunWith(t, shellUpdateApp(fmt.Sprintf(`
		Button("Other") { on press { out = await call("install", json.encode(Staged(staged: %q))) } }
		Button("Good") { on press { out = await call("install", json.encode(Staged(staged: %q))) } }
		Button("Tampered") { on press { out = await call("install", json.encode(Staged(staged: %q))) } }
		Button("Team") { on press { out = await call("install", json.encode(Staged(staged: %q))) } }
		Button("Elsewhere") { on press { out = await call("install", json.encode(Staged(staged: %q))) } }
		Button("Clear") { on press { out = "" } }
	`, other, good, tampered, team, signed)), Options{Player: filepath.Join(running, "Contents", "MacOS", filepath.Base(exe))})

	deadline := time.Now().Add(30 * time.Second)
	for {
		if _, err := os.Stat(dead); os.IsNotExist(err) {
			break
		}
		if time.Now().After(deadline) {
			t.Fatal("what an update left isn't removed at launch")
		}
		time.Sleep(100 * time.Millisecond)
	}
	shellStaged(t, running, otherSigned, "other")
	shellStaged(t, running, tamperedSigned, "tampered")
	os.WriteFile(filepath.Join(tampered, "Contents", "Info.plist"), []byte("<plist></plist>"), 0o644)
	if teamSigned != "" {
		shellStaged(t, running, teamSigned, "team")
	}
	shellStaged(t, running, signed, "good")
	a.Do("tap Other")
	if s := shellOut(t, a, "error"); !strings.Contains(s, "the update is the app com.example.other, not dev.asuka.mactest") {
		t.Fatalf("installing another app's bundle: %s", s)
	}
	a.Do("tap Clear", "tap Tampered")
	if s := shellOut(t, a, "error"); !strings.Contains(s, "the update's code signature isn't valid") {
		t.Fatalf("installing a bundle changed after it was signed: %s", s)
	}
	a.Do("tap Clear", "tap Elsewhere")
	if s := shellOut(t, a, "error"); !strings.Contains(s, "isn't staged beside the app") {
		t.Fatalf("installing a bundle not staged beside the app: %s", s)
	}
	if team != "" {
		a.Do("tap Clear", "tap Team")
		if s := shellOut(t, a, "error"); !strings.Contains(s, "the update is signed by team ") || !strings.Contains(s, "not the app's not set") {
			t.Fatalf("installing a bundle another team signed: %s", s)
		}
	} else {
		t.Log("no code signing identity: another team's signature isn't tried")
	}
	a.Do("tap Clear")
	a.Do("tap Good")
	if s := shellOut(t, a, "{"); s != "{}" {
		t.Fatalf("install: %s", s)
	}
	marker := filepath.Join(running, "Contents", "Resources", "asuka-update-marker")
	if _, err := os.Stat(marker); err == nil {
		t.Fatal("the bundle was replaced while the app runs")
	}
	// Once the app has quit, its helper puts the new bundle in place.
	a.Stop()
	deadline = time.Now().Add(30 * time.Second)
	for {
		if _, err := os.Stat(marker); err == nil {
			break
		}
		if time.Now().After(deadline) {
			t.Fatal("the update wasn't put in place after the app quit")
		}
		time.Sleep(100 * time.Millisecond)
	}
	if b, err := exec.Command("codesign", "--verify", "--deep", "--strict", running).CombinedOutput(); err != nil {
		t.Fatalf("the installed update's signature: %v\n%s", err, b)
	}
	// Its work directory is gone; the ones install refused are the
	// package's to remove (it removes what fails).
	for time.Now().Before(deadline) {
		if _, err := os.Stat(filepath.Dir(filepath.Dir(good))); os.IsNotExist(err) {
			return
		}
		time.Sleep(100 * time.Millisecond)
	}
	t.Fatalf("left behind: %s", filepath.Dir(filepath.Dir(good)))
}
