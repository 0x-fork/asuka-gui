package desktop_test

import (
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"testing"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/lang/compile"
	"github.com/egoist/asuka/std"
)

// The standard library's C on this computer's desktop, run with no Go:
// each case is an Asuka program that uses a package through its API on
// the test host (test/host.c), every std package's C linked in as in a
// dev player, and what it shows is checked. Linux only (the C is the
// Linux code; run it in the asuka-linux-test image, under xvfb-run and
// dbus-run-session for the cases that need a display or a bus).
func TestStdLinux(t *testing.T) {
	if runtime.GOOS != "linux" {
		t.Skip("the standard library's Linux C (run in the asuka-linux-test image)")
	}
	exe := stdHost(t)
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/hello":
			w.Header().Set("X-Asuka", "yes")
			fmt.Fprintf(w, "hello %s %s", r.Method, r.Header.Get("X-Who"))
		case "/moved":
			http.Redirect(w, r, "/hello", http.StatusFound)
		case "/signin":
			fmt.Fprint(w, `<!doctype html><title>Sign in</title><script>setTimeout(() => location.href = "myapp://done?code=42", 50)</script>`)
		case "/echo":
			b := make([]byte, r.ContentLength)
			r.Body.Read(b)
			w.Write(b)
		default:
			http.NotFound(w, r)
		}
	}))
	defer srv.Close()
	for _, c := range []struct {
		name, src string
		want      []string
	}{
		{"store", `import asuka/store

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    store.write("count", 3)
    store.write("name", "Ayu \"quoted\" ü")
    store.flush()
    step = "read {store.read("count", 0)} {store.read("name", "")} {store.read("none", "def")}"
    await sleep(1ms)
    store.remove("count")
    store.flush()
    step = "removed {store.read("count", 0)} in {store.dir().count() > 0}"
    await sleep(1ms)
    step = "done"
  }
}

app { Probe() }
`, []string{`host: read 3 Ayu "quoted" ü def`, "host: removed 0 in true", "host: done"}},
		{"http", `import asuka/http

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    do {
      let r = try await http.get("` + srv.URL + `/moved", headers: ["X-Who": "probe"])
      step = "got {r.status} {r.text()} {r.headers["x-asuka"] ?? "-"} {r.url.hasSuffix("/hello")}"
      await sleep(1ms)
      let p = try await http.post("` + srv.URL + `/echo", body: "ping".bytes())
      step = "posted {p.status} {p.text()}"
      await sleep(1ms)
      let n = try await http.get("` + srv.URL + `/none")
      step = "missing {n.status}"
      await sleep(1ms)
      step = "done"
    } catch e {
      step = "fail {e.message}"
    }
  }
}

app { Probe() }
`, []string{"host: got 200 hello GET probe yes true", "host: posted 200 ping", "host: missing 404", "host: done"}},
		{"monitors", `import asuka/network
import asuka/power
import asuka/appearance
import asuka/screen

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    for await s in network.changes() {
      step = "network {s.type != .none || !s.connected}"
      break
    }
    await sleep(1ms)
    for await s in power.changes() {
      step = "power {s.level} {s.battery}"
      break
    }
    await sleep(1ms)
    for await a in appearance.changes() {
      step = "appearance {a.increaseContrast}"
      break
    }
    await sleep(1ms)
    for await ds in screen.changes() {
      step = "screens {ds.count() > 0} {ds.count() == 0 || ds[0].primary} {ds.count() == 0 || ds[0].bounds.width > 0}"
      break
    }
    await sleep(1ms)
    step = "done"
  }
}

app { Probe() }
`, []string{"host: network true", "host: power -1 false", "host: appearance false", "host: screens true true true", "host: done"}},
		{"device", `import asuka/device
import asuka/sensors
import asuka/secure
import asuka/shell

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    do {
      let i = try await device.info()
      step = "info {i.model} {i.system} {i.appVersion} {i.build} {i.systemVersion.count() > 0}"
      await sleep(1ms)
      step = "sensors {sensors.available(.accelerometer)} {sensors.available(.gyroscope)}"
      await sleep(1ms)
      do {
        try await secure.write("token", "s3cret")
        let back = try await secure.read("token")
        try await secure.remove("token")
        let gone = try await secure.read("token")
        step = "secure {back ?? "-"} {gone ?? "none"}"
      } catch e {
        step = "secure failed: {e.message.contains("keyring") || e.message.contains("libsecret")}"
      }
      await sleep(1ms)
      shell.beep()
      do {
        try await shell.open("/no/such/file")
      } catch e {
        step = "shell {e.message}"
      }
      await sleep(1ms)
      step = "done"
    } catch e {
      step = "fail {e.message}"
    }
  }
}

app { Probe() }
`, []string{"host: info PC Linux 1.2.3 45 true", "host: sensors false false", "host: secure", "host: shell no file at /no/such/file", "host: done"}},
		{"watch", `import asuka/watch
import asuka/fs

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    do {
      let dir = fs.join([fs.appDir(), "watched"])
      try fs.mkdirAll(dir)
      let w = try await watch.path(dir, ignore: [".*"])
      step = "watching {w.root.hasSuffix("/watched")}"
      await sleep(1ms)
      try fs.writeText(fs.join([dir, ".hidden"]), "h")
      try fs.writeText(fs.join([dir, "a.txt"]), "a")
      for await events in w.events {
        for e in events {
          if e.op == .create && e.path == "a.txt" {
            step = "created {e.path}"
            await sleep(1ms)
            try fs.rename(fs.join([dir, "a.txt"]), fs.join([dir, "b.txt"]))
          } else if e.op == .rename {
            step = "renamed {e.oldPath} {e.path}"
            await sleep(1ms)
            try fs.writeText(fs.join([dir, "b.txt"]), "more")
          } else if e.op == .write {
            step = "wrote {e.path}"
            await sleep(1ms)
            try fs.removeAll(dir)
          } else if e.op == .remove && e.path == "." {
            step = "gone"
            await sleep(1ms)
          } else if e.op == .create {
            step = "fail: created {e.path}"
          }
        }
      }
      step = "done"
    } catch e {
      step = "fail {e.message}"
    }
  }
}

app { Probe() }
`, []string{"host: watching true", "host: created a.txt", "host: renamed a.txt b.txt", "host: wrote b.txt", "host: gone", "host: done"}},
		{"js", `import asuka/js

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    do {
      let a = try await js.eval("1 + 2")
      let b = try await js.eval(r"new Promise(res => setTimeout(() => res({x: [1, 'two']}), 10))")
      step = "js {a} {b}"
      await sleep(1ms)
      do {
        let _ = try await js.eval("throw new Error('nope')")
      } catch e {
        step = "threw {e.message}"
        await sleep(1ms)
      }
      do {
        let _ = try await js.eval(r"new Promise(() => {})", timeout: 300ms)
      } catch e {
        step = "late {e.message}"
        await sleep(1ms)
      }
      step = "done"
    } catch e {
      step = "fail {e.message}"
    }
  }
}

app { Probe() }
`, []string{`host: js 3 {"x":[1,"two"]}`, "host: threw Error: nope", "host: late", "host: done"}},
		{"webauth", `import asuka/webauth

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    do {
      let back = try await webauth.open("` + srv.URL + `/signin", callbackScheme: "myapp", ephemeral: true)
      step = "signed in {back}"
      await sleep(1ms)
      step = "done"
    } catch e {
      step = "fail {e.message}"
    }
  }
}

app { Probe() }
`, []string{"host: signed in myapp://done?code=42", "host: done"}},
		{"markdown", `import asuka/markdown

component Probe {
  state step = "start"
  Column {
    Text("#{step}")
    markdown.View("# Plan\n\nThree **steps**, *in* ` + "`order`" + `:\n\n1. Read [docs](https://asuka.dev)\n2. Write\n\n| a | b |\n|---|---|\n| 1 | 2 |\n") {
      width: 240
    }
  }
  task {
    await sleep(20ms)
    step = "healed {markdown.heal("**bold and ` + "`code" + `")}"
    await sleep(1ms)
    step = "done"
  }
}

app { Probe() }
`, []string{"host: native view asuka/markdown#Document", "host: measured asuka/markdown#Document 240x", "host: healed **bold and `code`**", "host: done"}},
		{"svg", `import asuka/svg

let tick = svg.Icon(r"""<svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 12l5 5L20 6"/><rect x="2" y="2" width="20" height="20" rx="4"/></svg>""")

component Probe {
  state step = "start"
  state color: Color = .accent
  Column {
    Text("#{step}")
    svg.View(tick) { color: color }
    svg.Shape([svg.Layer(path: svg.Path().rect(svg.Point(), svg.Point(fx: 1, fy: 1), radius: 6), fill: .red, fade: .blue)]) {
      width: 100
      height: 40
    }
  }
  task {
    await sleep(20ms)
    color = .label
    await sleep(20ms)
    step = "done"
  }
}

app { Probe() }
`, []string{"host: native view asuka/svg#View", "host: native view asuka/svg#Shape", "host: measured asuka/svg#View 22x22", "host: done"}},
	} {
		t.Run(c.name, func(t *testing.T) {
			dir := t.TempDir()
			os.WriteFile(filepath.Join(dir, "app.asuka"), []byte(c.src), 0o644)
			res, err := compile.Dir(dir, nil)
			if err != nil {
				t.Fatal(err)
			}
			if err := res.Err(); err != nil {
				t.Fatal(err)
			}
			mod := filepath.Join(dir, "app.akb")
			os.WriteFile(mod, res.Module.Encode(), 0o644)
			cmd := exec.Command(exe, mod)
			cmd.Env = append(os.Environ(), "ASUKA_DATA="+dir, noLeaks, "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1") // WebKit's sandbox doesn't start in a container
			out, err := cmd.CombinedOutput()
			got := string(out)
			if err != nil {
				t.Fatalf("%v\n%s", err, got)
			}
			for _, w := range c.want {
				if !strings.Contains(got, w) {
					t.Errorf("no %q in:\n%s", w, got)
				}
			}
		})
	}
}

