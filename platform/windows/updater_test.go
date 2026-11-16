package windows_test

import (
	"crypto/ed25519"
	"encoding/base64"
	"fmt"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// The updater end to end, as e2e's TestUpdaterMac and TestUpdaterLinux do
// it on theirs: e2e/testdata/updater built at 1.0.0 and 1.0.1 with asuka
// build for Windows, its updates signed with the test's key and served on
// 127.0.0.1 in Windows (the update package takes HTTP only to the same
// computer: testdata/feed); 1.0.0, run from its folder, checks, downloads
// the delta from 1.0.0, installs it (the host's update module: the
// folder's files replaced, the running .exe set aside) and relaunches into
// 1.0.1, which says it's up to date.
func TestUpdater(t *testing.T) {
	w := onWindows(t)
	zig(t)
	local, guest := w.job(t)
	root, err := filepath.Abs(filepath.Join("..", ".."))
	if err != nil {
		t.Fatal(err)
	}
	port := 20000 + time.Now().Nanosecond()%20000
	pub, priv, _ := ed25519.GenerateKey(nil)
	env := append(os.Environ(),
		"ASUKA_UPDATE_PUBLIC_KEY="+base64.StdEncoding.EncodeToString(pub),
		"ASUKA_UPDATE_PRIVATE_KEY="+base64.StdEncoding.EncodeToString(priv),
		fmt.Sprintf("ASUKA_UPDATE_URL=http://127.0.0.1:%d", port),
		"ASUKA_WINDOWS_CERT=",
	)
	feed := filepath.Join(local, "feed")
	os.MkdirAll(feed, 0o755)
	build := func(version string) string {
		out := filepath.Join(t.TempDir(), "dist")
		c := exec.Command("go", "run", "./cmd/asuka", "build", "--platform", "windows", "--arch", w.arch, "--version", version, "-o", out, "e2e/testdata/updater")
		c.Dir, c.Env = root, env
		b, err := c.CombinedOutput()
		if err != nil {
			t.Fatalf("asuka build %s: %v\n%s", version, err, b)
		}
		// The update files, the manifest last.
		files, _ := filepath.Glob(filepath.Join(out, "updates", "*"))
		var manifests []string
		for _, p := range files {
			if strings.HasSuffix(p, ".json") {
				manifests = append(manifests, p)
				continue
			}
			copyTo(t, p, filepath.Join(feed, filepath.Base(p)))
		}
		for _, p := range manifests {
			copyTo(t, p, filepath.Join(feed, filepath.Base(p)))
		}
		return out
	}
	// The builds read the published feed at the same address here (1.0.1's
	// delta from 1.0.0's archive).
	ln, err := net.Listen("tcp", fmt.Sprintf("127.0.0.1:%d", port))
	if err != nil {
		t.Fatal(err)
	}
	srv := &http.Server{Handler: http.FileServer(http.Dir(feed))}
	go srv.Serve(ln)
	v100 := build("1.0.0")
	build("1.0.1")
	srv.Close()
	// 1.0.0 installed: its folder.
	app := filepath.Join(local, "app")
	copyTree(t, filepath.Join(v100, "Updater"), app)

	// The feed's server, in the signed-in session as the app is.
	server := filepath.Join(local, "feed.exe")
	c := exec.Command("go", "build", "-o", server, "./testdata/feed")
	c.Env = append(os.Environ(), "GOOS=windows", "GOARCH="+w.arch, "CGO_ENABLED=0")
	if b, err := c.CombinedOutput(); err != nil {
		t.Fatalf("the feed's server: %v\n%s", err, b)
	}
	task := "asuka-feed-" + filepath.Base(local)
	ps := fmt.Sprintf(`$a = New-ScheduledTaskAction -Execute '%s\feed.exe' -Argument '"%s\feed" %d "%s\gets.txt"'; `+
		`Register-ScheduledTask -TaskName '%s' -Action $a -User $env:USERNAME -RunLevel Limited -Force | Out-Null; Start-ScheduledTask -TaskName '%s'`,
		guest, guest, port, guest, task, task)
	if out, err := exec.Command("swiftvm", "exec", w.vm, "--", ps).CombinedOutput(); err != nil && w.vm != "" {
		t.Fatalf("starting the feed: %v\n%s", err, out)
	}
	defer func() {
		os.WriteFile(filepath.Join(feed, "stop"), nil, 0o644)
		exec.Command("swiftvm", "exec", w.vm, "--", fmt.Sprintf(`Unregister-ScheduledTask -TaskName '%s' -Confirm:$false`, task)).Run()
	}()

	// 1.0.0 in its self-update mode: it quits as it relaunches.
	data := filepath.Join(local, "data")
	os.MkdirAll(data, 0o755)
	os.WriteFile(filepath.Join(data, "selfupdate"), nil, 0o644)
	out, err := w.run(t, guest+`\app`, guest+`\app\Updater.exe`, map[string]string{"ASUKA_DATA": guest + `\data`})
	if err != nil {
		b, _ := os.ReadFile(filepath.Join(data, "report"))
		t.Fatalf("1.0.0: %v\n%s\nit reported:\n%s", err, out, b)
	}
	var got string
	for end := time.Now().Add(90 * time.Second); time.Now().Before(end); time.Sleep(300 * time.Millisecond) {
		b, _ := os.ReadFile(filepath.Join(data, "report"))
		if got = string(b); strings.Contains(got, "launched 1.0.1") && strings.Contains(got, "up to date") {
			break
		}
	}
	t.Logf("the app reported:\n%s", got)
	for _, want := range []string{"launched 1.0.0", "found 1.0.1", "installed 1.0.1", "launched 1.0.1", "up to date"} {
		if !strings.Contains(got, want) {
			t.Fatalf("the app didn't report %q:\n%s\n1.0.0's output:\n%s", want, got, out)
		}
	}
	// The server's log, as the share shows it (it may lag).
	var gets []byte
	for end := time.Now().Add(15 * time.Second); time.Now().Before(end); time.Sleep(300 * time.Millisecond) {
		if gets, _ = os.ReadFile(filepath.Join(local, "gets.txt")); strings.Count(string(gets), "\n") >= 3 {
			break
		}
	}
	if !strings.Contains(string(gets), "-1.0.0-to-1.0.1-windows-"+w.arch+".delta") || strings.Contains(string(gets), "updater-1.0.1-windows-"+w.arch+".tar.gz") {
		t.Errorf("downloaded %s, want the delta alone", gets)
	}
	if list, _ := os.ReadFile(filepath.Join(app, ".asuka-files")); !strings.Contains(string(list), "Updater.exe") {
		t.Errorf("the folder's file list:\n%s", list)
	}
	// What the update left goes at the next launch: 1.0.1's ran.
	if left, _ := filepath.Glob(filepath.Join(app, ".asuka-update-*")); len(left) > 0 {
		t.Errorf("left %v", left)
	}
}

func copyTo(t *testing.T, from, to string) {
	t.Helper()
	b, err := os.ReadFile(from)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(to, b, 0o755); err != nil {
		t.Fatal(err)
	}
}

func copyTree(t *testing.T, from, to string) {
	t.Helper()
	err := filepath.Walk(from, func(p string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		rel, _ := filepath.Rel(from, p)
		dst := filepath.Join(to, rel)
		if info.IsDir() {
			return os.MkdirAll(dst, 0o755)
		}
		copyTo(t, p, dst)
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
}
