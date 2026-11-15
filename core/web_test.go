package core_test

import (
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/lang/cgen"
)

// The whole stack on the web: the Counter compiled to C, built with the
// core as WebAssembly (asuka.wasm), driven by the web host's JavaScript
// (platform/web) in headless Chrome, whose page presses Add and reports
// what it shows.
func TestWebCounter(t *testing.T) {
	if testing.Short() {
		t.Skip("headless Chrome (not -short)")
	}
	browser := chromePath()
	if browser == "" {
		t.Skip("no Chrome (ASUKA_CHROME)")
	}
	tc, err := cbuild.ForTarget(cbuild.Target{OS: "wasm", Arch: "wasm32"})
	if err != nil {
		t.Skip(err)
	}
	m, body := counterModule(t)
	m.App = int32(body)
	src, err := cgen.Generate(m)
	if err != nil {
		t.Fatal(err)
	}
	dir := t.TempDir()
	app := filepath.Join(dir, "app.c")
	os.WriteFile(app, src, 0o644)
	root, _ := filepath.Abs(".")
	wasm := filepath.Join(dir, "asuka.wasm")
	if err := tc.Lib(root, cbuild.Module, []string{app}, false, wasm); err != nil {
		t.Fatal(err)
	}
	page := `<!doctype html><meta charset="utf-8"><body><script>
const report = (m) => fetch("/result", { method: "POST", body: "ERROR " + m });
addEventListener("error", (e) => report(e.message + " " + (e.error && e.error.stack)));
addEventListener("unhandledrejection", (e) => report(String(e.reason && e.reason.stack || e.reason)));
const log = console.error; console.error = (...a) => { report(a.join(" ")); log(...a); };
</script><script type="module">
import { start } from "/src/asuka.js";
const host = await start({ core: "/asuka.wasm" });
const wait = (f) => new Promise((ok) => { const i = setInterval(() => { if (f()) { clearInterval(i); ok(); } }, 20); });
await wait(() => document.body.innerText.includes("Count: 0"));
const add = [...document.querySelectorAll("button")].find((b) => b.innerText.trim() === "Add");
add.click();
await wait(() => document.body.innerText.includes("Count: 1"));
await fetch("/result", { method: "POST", body: document.body.innerText });
</script>`
	results := make(chan string, 1)
	web, _ := filepath.Abs(filepath.Join("..", "platform", "web"))
	mux := http.NewServeMux()
	mux.Handle("/src/", http.FileServer(http.Dir(web)))
	mux.HandleFunc("/asuka.wasm", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/wasm")
		http.ServeFile(w, r, wasm)
	})
	mux.HandleFunc("/result", func(w http.ResponseWriter, r *http.Request) {
		b, _ := io.ReadAll(r.Body)
		results <- string(b)
	})
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) { io.WriteString(w, page) })
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	srv := &http.Server{Handler: mux}
	go srv.Serve(l)
	defer srv.Close()
	cmd := exec.Command(browser, "--headless=new", "--no-first-run", "--no-default-browser-check", "--disable-gpu",
		"--user-data-dir="+t.TempDir(), "--window-size=430,932", "http://"+l.Addr().String()+"/")
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	defer func() { cmd.Process.Kill(); cmd.Wait() }()
	select {
	case got := <-results:
		if strings.HasPrefix(got, "ERROR") || !strings.Contains(got, "Count: 1") {
			t.Fatalf("the page shows:\n%s", got)
		}
	case <-time.After(60 * time.Second):
		t.Fatal("the page never reported (it didn't load, or Add didn't count)")
	}
}

func chromePath() string {
	if p := os.Getenv("ASUKA_CHROME"); p != "" {
		return p
	}
	for _, p := range []string{"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "/Applications/Chromium.app/Contents/MacOS/Chromium"} {
		if _, err := os.Stat(p); err == nil {
			return p
		}
	}
	for _, n := range []string{"google-chrome", "google-chrome-stable", "chromium", "chromium-browser"} {
		if p, err := exec.LookPath(n); err == nil {
			return p
		}
	}
	return ""
}