var (
	stdOnce sync.Once
	stdExe  string
	stdErr  error
)

// stdHost builds the test host with every standard package's C for this
// computer's desktop (ASan, UBSan), once.
func stdHost(t *testing.T) string {
	stdOnce.Do(func() {
		core, err := cbuild.CoreDir()
		if err != nil {
			stdErr = err
			return
		}
		d, err := std.Dir()
		if err != nil {
			stdErr = err
			return
		}
		dirs, _ := filepath.Glob(filepath.Join(d, "*", "desktop"))
		var pkgs []string
		for _, x := range dirs {
			pkgs = append(pkgs, filepath.Dir(x))
		}
		natives, err := appbuild.DesktopNatives(cbuild.Host(), pkgs)
		if err != nil {
			stdErr = err
			return
		}
		desk, _ := filepath.Abs(".")
		srcs := []string{filepath.Join(desk, "test", "host.c")}
		for _, s := range natives.Sources {
			if strings.HasSuffix(s, ".c") {
				srcs = append(srcs, s)
			}
		}
		work, err := os.MkdirTemp("", "asuka-std-host")
		if err != nil {
			stdErr = err
			return
		}
		flags := append(append([]string{}, cbuild.Flags...), natives.Flags...)
		stdExe, stdErr = cbuild.Exe(core, srcs, flags, natives.Libs, true, work, "asuka-std-host")
	})
	if stdErr != nil {
		t.Fatal(stdErr)
	}
	return stdExe
}
